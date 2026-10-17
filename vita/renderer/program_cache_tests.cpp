// SPDX-License-Identifier: GPL-3.0-or-later
// Persisted program cache tests: key, hit/miss, corruption, version
// invalidation, bounds. Explicit checks, no assert.

#include "program_cache.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

using Bytes = std::vector<std::uint8_t>;

std::filesystem::path make_temp_directory(const char* name) {
  const auto path = std::filesystem::temp_directory_path() /
                    (std::string("bw-program-cache-") + name + "-" +
                     std::to_string(::getpid()));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::vector<std::filesystem::path> entries(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> found;
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    if (entry.path().extension() == ".bwsc")
      found.push_back(entry.path());
  return found;
}

std::uint64_t directory_bytes(const std::filesystem::path& dir) {
  std::uint64_t total = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    if (entry.is_regular_file())
      total += entry.file_size();
  return total;
}

Bytes read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void put_u64(Bytes& bytes, std::size_t offset, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

Bytes payload(std::uint8_t seed, std::size_t size) {
  Bytes bytes(size);
  for (std::size_t i = 0; i < size; ++i)
    bytes[i] = static_cast<std::uint8_t>(seed + i);
  return bytes;
}

gx_vita::ProgramCacheKey key(std::uint64_t id, const char* vertex,
                             const char* fragment) {
  return gx_vita::make_program_cache_key(id, vertex, fragment);
}

int key_is_deterministic_and_distinguishes_inputs() {
  const auto first = key(7, "vertex-a", "fragment-a");
  const auto same = key(7, "vertex-a", "fragment-a");
  CHECK(first.program_key == 7);
  CHECK(first.source_hash == same.source_hash);
  CHECK(first.format_version == gx_vita::kProgramCacheFormatVersion);
  CHECK(first.codegen_version == gx_vita::kProgramCacheCodegenVersion);
  CHECK(first.source_hash != key(7, "vertex-a", "fragment-b").source_hash);
  CHECK(first.source_hash != key(7, "vertex-b", "fragment-a").source_hash);
  CHECK(key(7, "ab", "c").source_hash != key(7, "a", "bc").source_hash);
  CHECK(key(8, "vertex-a", "fragment-a").source_hash == first.source_hash);
  std::printf("key: deterministic and source-sensitive\n");
  return 0;
}

int store_then_load_hits() {
  const auto dir = make_temp_directory("hit");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x11, 512);
  CHECK(cache.store(key(1, "v", "f"), bytes.data(), bytes.size()));
  CHECK(cache.stats().stores == 1);
  Bytes loaded;
  CHECK(cache.load(key(1, "v", "f"), &loaded));
  CHECK(loaded == bytes);
  CHECK(cache.stats().hits == 1);
  CHECK(cache.stats().misses == 0);
  CHECK(entries(dir).size() == 1);
  std::printf("hit: exact round trip\n");
  return 0;
}

int absent_entry_misses() {
  const auto dir = make_temp_directory("miss");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  Bytes loaded = payload(0x22, 4);
  CHECK(!cache.load(key(1, "v", "f"), &loaded));
  CHECK(loaded.size() == 4); // untouched by a miss
  CHECK(cache.stats().misses == 1);
  CHECK(cache.stats().rejected == 0);
  std::printf("miss: absent entry\n");
  return 0;
}

int corrupt_payload_is_rejected_and_removed() {
  const auto dir = make_temp_directory("corrupt");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x33, 256);
  CHECK(cache.store(key(2, "v", "f"), bytes.data(), bytes.size()));
  const auto files = entries(dir);
  CHECK(files.size() == 1);
  auto stored = read_file(files.front());
  stored.back() ^= 0xff;
  write_file(files.front(), stored);

  Bytes loaded;
  CHECK(!cache.load(key(2, "v", "f"), &loaded));
  CHECK(loaded.empty());
  CHECK(cache.stats().rejected == 1);
  CHECK(entries(dir).empty()); // corrupt entry removed
  std::printf("corrupt payload: rejected and removed\n");
  return 0;
}

int truncated_entry_is_rejected() {
  const auto dir = make_temp_directory("truncated");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x44, 128);
  CHECK(cache.store(key(3, "v", "f"), bytes.data(), bytes.size()));
  const auto files = entries(dir);
  CHECK(files.size() == 1);
  auto stored = read_file(files.front());
  stored.resize(stored.size() / 2);
  write_file(files.front(), stored);

  Bytes loaded;
  CHECK(!cache.load(key(3, "v", "f"), &loaded));
  CHECK(cache.stats().rejected == 1);
  std::printf("truncated entry: rejected\n");
  return 0;
}

int trailing_bytes_are_rejected() {
  const auto dir = make_temp_directory("trailing");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x45, 64);
  CHECK(cache.store(key(3, "v", "f"), bytes.data(), bytes.size()));
  const auto files = entries(dir);
  CHECK(files.size() == 1);
  auto stored = read_file(files.front());
  stored.push_back(0);
  write_file(files.front(), stored);

  Bytes loaded;
  CHECK(!cache.load(key(3, "v", "f"), &loaded));
  CHECK(cache.stats().rejected == 1);
  std::printf("trailing bytes: rejected\n");
  return 0;
}

int mismatched_header_is_rejected() {
  const auto dir = make_temp_directory("mismatch");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x55, 128);
  CHECK(cache.store(key(4, "v", "f"), bytes.data(), bytes.size()));

  {
    auto stored = read_file(entries(dir).front());
    put_u64(stored, 12, 99); // program_key
    write_file(entries(dir).front(), stored);
    Bytes loaded;
    CHECK(!cache.load(key(4, "v", "f"), &loaded));
    CHECK(cache.stats().rejected == 1);
  }

  CHECK(cache.store(key(4, "v", "f"), bytes.data(), bytes.size()));
  {
    auto stored = read_file(entries(dir).front());
    put_u64(stored, 20, 12345); // source_hash
    write_file(entries(dir).front(), stored);
    Bytes loaded;
    CHECK(!cache.load(key(4, "v", "f"), &loaded));
    CHECK(cache.stats().rejected == 2);
  }
  std::printf("mismatched header: rejected\n");
  return 0;
}

int version_mismatch_is_rejected() {
  const auto dir = make_temp_directory("version");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x66, 128);
  CHECK(cache.store(key(5, "v", "f"), bytes.data(), bytes.size()));

  {
    auto stored = read_file(entries(dir).front());
    put_u32(stored, 4, gx_vita::kProgramCacheFormatVersion + 1);
    write_file(entries(dir).front(), stored);
    Bytes loaded;
    CHECK(!cache.load(key(5, "v", "f"), &loaded));
    CHECK(cache.stats().rejected == 1);
  }

  CHECK(cache.store(key(5, "v", "f"), bytes.data(), bytes.size()));
  {
    auto stored = read_file(entries(dir).front());
    put_u32(stored, 8, gx_vita::kProgramCacheCodegenVersion + 1);
    write_file(entries(dir).front(), stored);
    Bytes loaded;
    CHECK(!cache.load(key(5, "v", "f"), &loaded));
    CHECK(cache.stats().rejected == 2);
  }
  std::printf("version mismatch: rejected\n");
  return 0;
}

int changed_source_misses_old_entry() {
  const auto dir = make_temp_directory("source-change");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0x77, 128);
  CHECK(cache.store(key(6, "old-vertex", "old-fragment"), bytes.data(),
                    bytes.size()));
  Bytes loaded;
  CHECK(!cache.load(key(6, "new-vertex", "old-fragment"), &loaded));
  CHECK(cache.stats().misses == 1);
  std::printf("changed source: old entry not reused\n");
  return 0;
}

int oversized_entry_is_rejected() {
  const auto dir = make_temp_directory("oversized");
  gx_vita::ProgramCache cache(dir.string(), 4096, 64);
  const auto bytes = payload(0x88, 65);
  CHECK(!cache.store(key(7, "v", "f"), bytes.data(), bytes.size()));
  CHECK(cache.stats().store_failures == 1);
  CHECK(entries(dir).empty());
  std::printf("oversized entry: rejected\n");
  return 0;
}

int bounded_bytes_evict_oldest() {
  const auto dir = make_temp_directory("bounded");
  const std::size_t entry_bytes = 40 + 100;
  const std::size_t max_bytes = entry_bytes * 2;
  gx_vita::ProgramCache cache(dir.string(), max_bytes);
  const auto bytes = payload(0x99, 100);
  for (std::uint64_t id = 1; id <= 4; ++id)
    CHECK(cache.store(key(id, "v", "f"), bytes.data(), bytes.size()));
  CHECK(directory_bytes(dir) <= max_bytes);
  CHECK(cache.stats().evictions >= 1);
  CHECK(entries(dir).size() == 2);

  Bytes loaded;
  CHECK(cache.load(key(4, "v", "f"), &loaded));
  CHECK(loaded == bytes);
  CHECK(!cache.load(key(1, "v", "f"), &loaded));
  std::printf("bounds: %zu bytes kept, %llu evicted\n",
              static_cast<std::size_t>(directory_bytes(dir)),
              static_cast<unsigned long long>(cache.stats().evictions));
  return 0;
}

int store_leaves_no_temp_file() {
  const auto dir = make_temp_directory("atomic");
  gx_vita::ProgramCache cache(dir.string(), 64 * 1024);
  const auto bytes = payload(0xaa, 64);
  {
    std::ofstream stale(dir / "crashed.bwsc.tmp", std::ios::binary);
    stale << "partial";
  }
  CHECK(cache.store(key(8, "v", "f"), bytes.data(), bytes.size()));
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    CHECK(entry.path().extension() != ".tmp");
  std::printf("store: no temp file left\n");
  return 0;
}

} // namespace

int main() {
  if (key_is_deterministic_and_distinguishes_inputs()) return 1;
  if (store_then_load_hits()) return 1;
  if (absent_entry_misses()) return 1;
  if (corrupt_payload_is_rejected_and_removed()) return 1;
  if (truncated_entry_is_rejected()) return 1;
  if (trailing_bytes_are_rejected()) return 1;
  if (mismatched_header_is_rejected()) return 1;
  if (version_mismatch_is_rejected()) return 1;
  if (changed_source_misses_old_entry()) return 1;
  if (oversized_entry_is_rejected()) return 1;
  if (bounded_bytes_evict_oldest()) return 1;
  if (store_leaves_no_temp_file()) return 1;
  std::printf("program_cache: all checks ok\n");
  return 0;
}
