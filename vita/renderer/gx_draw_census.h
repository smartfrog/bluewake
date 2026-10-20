// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace gxruntime::gxcore { struct DrawPlan; }
namespace gx_vita { struct TranslatedDraw; struct TextureUpload; }

namespace gx_vita::census {

inline constexpr std::uint32_t kMaxPresentedFrames = 1024;
inline constexpr std::uint32_t kFirstLogoStateFrame = 300;

struct AlphaStats {
  bool valid = false;
  std::uint32_t minimum = 0;
  std::uint32_t maximum = 0;
  std::uint32_t nonzero = 0;
  std::uint32_t texels = 0;
  std::uint64_t sum = 0;
  double mean() const { return texels ? static_cast<double>(sum) / texels : 0; }
};

struct TextureIdentity {
  std::uint32_t address = 0, format = 0, width = 0, height = 0;
  bool operator==(const TextureIdentity&) const = default;
};

struct AlphaSample {
  TextureIdentity identity;
  AlphaStats stats;
  std::uint32_t present = 0;
};

class AlphaCache {
public:
  const AlphaSample* observe(TextureIdentity identity, const TextureUpload& texture,
                             std::uint32_t present);
private:
  std::array<AlphaSample, 8> entries_{};
  std::uint32_t size_ = 0;
};

struct GeometryStats {
  bool valid = false;
  bool fullscreen = false;
  float minimum[3]{}, maximum[3]{}, w_minimum = 0, w_maximum = 0;
  float twice_area[2]{};
};

AlphaStats alpha_stats(const TextureUpload& texture);
GeometryStats geometry_stats(const TranslatedDraw& draw);

struct DrawInfo {
  std::uint32_t required_mask = 0;
  std::uint32_t logo_mask = 0;
  std::uint32_t decode_failed_mask = 0;
  bool is_quad = false;
  bool fullscreen = false;
  TextureIdentity logo_texture;
  std::uint32_t logo_texmap = 0;
  std::uint32_t alpha_stage_count = 0;
  std::uint32_t alpha_stages[16]{};
  std::uint8_t alpha_test[3]{};
  float raster_alpha[2][4]{};
  GeometryStats geometry;
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
