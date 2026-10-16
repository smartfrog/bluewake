// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

using GLenum = unsigned;
using GLuint = unsigned;
using GLint = int;
using GLsizei = int;
using GLsizeiptr = std::ptrdiff_t;
using GLboolean = unsigned char;

enum : unsigned {
  GL_FALSE, GL_TRUE, GL_VERSION, GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE,
  GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_FLOAT,
  GL_UNSIGNED_SHORT, GL_TEXTURE_2D, GL_RGBA, GL_UNSIGNED_BYTE,
  GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE,
  GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_FRONT, GL_BACK,
  GL_CG_VERTEX_SHADER_EXT, GL_CG_FRAGMENT_SHADER_EXT, GL_COMPILE_STATUS,
  GL_LINK_STATUS, GL_TEXTURE0, VGL_MEM_RAM, VGL_MEM_VRAM,
  SCE_GXM_MULTISAMPLE_NONE,
};
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_INVALID_VALUE = 0x501;

namespace test_gl {
struct Texture {
  std::size_t bytes = 0;
  std::vector<std::uint8_t> upload;
};
inline std::map<GLuint, Texture> textures;
inline std::map<GLuint, bool> buffers;
inline GLuint next_name = 1;
inline GLuint active_slot = 0;
inline GLuint bound[8]{};
inline GLenum error = GL_NO_ERROR;
inline unsigned generated_textures = 0;
inline unsigned deleted_textures = 0;
inline unsigned invalid_deletions = 0;
inline unsigned copies = 0;
inline unsigned draws = 0;
inline unsigned swaps = 0;
inline GLenum read_buffer = GL_BACK;
inline std::size_t texture_bytes() {
  std::size_t bytes = 0;
  for (const auto& [name, texture] : textures)
    bytes += texture.bytes;
  return bytes;
}
} // namespace test_gl

inline void glGenTextures(GLsizei count, GLuint* names) {
  for (int i = 0; i < count; ++i) {
    names[i] = test_gl::next_name++;
    test_gl::textures.emplace(names[i], test_gl::Texture{});
    ++test_gl::generated_textures;
  }
}
inline void glDeleteTextures(GLsizei count, const GLuint* names) {
  for (int i = 0; i < count; ++i) {
    if (names[i] == 0) continue;
    if (test_gl::textures.erase(names[i]) != 1)
      ++test_gl::invalid_deletions;
    ++test_gl::deleted_textures;
  }
}
inline void glActiveTexture(GLenum slot) { test_gl::active_slot = slot - GL_TEXTURE0; }
inline void glBindTexture(GLenum, GLuint name) { test_gl::bound[test_gl::active_slot] = name; }
inline void glTexImage2D(GLenum, GLint, GLint, GLsizei width, GLsizei height,
                         GLint, GLenum, GLenum, const void* data) {
  if (width < 0 || height < 0) { test_gl::error = GL_INVALID_VALUE; return; }
  auto& texture = test_gl::textures.at(test_gl::bound[test_gl::active_slot]);
  texture.bytes = std::size_t(width) * height * 4;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  texture.upload.clear();
  if (bytes != nullptr) texture.upload.assign(bytes, bytes + texture.bytes);
}
inline void glCopyTexSubImage2D(GLenum, GLint, GLint, GLint, GLint, GLint,
                               GLsizei, GLsizei) { ++test_gl::copies; }
inline void glGenBuffers(GLsizei count, GLuint* names) {
  for (int i = 0; i < count; ++i) {
    names[i] = test_gl::next_name++;
    test_gl::buffers.emplace(names[i], true);
  }
}
inline void glDeleteBuffers(GLsizei count, const GLuint* names) {
  for (int i = 0; i < count; ++i)
    if (test_gl::buffers.erase(names[i]) != 1) ++test_gl::invalid_deletions;
}
inline GLenum glGetError() {
  const auto error = test_gl::error;
  test_gl::error = GL_NO_ERROR;
  return error;
}
inline const unsigned char* glGetString(GLenum) { return reinterpret_cast<const unsigned char*>("test GL"); }
inline int vglInitExtended(int, int, int, int, int) { return 0; }
inline unsigned vglMemTotal(int) { return 1; }
inline void vglSwapBuffers(GLboolean) { ++test_gl::swaps; }
inline void glReadBuffer(GLenum buffer) { test_gl::read_buffer = buffer; }
inline void glReadPixels(GLint, GLint, GLsizei width, GLsizei height, GLenum,
                          GLenum, void* data) {
  std::fill_n(static_cast<std::uint8_t*>(data), std::size_t(width) * height * 4, 0x6b);
}
inline void glDrawElements(GLenum, GLsizei, GLenum, const void*) { ++test_gl::draws; }
inline GLuint glCreateShader(GLenum) { return test_gl::next_name++; }
inline GLuint glCreateProgram() { return test_gl::next_name++; }
inline void glGetShaderiv(GLuint, GLenum, GLint* status) { *status = 1; }
inline void glGetProgramiv(GLuint, GLenum, GLint* status) { *status = 1; }
inline GLint glGetUniformLocation(GLuint, const char*) { return 0; }

// Unobserved GL state calls keep their real signatures; resource calls above
// model ownership and uploaded bytes without allocating copy-sized GPU storage.
#define TEST_GL_VOID(name, signature) inline void name signature {}
TEST_GL_VOID(vglWaitVblankStart, (GLboolean))
TEST_GL_VOID(glViewport, (GLint, GLint, GLsizei, GLsizei))
TEST_GL_VOID(glUseProgram, (GLuint))
TEST_GL_VOID(glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const float*))
TEST_GL_VOID(glUniform4fv, (GLint, GLsizei, const float*))
TEST_GL_VOID(glUniform2fv, (GLint, GLsizei, const float*))
TEST_GL_VOID(glUniform1i, (GLint, GLint))
TEST_GL_VOID(glEnable, (GLenum))
TEST_GL_VOID(glDisable, (GLenum))
TEST_GL_VOID(glDepthFunc, (GLenum))
TEST_GL_VOID(glDepthRangef, (float, float))
TEST_GL_VOID(glDepthMask, (GLboolean))
TEST_GL_VOID(glBlendEquation, (GLenum))
TEST_GL_VOID(glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))
TEST_GL_VOID(glCullFace, (GLenum))
TEST_GL_VOID(glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))
TEST_GL_VOID(glFrontFace, (GLenum))
TEST_GL_VOID(glBindBuffer, (GLenum, GLuint))
TEST_GL_VOID(glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
TEST_GL_VOID(glEnableVertexAttribArray, (GLuint))
TEST_GL_VOID(glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
TEST_GL_VOID(glFinish, ())
TEST_GL_VOID(glTexParameteri, (GLenum, GLenum, GLint))
TEST_GL_VOID(glShaderSource, (GLuint, GLsizei, const char* const*, const GLint*))
TEST_GL_VOID(glCompileShader, (GLuint))
TEST_GL_VOID(glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*))
TEST_GL_VOID(glDeleteShader, (GLuint))
TEST_GL_VOID(glAttachShader, (GLuint, GLuint))
TEST_GL_VOID(glBindAttribLocation, (GLuint, GLuint, const char*))
TEST_GL_VOID(glLinkProgram, (GLuint))
TEST_GL_VOID(glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*))
TEST_GL_VOID(glDeleteProgram, (GLuint))
#undef TEST_GL_VOID
