// Copyright 2026 Paiza. All rights reserved.
//
// Read-only memory-mapped file, portable across Windows / macOS / Linux.
// The mapping is released in the destructor; the view stays valid for the
// lifetime of the object. Zero-copy: the OS pages data in on demand, so a
// large lexicon costs only the pages actually touched.

#ifndef PAIZA_COMMON_MAPPED_FILE_H_
#define PAIZA_COMMON_MAPPED_FILE_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace paiza {

class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  // Maps the whole file read-only. Returns false (and stays unmapped) if the
  // file is missing, empty, or the mapping fails.
  bool Open(const std::wstring& path);
  void Close();

  bool is_open() const { return data_ != nullptr; }
  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
#ifdef _WIN32
  void* file_handle_ = nullptr;
  void* mapping_handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

}  // namespace paiza

#endif  // PAIZA_COMMON_MAPPED_FILE_H_
