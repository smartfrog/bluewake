// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace gxruntime::gxcore { struct DrawPlan; }
namespace gx_vita { struct TranslatedDraw; }

namespace gx_vita::census {

inline constexpr std::uint32_t kMaxPresentedFrames = 1024;

struct DrawInfo {
  std::uint32_t required_mask = 0;
  std::uint32_t logo_mask = 0;
  std::uint32_t decode_failed_mask = 0;
  bool is_quad = false;
  bool fullscreen = false;
};

struct Counts {
  std::uint32_t textured_quad = 0;
  std::uint32_t untextured_quad = 0;
  std::uint32_t other = 0;
  std::uint32_t fullscreen = 0;
  std::uint32_t no_valid_texture = 0;
  std::uint32_t incomplete_texture = 0;
  std::uint32_t decode_failed = 0;
  std::uint32_t bind_valid = 0;
  std::uint32_t bind_skipped = 0;
  std::uint32_t required_skipped = 0;
  std::uint32_t bind_failed = 0;
  std::uint32_t upload_failed = 0;
  std::uint32_t logo = 0;
  std::uint32_t logo_valid = 0;
  std::uint32_t logo_skipped = 0;
  std::uint32_t refused = 0;
};

enum class BindStatus { Valid, Skipped, BindFailed, UploadFailed };

DrawInfo describe(const gxruntime::gxcore::DrawPlan& plan,
                  const TranslatedDraw& draw);
void count_bind(Counts& counts, BindStatus status);
void count_draw(Counts& counts, const DrawInfo& info,
                std::uint32_t valid_mask, std::uint32_t skipped_mask);

} // namespace gx_vita::census
