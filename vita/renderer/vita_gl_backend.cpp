// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 vitaGL backend: the drawing surface ww_renderer submits to.
//
// The submission shape is the VITA-WW-02 demo's, proven in Vita3K (vitaGL +
// libshacccg init, Cg compile, draws, EFB copy), with the pieces a real game
// needs: programs cached by the pinned shader key, one persistent vertex and
// index buffer, one texture object per texmap slot (re-specified per draw, so
// nothing leaks per frame). EFB copies do not retain unused GPU textures.
// Draws go to the default framebuffer at the Vita's 960x544; the EFB-to-screen
// mapping (the game renders 640x528) is a display decision left to the
// measurement ticket.
//
// Vita-only: the host build has no GPU and gets the NULL platform backend
// (ww_renderer.cpp), so the same renderer core is testable on the host with a
// recording backend.

#ifdef __vita__

#include "ww_renderer.h"
#include "ww_renderer_backend.h"
#include "gx_texgen.h"
#if defined(WW_PROGRAM_CACHE)
#include "program_cache.h"
#endif

#include <vitaGL.h>

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#if defined(WW_PROGRAM_CACHE)
#include <cstdlib>
#include <memory>
#include <vector>
#endif

namespace gxc = gxruntime::gxcore;

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
#if defined(WW_PROGRAM_CACHE)
constexpr const char* kDefaultProgramCacheDirectory =
    "ux0:data/bluewake/program-cache";
#endif
constexpr GLuint kAttributePosition = 0;
constexpr GLuint kAttributeColor0 = 1;
constexpr GLuint kAttributeColor1 = 2;
constexpr GLuint kAttributeUv0 = 3;

class VitaGlBackend final : public WwRendererBackend {
public:
  const char* init() override {
#if defined(WW_RENDERER_DRAW_CENSUS)
    census_ = {};
    census_presented_ = 0;
    census_pending_ = false;
    alpha_cache_ = {};
    logo_state_ = {};
#endif
    // vitaGL's init return is not a success flag (Cloudpunk's pattern): verify
    // the context through glGetString and the pools instead.
    (void)vglInitExtended(1024 * 1024, kScreenWidth, kScreenHeight,
                          64 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    if (glGetString(GL_VERSION) == nullptr)
      return "vitaGL context unavailable";
    if (vglMemTotal(VGL_MEM_RAM) == 0u || vglMemTotal(VGL_MEM_VRAM) == 0u)
      return "vitaGL memory pools unavailable";
    glViewport(0, 0, kScreenWidth, kScreenHeight);
    glGenBuffers(1, &vertex_buffer_);
    glGenBuffers(1, &index_buffer_);
    glGenBuffers(1, &texcoord_buffer_);
    glGenTextures(8, texture_slots_);
#if defined(WW_PROGRAM_CACHE)
    const char* directory = std::getenv("WW_PROGRAM_CACHE_DIR");
    program_cache_.reset(new gx_vita::ProgramCache(
        directory != nullptr && directory[0] != '\0'
            ? directory
            : kDefaultProgramCacheDirectory,
        gx_vita::kProgramCacheDefaultMaxBytes));
#endif
    return nullptr;
  }

  const char* submit_draw(const gx_vita::TranslatedDraw& draw,
                          const gx_vita::CgProgram& program) override {
    const GLuint gl_program = program_for(draw, program);
    if (gl_program == 0u)
      return error_;
    glUseProgram(gl_program);
    glUniformMatrix4fv(glGetUniformLocation(gl_program, "position_matrix"), 1,
                       GL_FALSE, draw.position_matrix);
    glUniformMatrix4fv(glGetUniformLocation(gl_program, "projection"), 1,
                       GL_FALSE, draw.projection);
    glUniform4fv(glGetUniformLocation(gl_program, "tev_colors"), 4,
                 &draw.tev_colors[0][0]);
    glUniform4fv(glGetUniformLocation(gl_program, "konst_colors"), 4,
                 &draw.konst_colors[0][0]);
    glUniform2fv(glGetUniformLocation(gl_program, "alpha_ref"), 1, draw.alpha_ref);
    glUniform2fv(glGetUniformLocation(gl_program, "ztexture_bias"), 1,
                 draw.ztexture_bias);
    glUniform4fv(glGetUniformLocation(gl_program, "fog_params"), 1, draw.fog.params);
    glUniform4fv(glGetUniformLocation(gl_program, "fog_color"), 1, draw.fog.color);
    glUniform2fv(glGetUniformLocation(gl_program, "fog_depth"), 1, draw.fog.depth);
    glUniform4fv(glGetUniformLocation(gl_program, "fog_range"), 3, &draw.fog.range[0][0]);
    const float fog_screen[] = {draw.fog.viewport[0] / kScreenWidth, draw.fog.viewport[1]};
    glUniform2fv(glGetUniformLocation(gl_program, "fog_screen"), 1, fog_screen);
    glUniform4fv(glGetUniformLocation(gl_program, "texture_dimensions"), 8,
                 &draw.texture_dimensions[0][0]);
    glUniform4fv(glGetUniformLocation(gl_program, "indirect_matrices"), 6,
                 &draw.indirect_matrices[0][0]);

    if (draw.pipeline.depth_test != 0u)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);
    glDepthFunc(draw.pipeline.depth_func);
    glDepthRangef(draw.pipeline.depth_near, draw.pipeline.depth_far);
    glDepthMask(draw.pipeline.depth_write != 0u ? GL_TRUE : GL_FALSE);
    if (draw.pipeline.blend_enable != 0u) {
      glEnable(GL_BLEND);
      glBlendEquation(draw.pipeline.blend_equation);
      glBlendFuncSeparate(draw.pipeline.blend_src, draw.pipeline.blend_dst,
                          draw.pipeline.blend_src_alpha,
                          draw.pipeline.blend_dst_alpha);
    } else {
      glDisable(GL_BLEND);
    }
    if (draw.pipeline.cull_enable != 0u) {
      glEnable(GL_CULL_FACE);
      glCullFace(draw.pipeline.cull_face);
    } else {
      glDisable(GL_CULL_FACE);
    }
    glColorMask(draw.pipeline.color_write != 0u ? GL_TRUE : GL_FALSE,
                draw.pipeline.color_write != 0u ? GL_TRUE : GL_FALSE,
                draw.pipeline.color_write != 0u ? GL_TRUE : GL_FALSE,
                draw.pipeline.alpha_write != 0u ? GL_TRUE : GL_FALSE);
    glFrontFace(draw.pipeline.front_face);

#if defined(WW_RENDERER_DRAW_CENSUS)
    if (const char* error = check_gl("pre-texture state"))
      return error;
    valid_texture_mask_ = skipped_texture_mask_ = 0;
    texture_error_ = nullptr;
#endif
    if (draw.texmap_mask != 0u) {
      for (std::uint32_t t = 0; t < 8u; ++t) {
        if ((draw.texmap_mask & (1u << t)) != 0u)
          bind_texture(draw.textures[t], t, gl_program);
      }
    } else {
      bind_texture(draw.texture, 0u, gl_program);
    }
#if defined(WW_RENDERER_DRAW_CENSUS)
    if (texture_error_ != nullptr)
      return texture_error_;
#endif

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(draw.geometry.vertices.size()),
                 draw.geometry.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(draw.geometry.indices.size() *
                                         sizeof(std::uint16_t)),
                 draw.geometry.indices.data(), GL_STATIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(gxc::kVertexStrideBytes);
    auto attribute = [&](GLuint index, GLint size, std::uint32_t offset) {
      glEnableVertexAttribArray(index);
      glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(
                                static_cast<std::uintptr_t>(offset)));
    };
    attribute(kAttributePosition, 3, gxc::kVertexPosOffset);
    attribute(kAttributeColor0, 4, gxc::kVertexColor0Offset);
    attribute(kAttributeColor1, 4, gxc::kVertexColor1Offset);
    glBindBuffer(GL_ARRAY_BUFFER, texcoord_buffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(draw.geometry.texcoords.size() * sizeof(float)),
                 draw.geometry.texcoords.data(), GL_STATIC_DRAW);
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t) {
      glEnableVertexAttribArray(kAttributeUv0 + t);
      glVertexAttribPointer(kAttributeUv0 + t, 3, GL_FLOAT, GL_FALSE,
                            gx_vita::texgen::kFloatsPerVertex * sizeof(float),
                            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(12u * t)));
    }

#if defined(WW_RENDERER_DRAW_CENSUS)
    if (draw.census.logo_mask && census_presented_ + 1 >= gx_vita::census::kFirstLogoStateFrame &&
        census_presented_ < gx_vita::census::kMaxPresentedFrames && !logo_state_.captured) {
      if (const char* error = check_gl("pre-logo-query geometry upload"))
        return error;
      if (const char* error = capture_logo_state(draw))
        return error;
    }
#endif
    glDrawElements(draw.pipeline.topology,
                   static_cast<GLsizei>(draw.geometry.indices.size()),
                   GL_UNSIGNED_SHORT, nullptr);
#if defined(WW_RENDERER_DRAW_CENSUS)
    if (census_presented_ < gx_vita::census::kMaxPresentedFrames) {
      gx_vita::census::count_draw(census_, draw.census, valid_texture_mask_, skipped_texture_mask_);
      census_pending_ = true;
    }
#endif
    return check_gl("draw");
  }

#if defined(WW_RENDERER_DRAW_CENSUS)
  void census_refused() override {
    if (census_presented_ < gx_vita::census::kMaxPresentedFrames) {
      ++census_.refused;
      census_pending_ = true;
    }
  }
#endif

  const char* finish() override {
    glFinish();
    return check_gl("GPU finish");
  }

  const char* submit_copy(const gx_vita::TranslatedCopy&) override {
    // Draws upload guest textures into texture_slots_; no draw or readback
    // consumes a GPU copy destination. Keeping one per guest address only
    // retained unreachable storage; it never performed guest-memory copyback
    // or copy clears. Those existing limitations are unchanged here.
    return check_gl("efb copy");
  }

  const char* present() override {
    vglSwapBuffers(GL_FALSE);
#if defined(WW_RENDERER_DRAW_CENSUS)
    if (const char* error = check_gl("present"))
      return error;
    if (census_presented_ < gx_vita::census::kMaxPresentedFrames) {
      ++census_presented_;
      dump_census(false);
    }
    return nullptr;
#else
    return check_gl("present");
#endif
  }

  const char* readback(WwRendererFramePixels& frame) override {
    frame.width = kScreenWidth;
    frame.height = kScreenHeight;
    frame.rgba.resize(std::size_t(kScreenWidth) * kScreenHeight * 4);
    glFinish();
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, kScreenWidth, kScreenHeight, GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.rgba.data());
    glReadBuffer(GL_BACK);
    return check_gl("front-buffer readback");
  }

  void shutdown() override {
#if defined(WW_RENDERER_DRAW_CENSUS)
    if (census_pending_)
      dump_census(true);
#endif
#if defined(WW_PROGRAM_CACHE)
    if (program_cache_ != nullptr) {
      const gx_vita::ProgramCacheStats& stats = program_cache_->stats();
      std::fprintf(stderr,
                   "program-cache: hits=%llu misses=%llu rejected=%llu "
                   "stores=%llu store_failures=%llu evictions=%llu\n",
                   static_cast<unsigned long long>(stats.hits),
                   static_cast<unsigned long long>(stats.misses),
                   static_cast<unsigned long long>(stats.rejected),
                   static_cast<unsigned long long>(stats.stores),
                   static_cast<unsigned long long>(stats.store_failures),
                   static_cast<unsigned long long>(stats.evictions));
    }
#endif
    for (const auto& [key, program] : programs_)
      glDeleteProgram(program);
    programs_.clear();
    if (vertex_buffer_ != 0u)
      glDeleteBuffers(1, &vertex_buffer_);
    if (index_buffer_ != 0u)
      glDeleteBuffers(1, &index_buffer_);
    if (texcoord_buffer_ != 0u)
      glDeleteBuffers(1, &texcoord_buffer_);
    texcoord_buffer_ = 0;
    vertex_buffer_ = index_buffer_ = 0u;
    glDeleteTextures(8, texture_slots_);
    for (std::uint32_t t = 0; t < 8u; ++t)
      texture_slots_[t] = 0u;
  }

private:
#if defined(WW_RENDERER_DRAW_CENSUS)
  struct LogoState {
    bool captured = false, cache_full = false;
    gx_vita::census::DrawInfo info;
    gx_vita::census::AlphaSample alpha;
    std::uint32_t ordinal = 0;
    GLboolean blend = 0, depth = 0, depth_write = 0, cull = 0, scissor = 0, mask[4]{};
    GLint equation = 0, src = 0, dst = 0, src_alpha = 0, dst_alpha = 0;
    GLint cull_face = 0, front_face = 0, box[4]{}, viewport[4]{};
    float tev_alpha[4]{}, konst_alpha[4]{}, refs[2]{};
    std::uint32_t use_dst_alpha = 0, output_dst_alpha = 0;
  };

  const char* capture_logo_state(const gx_vita::TranslatedDraw& draw) {
    auto& state = logo_state_;
    state.info = draw.census;
    state.ordinal = census_.textured_quad + census_.untextured_quad + census_.other + 1;
    state.blend = glIsEnabled(GL_BLEND);
    state.depth = glIsEnabled(GL_DEPTH_TEST);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depth_write);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.mask);
    glGetIntegerv(GL_BLEND_EQUATION, &state.equation);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.src);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.dst_alpha);
    glGetIntegerv(GL_CULL_FACE_MODE, &state.cull_face);
    glGetIntegerv(GL_FRONT_FACE, &state.front_face);
    glGetIntegerv(GL_SCISSOR_BOX, state.box);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    if (const char* error = check_gl("logo state query"))
      return error;
    const auto slot = draw.census.logo_texmap;
    if (slot >= 8)
      return "logo census texture slot out of range";
    const auto& texture = draw.texmap_mask ? draw.textures[slot] : draw.texture;
    const auto* sample = alpha_cache_.observe(draw.census.logo_texture, texture, census_presented_ + 1);
    state.cache_full = sample == nullptr;
    if (sample != nullptr)
      state.alpha = *sample;
    for (unsigned i = 0; i < 4; ++i) {
      state.tev_alpha[i] = draw.tev_colors[i][3];
      state.konst_alpha[i] = draw.konst_colors[i][3];
    }
    state.refs[0] = draw.alpha_ref[0];
    state.refs[1] = draw.alpha_ref[1];
    state.use_dst_alpha = draw.pipeline.use_dst_alpha;
    state.output_dst_alpha = draw.pipeline.dst_alpha;
    state.captured = true;
    return nullptr;
  }

  void dump_logo_state(bool partial) {
    const auto& s = logo_state_;
    const auto& a = s.alpha.stats;
    const auto& g = s.info.geometry;
    char stages[16 * 9 + 1] = "-";
    constexpr const char* hex = "0123456789abcdef";
    for (unsigned n = 0; n < s.info.alpha_stage_count; ++n) {
      for (unsigned i = 0; i < 8; ++i)
        stages[9*n+i] = hex[(s.info.alpha_stages[n] >> (28-4*i)) & 15];
      stages[9*n+8] = n + 1 == s.info.alpha_stage_count ? '\0' : ',';
    }
    const auto* ras0 = s.info.raster_alpha[0];
    const auto* ras1 = s.info.raster_alpha[1];
    std::fprintf(stderr,
        "[logo-state] p=%u partial=%u draw=%u tex=%08x,%u,%u,%u slot=%u sampled=%u "
        "a_valid=%u cache_full=%u a=%u,%u,%.3f nz=%u/%u "
        "blend=%u,%d,%d,%d,%d,%d depth=%u,%u cull=%u,%d,%d "
        "scissor=%u,%d,%d,%d,%d viewport=%d,%d,%d,%d mask=%u%u%u%u "
        "rasA=%.6g,%.6g/%.6g,%.6g tevA=%.6g,%.6g,%.6g,%.6g "
        "kA=%.6g,%.6g,%.6g,%.6g atest=%u,%u,%u,%.6g,%.6g tev=%s "
        "override=%u,%u geom=%u ndc=%.9g,%.9g,%.9g:%.9g,%.9g,%.9g "
        "w=%.9g,%.9g area=%.9g,%.9g fragA=unobserved\n",
        census_presented_, partial ? 1u : 0u, s.ordinal, s.info.logo_texture.address,
        s.info.logo_texture.format, s.info.logo_texture.width, s.info.logo_texture.height,
        s.info.logo_texmap, s.alpha.present, a.valid ? 1u : 0u, s.cache_full ? 1u : 0u,
        a.minimum, a.maximum, a.mean(), a.nonzero, a.texels,
        unsigned(s.blend), s.equation, s.src, s.dst, s.src_alpha, s.dst_alpha,
        unsigned(s.depth), unsigned(s.depth_write), unsigned(s.cull), s.cull_face, s.front_face,
        unsigned(s.scissor), s.box[0], s.box[1], s.box[2], s.box[3],
        s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3],
        unsigned(s.mask[0]), unsigned(s.mask[1]), unsigned(s.mask[2]), unsigned(s.mask[3]),
        *std::min_element(ras0,ras0+4), *std::max_element(ras0,ras0+4),
        *std::min_element(ras1,ras1+4), *std::max_element(ras1,ras1+4),
        s.tev_alpha[0], s.tev_alpha[1], s.tev_alpha[2], s.tev_alpha[3],
        s.konst_alpha[0], s.konst_alpha[1], s.konst_alpha[2], s.konst_alpha[3],
        unsigned(s.info.alpha_test[0]), unsigned(s.info.alpha_test[1]), unsigned(s.info.alpha_test[2]),
        s.refs[0], s.refs[1], stages, s.use_dst_alpha, s.output_dst_alpha, g.valid ? 1u : 0u,
        g.minimum[0], g.minimum[1], g.minimum[2], g.maximum[0], g.maximum[1], g.maximum[2],
        g.w_minimum, g.w_maximum, g.twice_area[0], g.twice_area[1]);
  }

  void dump_census(bool partial) {
    std::fprintf(stderr,
        "[draw-census] present=%u partial=%u tq=%u uq=%u other=%u full=%u "
        "no_valid=%u incomplete=%u decode_failed=%u bind_valid=%u bind_skipped=%u "
        "required_skipped=%u bind_failed=%u upload_failed=%u logo=%u logo_valid=%u "
        "logo_skipped=%u refused=%u\n",
        census_presented_, partial ? 1u : 0u, census_.textured_quad, census_.untextured_quad,
        census_.other, census_.fullscreen, census_.no_valid_texture, census_.incomplete_texture,
        census_.decode_failed, census_.bind_valid, census_.bind_skipped, census_.required_skipped,
        census_.bind_failed, census_.upload_failed, census_.logo, census_.logo_valid,
        census_.logo_skipped, census_.refused);
    if (logo_state_.captured)
      dump_logo_state(partial);
    std::fflush(stderr);
    census_ = {};
    logo_state_ = {};
    census_pending_ = false;
  }

  void census_bind(gx_vita::census::BindStatus status) {
    if (census_presented_ < gx_vita::census::kMaxPresentedFrames) {
      gx_vita::census::count_bind(census_, status);
      census_pending_ = true;
    }
  }

  gx_vita::census::Counts census_;
  std::uint32_t census_presented_ = 0;
  std::uint32_t valid_texture_mask_ = 0;
  std::uint32_t skipped_texture_mask_ = 0;
  bool census_pending_ = false;
  const char* texture_error_ = nullptr;
  gx_vita::census::AlphaCache alpha_cache_;
  LogoState logo_state_;
#endif

  const char* check_gl(const char* what) {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
      return nullptr;
    std::snprintf(error_, sizeof error_, "vitaGL %s error 0x%X", what,
                  static_cast<unsigned>(error));
    return error_;
  }

  GLuint compile_shader(GLenum type, const std::string& source,
                        const char* label) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
      char log[512] = {0};
      glGetShaderInfoLog(shader, sizeof log, nullptr, log);
      std::snprintf(error_, sizeof error_, "vitaGL %s shader refused: %.400s",
                    label, log);
      glDeleteShader(shader);
      return 0u;
    }
    return shader;
  }

  GLuint program_for(const gx_vita::TranslatedDraw& draw,
                     const gx_vita::CgProgram& program) {
    const auto found = programs_.find(draw.pipeline.program_key);
    if (found != programs_.end())
      return found->second;
#if defined(WW_PROGRAM_CACHE)
    const gx_vita::ProgramCacheKey cache_key =
        gx_vita::make_program_cache_key(draw.pipeline.program_key,
                                        program.vertex, program.fragment);
    std::vector<std::uint8_t> binary;
    if (program_cache_ != nullptr && program_cache_->load(cache_key, &binary)) {
      const GLuint restored = glCreateProgram();
      glProgramBinary(restored, 0u, binary.data(),
                      static_cast<GLsizei>(binary.size()));
      GLint restored_status = 0;
      glGetProgramiv(restored, GL_LINK_STATUS, &restored_status);
      if (restored_status != 0) {
        programs_.emplace(draw.pipeline.program_key, restored);
        return restored;
      }
      glDeleteProgram(restored);
      std::fprintf(stderr,
                   "program-cache: cached binary for key %016llx did not link, "
                   "compiling\n",
                   static_cast<unsigned long long>(draw.pipeline.program_key));
    }
#endif
    const GLuint vertex =
        compile_shader(GL_CG_VERTEX_SHADER_EXT, program.vertex, "vertex");
    if (vertex == 0u)
      return 0u;
    const GLuint fragment =
        compile_shader(GL_CG_FRAGMENT_SHADER_EXT, program.fragment, "fragment");
    if (fragment == 0u) {
      glDeleteShader(vertex);
      return 0u;
    }
    const GLuint linked = glCreateProgram();
    glAttachShader(linked, vertex);
    glAttachShader(linked, fragment);
    glBindAttribLocation(linked, kAttributePosition, "position");
    glBindAttribLocation(linked, kAttributeColor0, "color0");
    glBindAttribLocation(linked, kAttributeColor1, "color1");
    for (std::uint32_t t = 0; t < gxc::kMaxTexGens; ++t) {
      char name[8];
      std::snprintf(name, sizeof name, "uv%u", t);
      glBindAttribLocation(linked, kAttributeUv0 + t, name);
    }
    glLinkProgram(linked);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint status = 0;
    glGetProgramiv(linked, GL_LINK_STATUS, &status);
    if (status == 0) {
      char log[512] = {0};
      glGetProgramInfoLog(linked, sizeof log, nullptr, log);
      std::snprintf(error_, sizeof error_, "vitaGL program link refused: %.400s",
                    log);
      glDeleteProgram(linked);
      return 0u;
    }
#if defined(WW_PROGRAM_CACHE)
    if (program_cache_ != nullptr) {
      GLint binary_size = 0;
      glGetProgramiv(linked, GL_PROGRAM_BINARY_LENGTH, &binary_size);
      if (binary_size > 0 &&
          static_cast<std::size_t>(binary_size) <=
              gx_vita::kProgramCacheDefaultMaxEntryBytes) {
        std::vector<std::uint8_t> saved(static_cast<std::size_t>(binary_size));
        GLsizei written = 0;
        GLenum format = 0;
        glGetProgramBinary(linked, binary_size, &written, &format,
                           saved.data());
        if (written > 0 &&
            static_cast<std::size_t>(written) <= saved.size() &&
            !program_cache_->store(cache_key, saved.data(),
                                   static_cast<std::size_t>(written))) {
          std::fprintf(stderr, "program-cache: could not store key %016llx\n",
                       static_cast<unsigned long long>(
                           draw.pipeline.program_key));
        }
      }
    }
#endif
    programs_.emplace(draw.pipeline.program_key, linked);
    return linked;
  }

  void bind_texture(const gx_vita::TextureUpload& texture, std::uint32_t texmap,
                    GLuint gl_program) {
#if defined(WW_RENDERER_DRAW_CENSUS)
    if (texture_error_ != nullptr)
      return;
#endif
    if (!texture.valid || texmap >= 8u) {
#if defined(WW_RENDERER_DRAW_CENSUS)
      census_bind(gx_vita::census::BindStatus::Skipped);
      if (texmap < 8u)
        skipped_texture_mask_ |= 1u << texmap;
#endif
      return;
    }
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texmap));
    glBindTexture(GL_TEXTURE_2D, texture_slots_[texmap]);
#if defined(WW_RENDERER_DRAW_CENSUS)
    texture_error_ = check_gl("texture bind");
    if (texture_error_ != nullptr) {
      census_bind(gx_vita::census::BindStatus::BindFailed);
      return;
    }
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(texture.width),
                 static_cast<GLsizei>(texture.height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texture.rgba.data());
#if defined(WW_RENDERER_DRAW_CENSUS)
    texture_error_ = check_gl("texture upload");
    if (texture_error_ != nullptr) {
      census_bind(gx_vita::census::BindStatus::UploadFailed);
      return;
    }
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.min_filter);
    char uniform[16];
    std::snprintf(uniform, sizeof uniform, "tex%u", texmap);
    glUniform1i(glGetUniformLocation(gl_program, uniform),
                static_cast<GLint>(texmap));
#if defined(WW_RENDERER_DRAW_CENSUS)
    texture_error_ = check_gl("texture sampler");
    if (texture_error_ != nullptr) {
      census_bind(gx_vita::census::BindStatus::BindFailed);
      return;
    }
    census_bind(gx_vita::census::BindStatus::Valid);
    valid_texture_mask_ |= 1u << texmap;
#endif
  }

  char error_[512] = {0};
  std::unordered_map<std::uint64_t, GLuint> programs_;
#if defined(WW_PROGRAM_CACHE)
  std::unique_ptr<gx_vita::ProgramCache> program_cache_;
#endif
  GLuint vertex_buffer_ = 0;
  GLuint index_buffer_ = 0;
  GLuint texcoord_buffer_ = 0;
  GLuint texture_slots_[8] = {0};
};

} // namespace

WwRendererBackend* ww_renderer_vita_backend(void) {
  static VitaGlBackend backend;
  return &backend;
}

#endif // __vita__
