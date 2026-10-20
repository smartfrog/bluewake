// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace gxruntime::gxcore { struct DrawPlan; }
namespace gx_vita { struct TranslatedDraw; }

namespace gx_vita::census {

inline constexpr std::uint32_t kMaxPresentedFrames = 1024;
inline constexpr std::uint32_t kMaxEntries = 8;

struct TextureIdentity {
  std::uint32_t address = 0, width = 0, height = 0, format = 0;
  bool operator==(const TextureIdentity&) const = default;
};

struct DrawEntry {
  TextureIdentity texture;
  std::uint32_t slot = 0;
  bool four_vertices = false, fullscreen = false, depth_test = false, depth_write = false;
  bool operator==(const DrawEntry&) const = default;
};

struct DrawInfo {
  std::uint32_t required_mask = 0;
  std::uint32_t decode_failed_mask = 0;
  bool is_quad = false;
  bool fullscreen = false;
  std::array<DrawEntry, 8> entries{};
};

struct InventoryEntry {
  DrawEntry draw;
  std::uint32_t count = 0;
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
  std::uint32_t refused = 0;
  std::uint32_t textured = 0;
  std::uint32_t entry_count = 0;
  std::uint32_t omitted = 0;
  std::array<InventoryEntry, kMaxEntries> entries{};
};

enum class BindStatus { Valid, Skipped, BindFailed, UploadFailed };

DrawInfo describe(const gxruntime::gxcore::DrawPlan& plan,
                  const TranslatedDraw& draw);
void count_bind(Counts& counts, BindStatus status);
void count_draw(Counts& counts, const DrawInfo& info,
                std::uint32_t valid_mask, std::uint32_t skipped_mask);
std::string format_frame(const Counts& counts, std::uint32_t present, bool partial);

} // namespace gx_vita::census
