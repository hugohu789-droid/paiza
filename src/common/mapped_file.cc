// Copyright 2026 Paiza. All rights reserved.

#include "common/mapped_file.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/string_utils.h"
#endif

namespace paiza {

MappedFile::~MappedFile() { Close(); }

#ifdef _WIN32

bool MappedFile::Open(const std::wstring& path) {
  Close();
  HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      static_cast<uint64_t>(size.QuadPart) > SIZE_MAX) {
    ::CloseHandle(file);
    return false;
  }
  HANDLE mapping =
      ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) {
    ::CloseHandle(file);
    return false;
  }
  const void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (view == nullptr) {
    ::CloseHandle(mapping);
    ::CloseHandle(file);
    return false;
  }
  data_ = static_cast<const uint8_t*>(view);
  size_ = static_cast<size_t>(size.QuadPart);
  file_handle_ = file;
  mapping_handle_ = mapping;
  return true;
}

void MappedFile::Close() {
  if (data_ != nullptr) ::UnmapViewOfFile(data_);
  if (mapping_handle_ != nullptr) ::CloseHandle(mapping_handle_);
  if (file_handle_ != nullptr) ::CloseHandle(file_handle_);
  data_ = nullptr;
  size_ = 0;
  file_handle_ = nullptr;
  mapping_handle_ = nullptr;
}

#else  // POSIX

bool MappedFile::Open(const std::wstring& path) {
  Close();
  const std::string utf8 = WideToUtf8(path);
  const int fd = ::open(utf8.c_str(), O_RDONLY);
  if (fd < 0) return false;
  struct stat st = {};
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    return false;
  }
  void* view = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ,
                      MAP_PRIVATE, fd, 0);
  if (view == MAP_FAILED) {
    ::close(fd);
    return false;
  }
  data_ = static_cast<const uint8_t*>(view);
  size_ = static_cast<size_t>(st.st_size);
  fd_ = fd;
  return true;
}

void MappedFile::Close() {
  if (data_ != nullptr) {
    ::munmap(const_cast<uint8_t*>(data_), size_);
    ::close(fd_);
  }
  data_ = nullptr;
  size_ = 0;
  fd_ = -1;
}

#endif

}  // namespace paiza
