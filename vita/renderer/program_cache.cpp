// SPDX-License-Identifier: GPL-3.0-or-later
// Persisted compiled-program cache. See program_cache.h.

#include "program_cache.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace gx_vita {

namespace {

constexpr std::uint8_t kMagic[4] = {'B', 'W', 'S', 'C'};
constexpr std::size_t kHeaderBytes = 40;
constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;

std::uint64_t fnv1a(std::uint64_t hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t fnv_u32(std::uint64_t hash, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    hash ^= static_cast<std::uint8_t>(value >> (8 * i));
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t source_hash(const std::string& vertex,
                          const std::string& fragment) {
  std::uint64_t hash = fnv_u32(kFnvOffset, static_cast<std::uint32_t>(vertex.size()));
  hash = fnv1a(hash, vertex.data(), vertex.size());
  hash = fnv_u32(hash, static_cast<std::uint32_t>(fragment.size()));
  return fnv1a(hash, fragment.data(), fragment.size());
}

void put_u32(std::uint8_t* out, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void put_u64(std::uint8_t* out, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t get_u32(const std::uint8_t* in) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
  return value;
}

std::uint64_t get_u64(const std::uint8_t* in) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
  return value;
}

std::string file_name(const ProgramCacheKey& key) {
  char name[96];
  const int written = std::snprintf(
      name, sizeof name, "v%u-%u-%016llx-%016llx.bwsc", key.format_version,
      key.codegen_version, static_cast<unsigned long long>(key.program_key),
      static_cast<unsigned long long>(key.source_hash));
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof name)
    return {};
  return name;
}

std::string join(const std::string& directory, const std::string& name) {
  return directory + "/" + name;
}

bool make_directories(const std::string& path) {
  if (path.empty())
    return false;
  for (std::size_t end = 1; end <= path.size(); ++end) {
    if (end != path.size() && path[end] != '/')
      continue;
    const std::string part = path.substr(0, end);
    if (part.empty())
      continue;
    if (::mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
      return false;
  }
  struct stat status {};
  return ::stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

struct Entry {
  std::string path;
  std::uint64_t bytes = 0;
  std::time_t mtime = 0;
};

bool has_suffix(const std::string& text, const char* suffix) {
  const std::size_t length = std::strlen(suffix);
  return text.size() >= length &&
         text.compare(text.size() - length, length, suffix) == 0;
}

std::vector<Entry> list_entries(const std::string& directory) {
  std::vector<Entry> entries;
  DIR* dir = ::opendir(directory.c_str());
  if (dir == nullptr)
    return entries;
  while (const dirent* item = ::readdir(dir)) {
    const std::string name = item->d_name;
    if (!has_suffix(name, ".bwsc"))
      continue;
    const std::string path = join(directory, name);
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode))
      continue;
    Entry entry;
    entry.path = path;
    entry.bytes = static_cast<std::uint64_t>(status.st_size);
    entry.mtime = status.st_mtime;
    entries.push_back(std::move(entry));
  }
  ::closedir(dir);
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.mtime != b.mtime)
      return a.mtime < b.mtime;
    return a.path < b.path;
  });
  return entries;
}

// A crash between the temporary write and the rename leaves a complete or
// partial .tmp that no load ever reads. Remove it while the cache is already
// scanning the directory, so an interrupted store cannot grow unbounded.
void remove_stale_temporaries(const std::string& directory) {
  DIR* dir = ::opendir(directory.c_str());
  if (dir == nullptr)
    return;
  while (const dirent* item = ::readdir(dir)) {
    const std::string name = item->d_name;
    if (has_suffix(name, ".bwsc.tmp"))
      std::remove(join(directory, name).c_str());
  }
  ::closedir(dir);
}

} // namespace

ProgramCacheKey make_program_cache_key(std::uint64_t program_key,
                                       const std::string& vertex,
                                       const std::string& fragment) {
  ProgramCacheKey key;
  key.program_key = program_key;
  key.source_hash = source_hash(vertex, fragment);
  return key;
}

ProgramCache::ProgramCache(std::string directory, std::size_t max_bytes,
                           std::size_t max_entry_bytes)
    : directory_(std::move(directory)),
      max_bytes_(max_bytes),
      max_entry_bytes_(max_entry_bytes) {
  while (directory_.size() > 1 && directory_.back() == '/')
    directory_.pop_back();
}

bool ProgramCache::load(const ProgramCacheKey& key,
                        std::vector<std::uint8_t>* blob) {
  if (blob == nullptr) {
    ++stats_.misses;
    ++stats_.rejected;
    return false;
  }
  const std::string name = file_name(key);
  if (name.empty()) {
    ++stats_.misses;
    ++stats_.rejected;
    return false;
  }
  const std::string path = join(directory_, name);
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    ++stats_.misses;
    return false;
  }

  std::uint8_t header[kHeaderBytes];
  const bool header_read =
      std::fread(header, 1, kHeaderBytes, file) == kHeaderBytes;
  bool ok = header_read && std::memcmp(header, kMagic, sizeof kMagic) == 0 &&
            get_u32(header + 4) == key.format_version &&
            get_u32(header + 8) == key.codegen_version &&
            get_u64(header + 12) == key.program_key &&
            get_u64(header + 20) == key.source_hash;
  const std::uint32_t payload_size = ok ? get_u32(header + 28) : 0;
  const std::uint64_t payload_hash = ok ? get_u64(header + 32) : 0;
  ok = ok && payload_size > 0 && payload_size <= max_entry_bytes_ &&
       payload_size <= max_bytes_;

  long file_size = -1;
  if (ok && std::fseek(file, 0, SEEK_END) == 0)
    file_size = std::ftell(file);
  ok = ok && file_size == static_cast<long>(kHeaderBytes + payload_size);

  std::vector<std::uint8_t> bytes;
  if (ok) {
    ok = std::fseek(file, static_cast<long>(kHeaderBytes), SEEK_SET) == 0;
    bytes.resize(payload_size);
    ok = ok && std::fread(bytes.data(), 1, payload_size, file) == payload_size &&
         fnv1a(kFnvOffset, bytes.data(), payload_size) == payload_hash;
  }
  std::fclose(file);

  if (!ok) {
    ++stats_.misses;
    ++stats_.rejected;
    std::remove(path.c_str()); // best effort; a read-only cache still misses safely
    return false;
  }
  *blob = std::move(bytes);
  ++stats_.hits;
  return true;
}

bool ProgramCache::store(const ProgramCacheKey& key, const std::uint8_t* blob,
                         std::size_t size) {
  if (blob == nullptr || size == 0 || size > max_entry_bytes_ ||
      size > max_bytes_ || directory_.empty()) {
    ++stats_.store_failures;
    return false;
  }
  const std::string name = file_name(key);
  if (name.empty() || !make_directories(directory_)) {
    ++stats_.store_failures;
    return false;
  }
  const std::string target = join(directory_, name);
  remove_stale_temporaries(directory_);

  std::uint64_t total = 0;
  std::vector<Entry> candidates;
  for (const Entry& entry : list_entries(directory_)) {
    if (entry.path == target)
      continue;
    total += entry.bytes;
    candidates.push_back(entry);
  }
  for (const Entry& entry : candidates) {
    if (total + size <= max_bytes_)
      break;
    if (std::remove(entry.path.c_str()) == 0) {
      total -= entry.bytes;
      ++stats_.evictions;
    }
  }
  if (total + size > max_bytes_) {
    ++stats_.store_failures;
    return false;
  }

  std::uint8_t header[kHeaderBytes];
  std::memcpy(header, kMagic, sizeof kMagic);
  put_u32(header + 4, key.format_version);
  put_u32(header + 8, key.codegen_version);
  put_u64(header + 12, key.program_key);
  put_u64(header + 20, key.source_hash);
  put_u32(header + 28, static_cast<std::uint32_t>(size));
  put_u64(header + 32, fnv1a(kFnvOffset, blob, size));

  const std::string temporary = target + ".tmp";
  std::FILE* file = std::fopen(temporary.c_str(), "wb");
  if (file == nullptr) {
    ++stats_.store_failures;
    return false;
  }
  bool ok = std::fwrite(header, 1, kHeaderBytes, file) == kHeaderBytes &&
            std::fwrite(blob, 1, size, file) == size;
  ok = std::fflush(file) == 0 && ok;
  ok = std::fclose(file) == 0 && ok;
  if (ok && std::rename(temporary.c_str(), target.c_str()) != 0) {
    // Some device filesystems refuse to replace an existing name. The temp
    // file is complete, so a remove-and-rename fallback still stores a valid
    // entry (or reports failure and leaves the caller compiling).
    std::remove(target.c_str());
    ok = std::rename(temporary.c_str(), target.c_str()) == 0;
  }
  if (!ok) {
    std::remove(temporary.c_str());
    ++stats_.store_failures;
    return false;
  }
  ++stats_.stores;
  return true;
}

} // namespace gx_vita
