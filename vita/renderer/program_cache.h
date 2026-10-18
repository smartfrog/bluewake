// SPDX-License-Identifier: GPL-3.0-or-later
// Persisted compiled-program cache for the Vita renderer (POC, off by default).
//
// vitaGL compiles generated Cg to a GXM binary at first use. The reference
// build ships its compiled shader pack, so a new material never pays a cold
// compile. This cache keeps that idea local to the player's device: after a
// successful compile the backend dumps the vitaGL program binary
// (glGetProgramBinary) and a later run restores it (glProgramBinary) instead
// of compiling again.
//
// Never shipped and never game data: entries are GXP shader binaries generated
// from the renderer's own Cg, stored under the player's data directory. They
// can be deleted at any time.
//
// Bounded and fail-safe: a missing, truncated, corrupt, oversized or
// version-mismatched entry is a miss, and the caller compiles as if the cache
// did not exist. Store failures are reported, never fatal.
//
// Key: gxcore ShaderKey hash (program_key) + FNV-1a over both Cg sources + the
// format and codegen versions. Equal keys mean byte-identical generated
// shaders and the same bound-attribute interface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gx_vita {

// Bump when the on-disk layout or file naming changes.
inline constexpr std::uint32_t kProgramCacheFormatVersion = 1;
// Bump when generate_cg output, the attribute/sampler interface, or the
// pinned vitaGL/ShaccCg binary contract changes; old entries then miss.
inline constexpr std::uint32_t kProgramCacheCodegenVersion = 1;

inline constexpr std::size_t kProgramCacheDefaultMaxBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kProgramCacheDefaultMaxEntryBytes = 1024u * 1024u;

struct ProgramCacheKey {
  std::uint64_t program_key = 0;
  std::uint64_t source_hash = 0;
  std::uint32_t format_version = kProgramCacheFormatVersion;
  std::uint32_t codegen_version = kProgramCacheCodegenVersion;
};

// FNV-1a over the two sources, length-prefixed so a vertex/fragment split
// cannot collide with a different split of the same bytes.
ProgramCacheKey make_program_cache_key(std::uint64_t program_key,
                                       const std::string& vertex,
                                       const std::string& fragment);

struct ProgramCacheStats {
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;    // no usable entry: absent or rejected
  std::uint64_t rejected = 0;  // entry existed but failed validation (subset of misses)
  std::uint64_t stores = 0;
  std::uint64_t store_failures = 0;
  std::uint64_t evictions = 0;
};

class ProgramCache {
 public:
  ProgramCache(std::string directory, std::size_t max_bytes,
               std::size_t max_entry_bytes = kProgramCacheDefaultMaxEntryBytes);

  // Reads and fully validates an entry. On failure `blob` is left untouched
  // and the caller must compile. A rejected entry is removed when possible so
  // it cannot fail again on every boot.
  bool load(const ProgramCacheKey& key, std::vector<std::uint8_t>* blob);

  // Writes atomically (temp file then rename), evicting the oldest entries
  // until the store fits max_bytes. Returns false on any I/O or bound failure.
  bool store(const ProgramCacheKey& key, const std::uint8_t* blob,
             std::size_t size);

  const ProgramCacheStats& stats() const { return stats_; }
  void reset_stats() { stats_ = ProgramCacheStats{}; }

  const std::string& directory() const { return directory_; }
  std::size_t max_bytes() const { return max_bytes_; }

 private:
  std::string directory_;
  std::size_t max_bytes_;
  std::size_t max_entry_bytes_;
  ProgramCacheStats stats_;
};

} // namespace gx_vita
