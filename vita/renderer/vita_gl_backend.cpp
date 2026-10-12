// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-17 vitaGL backend: the drawing surface ww_renderer submits to.
//
// The submission shape is the VITA-WW-02 demo's, proven in Vita3K (vitaGL +
// libshacccg init, Cg compile, draws, EFB copy), with the pieces a real game
// needs: programs cached by the pinned shader key, one persistent vertex and
// index buffer, one texture object per texmap slot (re-specified per draw, so
// nothing leaks per frame), and EFB copy destinations cached by guest address.
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

#include <vitaGL.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace gxc = gxruntime::gxcore;

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr GLuint kAttributePosition = 0;
constexpr GLuint kAttributeColor0 = 1;
constexpr GLuint kAttributeColor1 = 2;
constexpr GLuint kAttributeUv0 = 3;

class VitaGlBackend final : public WwRendererBackend {
public:
  const char* init() override {
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

    if (draw.texmap_mask != 0u) {
      for (std::uint32_t t = 0; t < 8u; ++t) {
        if ((draw.texmap_mask & (1u << t)) != 0u)
          bind_texture(draw.textures[t], t, gl_program);
      }
    } else {
      bind_texture(draw.texture, 0u, gl_program);
    }

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

    glDrawElements(draw.pipeline.topology,
                   static_cast<GLsizei>(draw.geometry.indices.size()),
                   GL_UNSIGNED_SHORT, nullptr);
    return check_gl("draw");
  }

  const char* submit_copy(const gx_vita::TranslatedCopy& copy) override {
    CopyTexture& destination = copy_textures_[copy.dest_address];
    if (destination.name == 0u)
      glGenTextures(1, &destination.name);
    glBindTexture(GL_TEXTURE_2D, destination.name);
    if (destination.width != copy.dest_width ||
        destination.height != copy.dest_height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                   static_cast<GLsizei>(copy.dest_width),
                   static_cast<GLsizei>(copy.dest_height), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      destination.width = copy.dest_width;
      destination.height = copy.dest_height;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLint>(copy.src_x),
                        static_cast<GLint>(copy.src_y),
                        static_cast<GLsizei>(copy.width),
                        static_cast<GLsizei>(copy.height));
    return check_gl("efb copy");
  }

  const char* present() override {
    vglSwapBuffers(GL_FALSE);
    return check_gl("present");
  }

  void shutdown() override {
    for (const auto& [key, program] : programs_)
      glDeleteProgram(program);
    programs_.clear();
    for (const auto& [address, texture] : copy_textures_)
      glDeleteTextures(1, &texture.name);
    copy_textures_.clear();
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
  struct CopyTexture {
    GLuint name = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

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
    programs_.emplace(draw.pipeline.program_key, linked);
    return linked;
  }

  void bind_texture(const gx_vita::TextureUpload& texture, std::uint32_t texmap,
                    GLuint gl_program) {
    if (!texture.valid || texmap >= 8u)
      return;
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texmap));
    glBindTexture(GL_TEXTURE_2D, texture_slots_[texmap]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(texture.width),
                 static_cast<GLsizei>(texture.height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texture.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.min_filter);
    char uniform[16];
    std::snprintf(uniform, sizeof uniform, "tex%u", texmap);
    glUniform1i(glGetUniformLocation(gl_program, uniform),
                static_cast<GLint>(texmap));
  }

  char error_[512] = {0};
  std::unordered_map<std::uint64_t, GLuint> programs_;
  std::unordered_map<std::uint32_t, CopyTexture> copy_textures_;
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
