// Copyright 2026 Paiza. All rights reserved.

#include "core/scorer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace paiza {
namespace {

// Trims ASCII whitespace from both ends.
std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    --e;
  return s.substr(b, e - b);
}

}  // namespace

void Scorer::LoadWeights(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return;  // Keep defaults.
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    const size_t eq = trimmed.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(trimmed.substr(0, eq));
    const std::string value = Trim(trimmed.substr(eq + 1));
    if (value.empty()) continue;
    const double v = std::atof(value.c_str());
    if (key == "w_edit") weights_.w_edit = v;
    else if (key == "w_bigram") weights_.w_bigram = v;
    else if (key == "w_sys") weights_.w_sys = v;
    else if (key == "w_user") weights_.w_user = v;
    else if (key == "user_halflife_days") weights_.user_halflife_days = v;
  }
}

double Scorer::Score(const ScoreSignals& signals, int64_t now_unix) const {
  double user_freq = static_cast<double>(signals.user_frequency);
  if (user_freq > 0.0 && weights_.user_halflife_days > 0.0 &&
      signals.last_used_unix > 0 && now_unix > signals.last_used_unix) {
    const double age_days =
        static_cast<double>(now_unix - signals.last_used_unix) / 86400.0;
    user_freq *= std::pow(0.5, age_days / weights_.user_halflife_days);
  }
  return weights_.w_edit * (-static_cast<double>(signals.edit_cost)) +
         weights_.w_bigram * (signals.bigram_hit ? 1.0 : 0.0) +
         weights_.w_sys * std::log1p(static_cast<double>(signals.system_frequency)) +
         weights_.w_user * std::log1p(user_freq);
}

int32_t Scorer::ScoreToInt(const ScoreSignals& signals,
                           int64_t now_unix) const {
  // Scale to preserve fractional ordering inside Candidate::score (int32).
  const double scaled = Score(signals, now_unix) * 1000.0;
  const double clamped =
      std::clamp(scaled, static_cast<double>(std::numeric_limits<int32_t>::min()),
                 static_cast<double>(std::numeric_limits<int32_t>::max()));
  return static_cast<int32_t>(std::lround(clamped));
}

}  // namespace paiza
