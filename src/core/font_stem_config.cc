// Copyright 2026 Paiza. All rights reserved.

#include "core/font_stem_config.h"

#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "common/paths.h"
#include "common/string_utils.h"

namespace paiza {
namespace {

struct BuiltinEntry {
  const wchar_t* family;
  float ratio;
};

// Calibrated stem ratios shipped with the IME. Measured with the debug
// spine overlay (CandidateWindow::SetDebugSpine) against real candidates.
constexpr BuiltinEntry kBuiltinEntries[] = {
    {L"Noto Sans Mongolian", 0.672f},
};

constexpr std::chrono::milliseconds kCheckInterval{500};

std::wstring ToLower(std::wstring s) {
  for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
  return s;
}

// Shared, hot-reloading table of family -> ratio.
class StemConfigStore {
 public:
  StemConfigStore() : path_(FontStemConfigFilePath()) { Reload(); }

  std::optional<float> Get(const std::wstring& family) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_check_) {
      next_check_ = now + kCheckInterval;
      ReloadIfChanged();
    }
    const auto it = table_.find(ToLower(family));
    if (it == table_.end()) return std::nullopt;
    return it->second;
  }

 private:
  void ReloadIfChanged() {
    if (path_.empty()) return;
    // Content-based change detection, same rationale as KeyMappingStore:
    // mtime misses writes landing within the filesystem timestamp
    // granularity. The file is tiny; reading it on the throttled poll is
    // negligible.
    std::string bytes;
    bool exists = false;
    {
      std::ifstream file(std::filesystem::path(path_), std::ios::binary);
      if (file.is_open()) {
        exists = true;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        bytes = buffer.str();
      }
    }
    if (exists == last_exists_ && bytes == last_content_) return;
    last_exists_ = exists;
    last_content_ = std::move(bytes);
    Reload();
  }

  void Reload() {
    table_.clear();
    for (const BuiltinEntry& entry : kBuiltinEntries) {
      table_[ToLower(entry.family)] = entry.ratio;
    }
    if (!last_exists_) return;
    std::istringstream file(last_content_);
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      // "<family> <TAB or spaces> <ratio>": the ratio is the last token,
      // everything before it (trimmed) is the family name (may contain
      // spaces).
      const size_t split = line.find_last_of(" \t");
      if (split == std::string::npos || split == 0) continue;
      const std::string ratio_token = line.substr(split + 1);
      std::string family_token = line.substr(0, split);
      while (!family_token.empty() &&
             (family_token.back() == ' ' || family_token.back() == '\t')) {
        family_token.pop_back();
      }
      char* end = nullptr;
      const float ratio =
          static_cast<float>(std::strtod(ratio_token.c_str(), &end));
      if (end == nullptr || *end != '\0' || ratio <= 0.05f ||
          ratio >= 0.95f || family_token.empty()) {
        continue;
      }
      table_[ToLower(Utf8ToWide(family_token))] = ratio;
    }
  }

  std::mutex mutex_;
  std::wstring path_;
  std::map<std::wstring, float> table_;
  // Snapshot of the file as of the last poll (existence + raw bytes).
  bool last_exists_ = false;
  std::string last_content_;
  std::chrono::steady_clock::time_point next_check_{};
};

StemConfigStore& Store() {
  static StemConfigStore store;
  return store;
}

}  // namespace

std::wstring FontStemConfigFilePath() {
  const std::wstring dir = UserConfigDirectory();
  if (dir.empty()) return {};
  return (std::filesystem::path(dir) / L"fontstems.txt").wstring();
}

void EnsureFontStemConfigFile() {
  const std::wstring path = FontStemConfigFilePath();
  std::error_code ec;
  if (path.empty() || std::filesystem::exists(path, ec)) return;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::ofstream file(std::filesystem::path(path),
                     std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return;
  file << "# Paiza Mongolian IME - vertical stem calibration per font.\n"
       << "# Format: <font family> <TAB> <stem ratio>\n"
       << "# The ratio is the Mongolian stem position from the leading\n"
       << "# edge of a vertical layout, as a fraction of the font size.\n"
       << "# Fonts NOT listed here are rendered without stem centring.\n"
       << "# Saved as UTF-8; changes apply within a second.\n";
  for (const BuiltinEntry& entry : kBuiltinEntries) {
    file << WideToUtf8(entry.family) << "\t" << entry.ratio << "\n";
  }
}

std::optional<float> GetFontStemRatio(const std::wstring& family) {
  return Store().Get(family);
}

}  // namespace paiza
