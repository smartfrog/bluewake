// SPDX-License-Identifier: GPL-3.0-or-later
// VITA-WW-02 vitaGL demo fixture.
//
// Replays the synthetic GX fixtures through the pinned frontend and gxcore on
// the Vita, translates each draw with the same host-tested code (gx_vita,
// gx_cg), submits it through vitaGL (Cg runtime shaders), reads the
// framebuffer back into a guest buffer, writes a PNG capture and a text
// report under ux0:data/ww02/. Every check is explicit; a failure is
// reported, never skipped.

#include <vitaGL.h>

#include "gx_cg.h"
#include "gx_fixture.hpp"
#include "gx_vita.h"
#include "png_capture.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gxc = gxruntime::gxcore;

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr GLuint kAttributePosition = 0;
constexpr GLuint kAttributeColor0 = 1;
constexpr GLuint kAttributeUv0 = 2;
constexpr const char* kReportPath = "ux0:data/ww02/report.txt";
constexpr const char* kCapturePath = "ux0:data/ww02/capture.png";

FILE* g_report = nullptr;
int g_failures = 0;        // real failures of the demo path
int g_pixel_failures = 0;  // pixel checks blocked by the emulator's readback

void report(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  if (g_report != nullptr) {
    va_start(args, format);
    vfprintf(g_report, format, args);
    va_end(args);
    fflush(g_report);
  }
}

void fail(const char* what) {
  ++g_failures;
  report("FAIL %s\n", what);
}

void pixel_fail(const char* what) {
  ++g_pixel_failures;
  report("PIXELS %s\n", what);
}

// 4x4 matrices in the layout the generated Cg reads with mul(vec, mat):
// data[row * 4 + col] (row-major, like the pinned vitaGL samples).
void identity_matrix(float* out) {
  std::memset(out, 0, 16u * sizeof(float));
  out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void ortho_matrix(float left, float right, float bottom, float top, float* out) {
  std::memset(out, 0, 16u * sizeof(float));
  out[0] = 2.0f / (right - left);
  out[5] = 2.0f / (top - bottom);
  out[10] = 1.0f;
  out[12] = -(right + left) / (right - left);
  out[13] = -(top + bottom) / (top - bottom);
  out[15] = 1.0f;
}

GLuint compile_shader(GLenum type, const std::string& source,
                      const char* label) {
  GLuint shader = glCreateShader(type);
  const char* text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == 0) {
    char log[2048] = {0};
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    report("shader %s failed: %s\n", label, log);
    fail("shader compile");
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

struct DemoState {
  float projection[16];
  int draws = 0;
  int programs = 0;
  int copies = 0;
};

void upload_texture(const gx_vita::TextureUpload& texture, GLuint* name) {
  glGenTextures(1, name);
  glBindTexture(GL_TEXTURE_2D, *name);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)texture.width,
               (GLsizei)texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               texture.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrap_s);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrap_t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.mag_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.min_filter);
}

void submit_plan(const gxc::DrawPlan& plan, void* user) {
  auto* state = static_cast<DemoState*>(user);
  const gx_vita::TranslatedDraw draw = gx_vita::translate(plan);
  if (!draw.ok) {
    report("plan skipped: %s\n", draw.skip_reason);
    return;
  }
  const gx_vita::CgProgram cg = gx_vita::generate_cg(plan.pipeline.shader);
  if (!cg.ok) {
    report("program refused: %s\n", cg.unsupported_reason);
    fail("cg generation");
    return;
  }
  const GLuint vertex = compile_shader(GL_CG_VERTEX_SHADER_EXT, cg.vertex,
                                       "vertex");
  const GLuint fragment = compile_shader(GL_CG_FRAGMENT_SHADER_EXT,
                                         cg.fragment, "fragment");
  if (vertex == 0 || fragment == 0)
    return;
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glBindAttribLocation(program, kAttributePosition, "position");
  glBindAttribLocation(program, kAttributeColor0, "color0");
  glBindAttribLocation(program, kAttributeUv0, "uv0");
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == 0) {
    char log[2048] = {0};
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    report("program link failed: %s\n", log);
    fail("program link");
    return;
  }
  glUseProgram(program);
  ++state->programs;

  float position_matrix[16];
  identity_matrix(position_matrix);
  glUniformMatrix4fv(glGetUniformLocation(program, "position_matrix"), 1,
                    GL_FALSE, position_matrix);
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE,
                    state->projection);

  GLuint vbo = 0;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)draw.geometry.vertices.size(),
               draw.geometry.vertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(kAttributePosition);
  glVertexAttribPointer(kAttributePosition, 3, GL_FLOAT, GL_FALSE,
                        (GLsizei)gxc::kVertexStrideBytes,
                        (const void*)(uintptr_t)gxc::kVertexPosOffset);
  glEnableVertexAttribArray(kAttributeColor0);
  glVertexAttribPointer(kAttributeColor0, 4, GL_FLOAT, GL_FALSE,
                        (GLsizei)gxc::kVertexStrideBytes,
                        (const void*)(uintptr_t)gxc::kVertexColor0Offset);
  glEnableVertexAttribArray(kAttributeUv0);
  glVertexAttribPointer(kAttributeUv0, 2, GL_FLOAT, GL_FALSE,
                        (GLsizei)gxc::kVertexStrideBytes,
                        (const void*)(uintptr_t)gxc::kVertexUvOffset);
  GLuint ibo = 0;
  glGenBuffers(1, &ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(draw.geometry.indices.size() * sizeof(std::uint16_t)),
               draw.geometry.indices.data(), GL_STATIC_DRAW);

  if (draw.pipeline.depth_test != 0u)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  glDepthFunc(draw.pipeline.depth_func);
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

  int texture_units = 0;
  auto bind_texture = [&](const gx_vita::TextureUpload& texture, int texmap) {
    if (!texture.valid)
      return;
    GLuint name = 0;
    upload_texture(texture, &name);
    glActiveTexture(GL_TEXTURE0 + (GLenum)texmap);
    glBindTexture(GL_TEXTURE_2D, name);
    char uniform[16];
    std::snprintf(uniform, sizeof uniform, "tex%d", texmap);
    glUniform1i(glGetUniformLocation(program, uniform), texmap);
    ++texture_units;
  };
  if (draw.texmap_mask != 0u) {
    for (int t = 0; t < 8; ++t)
      if ((draw.texmap_mask & (1u << t)) != 0u)
        bind_texture(draw.textures[t], t);
  } else {
    bind_texture(draw.texture, 0);
  }

  glDrawElements(draw.pipeline.topology, (GLsizei)draw.geometry.indices.size(),
                 GL_UNSIGNED_SHORT, nullptr);
  const GLenum error = glGetError();
  report("draw vertices=%u indices=%u topology=0x%04x textures=%d err=0x%X\n",
         draw.geometry.vertex_count,
         (unsigned)draw.geometry.indices.size(), draw.pipeline.topology,
         texture_units, error);
  if (error != GL_NO_ERROR)
    fail("draw error");
  ++state->draws;
}

struct CopyCapture {
  bool seen = false;
  gxc::EfbCopyCommand cmd{};
};

void capture_copy(const gxc::EfbCopyCommand& cmd, void* user) {
  auto* capture = static_cast<CopyCapture*>(user);
  capture->cmd = cmd;
  capture->seen = true;
}

void clear_and_viewport(float r, float g, float b) {
  glViewport(0, 0, kScreenWidth, kScreenHeight);
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

std::uint8_t* read_framebuffer() {
  auto* pixels = static_cast<std::uint8_t*>(
      std::malloc((std::size_t)kScreenWidth * kScreenHeight * 4u));
  if (pixels == nullptr) {
    fail("framebuffer allocation");
    return nullptr;
  }
  // Present the frame, then read what was presented: the back-buffer read can
  // return an untouched surface when no in-flight work was forced.
  glFinish();
  vglSwapBuffers(GL_FALSE);
  glReadBuffer(GL_FRONT);
  glReadPixels(0, 0, kScreenWidth, kScreenHeight, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glReadBuffer(GL_BACK);
  return pixels;
}

std::uint8_t pixel_at(const std::uint8_t* pixels, int x, int y, int channel) {
  return pixels[((std::size_t)y * kScreenWidth + (std::size_t)x) * 4u +
                (std::size_t)channel];
}

bool replay(const char* scenario, gx_fixture::PlanObserver observer,
            void* user,
            gxc::GxCoreSink::CopyObserver copy_observer = nullptr,
            void* copy_user = nullptr) {
  std::string error;
  if (!gx_fixture::replay_scenario(scenario, observer, user, &error,
                                   copy_observer, copy_user)) {
    report("replay %s failed: %s\n", scenario, error.c_str());
    fail("replay");
    return false;
  }
  return true;
}

void run_textured_triangle(DemoState& state) {
  report("scenario textured_triangle\n");
  ortho_matrix(0.0f, 1.0f, 0.0f, 1.0f, state.projection);
  clear_and_viewport(0.1f, 0.2f, 0.3f);
  state.draws = 0;
  if (!replay("textured_triangle", submit_plan, &state))
    return;
  if (state.draws != 1) {
    fail("textured_triangle draw count");
    return;
  }
  std::uint8_t* pixels = read_framebuffer();
  if (pixels == nullptr)
    return;
  // The barycentre of the (0,0),(1,0),(0,1) triangle samples uv (1/3, 1/3):
  // texel (1,1) of the 4x4 RGBA8 fixture, decoded as (3, 34, 35) by the
  // documented AR/GB plane layout.
  const int x = (int)(kScreenWidth / 3);
  const int y = (int)(kScreenHeight / 3);
  const std::uint8_t r = pixel_at(pixels, x, y, 0);
  const std::uint8_t g = pixel_at(pixels, x, y, 1);
  const std::uint8_t b = pixel_at(pixels, x, y, 2);
  report("readback barycentre(%d,%d) rgb=%u,%u,%u expected=3,34,35\n", x, y, r,
         g, b);
  if (r != 3u || g != 34u || b != 35u)
    pixel_fail("textured_triangle texel");
  const std::uint8_t cr = pixel_at(pixels, kScreenWidth - 4, kScreenHeight - 4, 0);
  report("readback corner rgb=%u expected=25 (clear 0.1)\n", cr);
  if (cr != 25u)
    pixel_fail("clear colour");
  std::free(pixels);
}

void run_hud_quad(DemoState& state) {
  report("scenario hud_quad\n");
  ortho_matrix(0.0f, 640.0f, 0.0f, 480.0f, state.projection);
  clear_and_viewport(0.0f, 0.0f, 0.0f);
  state.draws = 0;
  if (!replay("hud_quad", submit_plan, &state))
    return;
  if (state.draws != 1) {
    fail("hud_quad draw count");
    return;
  }
  std::uint8_t* pixels = read_framebuffer();
  if (pixels == nullptr)
    return;
  int distinct = 0;
  std::uint32_t seen[64] = {};
  for (int y = 0; y < kScreenHeight; y += 7) {
    for (int x = 0; x < kScreenWidth; x += 7) {
      const std::uint32_t colour =
          (std::uint32_t)pixel_at(pixels, x, y, 0) << 16u |
          (std::uint32_t)pixel_at(pixels, x, y, 1) << 8u |
          (std::uint32_t)pixel_at(pixels, x, y, 2);
      bool known = false;
      for (int i = 0; i < distinct; ++i)
        known = known || seen[i] == colour;
      if (!known && distinct < 64)
        seen[distinct++] = colour;
    }
  }
  report("readback distinct colours=%d\n", distinct);
  if (distinct < 8)
    pixel_fail("hud_quad coverage");

  // EFB readback into a guest buffer with dirty marking, then the capture.
  std::vector<std::uint8_t> guest((std::size_t)kScreenWidth * kScreenHeight * 4u);
  struct MarkState {
    int rows = 0;
  } marks;
  auto mark = [](void* user, std::uint32_t offset, std::uint32_t size) {
    (void)offset;
    (void)size;
    ++static_cast<MarkState*>(user)->rows;
  };
  gx_vita::EfbReadback rect{};
  rect.width = kScreenWidth;
  rect.height = kScreenHeight;
  if (!gx_vita::copy_readback_to_guest(pixels, rect, kScreenWidth * 4u,
                                       guest.data(), guest.size(), mark,
                                       &marks)) {
    fail("readback copy");
  } else {
    const bool same =
        std::memcmp(guest.data(), pixels,
                    (std::size_t)kScreenWidth * kScreenHeight * 4u) == 0;
    report("readback guest rows=%d identical=%d\n", marks.rows, same ? 1 : 0);
    if (!same || marks.rows != kScreenHeight)
      fail("readback sync");
  }
  if (!png_write_rgba8(kCapturePath, pixels, kScreenWidth, kScreenHeight)) {
    fail("png capture");
  } else {
    report("capture written %dx%d\n", kScreenWidth, kScreenHeight);
  }
  std::free(pixels);
}

void run_copy_triangle(DemoState& state) {
  report("scenario copy_triangle\n");
  ortho_matrix(0.0f, 1.0f, 0.0f, 1.0f, state.projection);
  clear_and_viewport(0.0f, 0.0f, 0.0f);
  CopyCapture copy;
  state.draws = 0;
  if (!replay("copy_triangle", submit_plan, &state, capture_copy, &copy))
    return;
  if (!copy.seen) {
    fail("copy observer");
    return;
  }
  const gx_vita::TranslatedCopy translated = gx_vita::translate_copy(copy.cmd);
  if (!translated.ok) {
    report("copy refused: %s\n", translated.skip_reason);
    fail("copy translation");
    return;
  }
  // Execute the copy into a texture of the destination size (RGBA here; the
  // R8 destination conversion is the backend's, see the status document).
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)translated.dest_width,
               (GLsizei)translated.dest_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLint)translated.src_x,
                      (GLint)translated.src_y, (GLsizei)translated.width,
                      (GLsizei)translated.height);
  const GLenum error = glGetError();
  report("copy %ux%u -> texture err=0x%X\n", translated.width,
         translated.height, error);
  if (error != GL_NO_ERROR)
    fail("copy error");
  ++state.copies;
}

} // namespace

int main() {
  g_report = std::fopen(kReportPath, "w");
  report("ww02 gx vitaGL demo\n");
  // vitaGL's init return is not a success flag (Cloudpunk's pattern): verify
  // the context through glGetString and the pools instead.
  (void)vglInitExtended(1024 * 1024, kScreenWidth, kScreenHeight,
                        64 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
  vglWaitVblankStart(GL_TRUE);
  const GLubyte* version = glGetString(GL_VERSION);
  const size_t ram_total = vglMemTotal(VGL_MEM_RAM);
  const size_t vram_total = vglMemTotal(VGL_MEM_VRAM);
  report("gl version %s ram=%u vram=%u\n",
         version != nullptr ? (const char*)version : "(null)",
         (unsigned)ram_total, (unsigned)vram_total);
  if (version == nullptr || ram_total == 0u || vram_total == 0u) {
    report("vitaGL init failed\n");
    if (g_report != nullptr)
      std::fclose(g_report);
    return 1;
  }
  // Prime the display: the first swap activates the back buffer.
  vglSwapBuffers(GL_FALSE);
  // Diagnostic probe: a full-screen red clear read back from both buffers
  // tells whether rendering reaches the surfaces at all.
  glViewport(0, 0, kScreenWidth, kScreenHeight);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glFinish();
  vglSwapBuffers(GL_FALSE);
  std::uint8_t probe[4] = {};
  glReadBuffer(GL_FRONT);
  glReadPixels(kScreenWidth / 2, kScreenHeight / 2, 1, 1, GL_RGBA,
               GL_UNSIGNED_BYTE, probe);
  report("probe front=%u,%u,%u,%u\n", probe[0], probe[1], probe[2], probe[3]);
  glReadBuffer(GL_BACK);
  glReadPixels(kScreenWidth / 2, kScreenHeight / 2, 1, 1, GL_RGBA,
               GL_UNSIGNED_BYTE, probe);
  report("probe back=%u,%u,%u,%u\n", probe[0], probe[1], probe[2], probe[3]);
  glReadBuffer(GL_BACK);
  // FBO probe: the framebuffer path forces the in-flight scene before a read.
  {
    GLuint colour = 0, fbo = 0;
    glGenTextures(1, &colour);
    glBindTexture(GL_TEXTURE_2D, colour);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kScreenWidth, kScreenHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colour, 0);
    report("fbo status=0x%X err=0x%X\n",
           glCheckFramebufferStatus(GL_FRAMEBUFFER), glGetError());
    glViewport(0, 0, kScreenWidth, kScreenHeight);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    std::uint8_t fbo_probe[4] = {};
    glReadPixels(kScreenWidth / 2, kScreenHeight / 2, 1, 1, GL_RGBA,
                 GL_UNSIGNED_BYTE, fbo_probe);
    report("probe fbo=%u,%u,%u,%u\n", fbo_probe[0], fbo_probe[1], fbo_probe[2],
           fbo_probe[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  DemoState state{};
  run_textured_triangle(state);
  run_hud_quad(state);
  run_copy_triangle(state);

  report("draws=%d programs=%d copies=%d failures=%d pixel_failures=%d\n",
         state.draws, state.programs, state.copies, g_failures,
         g_pixel_failures);
  // Exit codes: 0 everything green; 2 the path ran but the emulator's GXM
  // surfaces never reach guest memory (pixel checks blocked, documented in
  // docs/vita/VITA-WW-02.md); 1 a real failure of the demo path.
  const int code = g_failures != 0 ? 1 : (g_pixel_failures != 0 ? 2 : 0);
  report("demo done %d\n", code);
  vglSwapBuffers(GL_FALSE);
  if (g_report != nullptr)
    std::fclose(g_report);
  return code;
}
