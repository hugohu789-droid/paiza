// Copyright 2026 Paiza. All rights reserved.

#include "core/input_session.h"

#include <algorithm>
#include <utility>

namespace paiza {

InputSession::InputSession(CandidateEngine* engine,
                           KeyMappingStore* mappings)
    : InputSession(engine, mappings, Config{}) {}

InputSession::InputSession(CandidateEngine* engine,
                           KeyMappingStore* mappings, Config config)
    : engine_(engine), mappings_(mappings), config_(config) {
  if (config_.page_size == 0) config_.page_size = 1;
  if (config_.max_candidates == 0) config_.max_candidates = 1;
}

size_t InputSession::page_count() const {
  if (candidates_.empty()) return 0;
  return (candidates_.size() + config_.page_size - 1) / config_.page_size;
}

void InputSession::set_page_size(size_t page_size) {
  config_.page_size = std::clamp<size_t>(page_size, 1, 9);
  page_ = selected_ / config_.page_size;
}

void InputSession::PageRange(size_t* first, size_t* last) const {
  *first = page_ * config_.page_size;
  *last = std::min(*first + config_.page_size, candidates_.size());
}

void InputSession::Reset() {
  latin_buffer_.clear();
  preedit_.clear();
  candidates_.clear();
  selected_ = 0;
  page_ = 0;
}

InputSession::KeyResult InputSession::Commit(std::wstring text) {
  Reset();
  KeyResult result;
  result.handled = true;
  result.commit = std::move(text);
  return result;
}

void InputSession::RefreshCandidates() {
  preedit_ = mappings_->Get().Convert(latin_buffer_);
  candidates_ = engine_->Query(latin_buffer_, config_.max_candidates);
  if (candidates_.empty() && !preedit_.empty()) {
    // The raw conversion is always offered so typing works even without
    // dictionary coverage (mirrors the Windows EngineProxy fallback).
    Candidate fallback;
    fallback.text = preedit_;
    candidates_.push_back(std::move(fallback));
  }
  selected_ = 0;
  page_ = 0;
}

InputSession::KeyResult InputSession::HandleLetter(char letter) {
  const bool lower = letter >= 'a' && letter <= 'z';
  const bool upper = letter >= 'A' && letter <= 'Z';
  if (!lower && !upper) return {};
  latin_buffer_.push_back(letter);
  RefreshCandidates();
  return {.handled = true, .commit = std::nullopt};
}

InputSession::KeyResult InputSession::HandleDigit(int digit) {
  if (!composing() || digit < 1 || digit > 9) return {};
  size_t first = 0;
  size_t last = 0;
  PageRange(&first, &last);
  const size_t index = first + static_cast<size_t>(digit - 1);
  if (index >= last) return {};  // Beyond the page: key passes through.
  return Commit(candidates_[index].text);
}

InputSession::KeyResult InputSession::HandleSpace() {
  if (!composing()) return {};
  if (selected_ < candidates_.size()) {
    return Commit(candidates_[selected_].text);
  }
  return Commit(preedit_);
}

InputSession::KeyResult InputSession::HandleEnter() {
  if (!composing()) return {};
  return Commit(preedit_);
}

InputSession::KeyResult InputSession::HandleEscape() {
  if (!composing()) return {};
  Reset();
  return {.handled = true, .commit = std::nullopt};
}

InputSession::KeyResult InputSession::HandleBackspace() {
  if (!composing()) return {};
  latin_buffer_.pop_back();
  if (latin_buffer_.empty()) {
    Reset();
  } else {
    RefreshCandidates();
  }
  return {.handled = true, .commit = std::nullopt};
}

InputSession::KeyResult InputSession::HandleOtherKey() {
  if (!composing()) return {};
  KeyResult result = Commit(preedit_);
  result.handled = false;  // The key still goes to the application.
  return result;
}

void InputSession::MoveSelection(int delta) {
  if (candidates_.empty()) return;
  const size_t n = candidates_.size();
  if (delta >= 0) {
    selected_ = (selected_ + 1) % n;
  } else {
    selected_ = (selected_ + n - 1) % n;
  }
  page_ = selected_ / config_.page_size;
}

bool InputSession::PageForward() {
  if (page_ + 1 >= page_count()) return false;
  ++page_;
  selected_ = page_ * config_.page_size;
  return true;
}

bool InputSession::PageBackward() {
  if (page_ == 0) return false;
  --page_;
  selected_ = page_ * config_.page_size;
  return true;
}

InputSession::KeyResult InputSession::CommitCandidate(size_t index) {
  if (index >= candidates_.size()) return {};
  return Commit(candidates_[index].text);
}

}  // namespace paiza
