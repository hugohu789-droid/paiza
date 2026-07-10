// Copyright 2026 Paiza. All rights reserved.
//
// Platform-neutral composition session: the complete key policy of the
// IME (letters compose, digits commit, Space/Enter/Escape semantics,
// candidate paging/selection) as a testable state machine.
//
// Platform front-ends are thin translators around this class:
//   * macOS  (InputMethodKit): NSEvent -> HandleXxx(), then mirror
//     preedit() into setMarkedText: and candidates() into the panel.
//   * Linux  (IBus/Fcitx5):    key events -> HandleXxx(), same idea.
//   * Windows (TSF) currently implements the same policy inside
//     TextService (predates this class); it is a candidate for migration.
//
// The behaviour intentionally mirrors the Windows key policy 1:1 — this
// class plus its unit tests are the cross-platform feature-parity
// contract (see tests/input_session_test.cc).

#ifndef PAIZA_CORE_INPUT_SESSION_H_
#define PAIZA_CORE_INPUT_SESSION_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/candidate.h"
#include "core/candidate_engine.h"
#include "core/key_mapping_store.h"
#include "core/user_dictionary.h"

namespace paiza {

class InputSession {
 public:
  struct Config {
    size_t page_size = 7;       // Digit keys 1..9 select on this page.
    size_t max_candidates = 27; // Queried from the engine per keystroke.
  };

  // Outcome of a key event. The host applies it as:
  //   1. if commit -> insert *commit into the client document
  //   2. if composing() -> show preedit() as marked text + candidates()
  //      in the panel; else clear marked text and hide the panel
  //   3. handled == false -> let the key go through to the application
  struct KeyResult {
    bool handled = false;
    std::optional<std::wstring> commit;
  };

  // `engine` and `mappings` must outlive the session. (Two overloads
  // rather than a `Config config = {}` default argument: clang rejects
  // default member initializers used in default arguments inside the
  // enclosing class definition.)
  InputSession(CandidateEngine* engine, KeyMappingStore* mappings);
  InputSession(CandidateEngine* engine, KeyMappingStore* mappings,
               Config config);

  // Key events --------------------------------------------------------
  // True when `c` participates in composition: ASCII letters (upper case
  // = the Shift second letter set) plus any non-digit printable character
  // with an entry in the current key mapping (e.g. the default '-' MVS,
  // '_' NNBSP, '*'/'@'/'#'/'$'/'%' Birga+FVS keys — and whatever the user
  // maps in keymap.txt). Digits never compose: 1-9 select candidates.
  bool IsComposableChar(char c);
  // `letter` is a case-resolved composable character (see above). Starts
  // a composition when none is active; not-composable characters are
  // ignored (handled=false).
  KeyResult HandleLetter(char letter);
  // Digit 1..9: commits that candidate of the current page. Not handled
  // when the digit exceeds the page's candidate count (key passes
  // through, composition unchanged) — mirrors the Windows behaviour.
  KeyResult HandleDigit(int digit);
  KeyResult HandleSpace();      // Commit the highlighted candidate.
  KeyResult HandleEnter();      // Commit the raw conversion.
  KeyResult HandleEscape();     // Cancel, committing nothing.
  KeyResult HandleBackspace();  // Drop last letter; empties -> cancel.
  // Any other key while composing: the raw conversion is committed and
  // handled=false so the host passes the key on to the application.
  KeyResult HandleOtherKey();

  // Candidate navigation ------------------------------------------------
  void MoveSelection(int delta);  // +1 / -1, wraps; page follows.
  bool PageForward();
  bool PageBackward();
  // Mouse click on the candidate panel.
  KeyResult CommitCandidate(size_t index);

  // State for the host --------------------------------------------------
  bool composing() const { return !latin_buffer_.empty(); }
  // The composition string shown as marked text (Mongolian conversion of
  // the typed buffer under the current key mapping).
  const std::wstring& preedit() const { return preedit_; }
  const std::vector<Candidate>& candidates() const { return candidates_; }
  size_t selected_index() const { return selected_; }
  size_t current_page() const { return page_; }
  size_t page_count() const;
  size_t page_size() const { return config_.page_size; }
  // Applies the "candidates per page" setting to a live session (clamped
  // to 1..9, the digit-selectable range); the page follows the selection.
  void set_page_size(size_t page_size);
  // Candidates visible on the current page: [first, last).
  void PageRange(size_t* first, size_t* last) const;

  // Clears the composition without committing (host lost focus etc.).
  void Reset();

  // Optional dynamic user lexicon: every commit records the (typed keys,
  // committed word) pair for frequency boosting and user-coined words
  // (spec §3.2). The dictionary must outlive the session; it is usually
  // also registered as a CandidateSource on the same engine.
  void set_user_dictionary(UserDictionary* dictionary) {
    user_dictionary_ = dictionary;
  }

 private:
  void RefreshCandidates();
  KeyResult Commit(std::wstring text);

  CandidateEngine* engine_;
  KeyMappingStore* mappings_;
  UserDictionary* user_dictionary_ = nullptr;
  Config config_;

  std::string latin_buffer_;
  std::wstring preedit_;
  std::vector<Candidate> candidates_;
  size_t selected_ = 0;
  size_t page_ = 0;
};

}  // namespace paiza

#endif  // PAIZA_CORE_INPUT_SESSION_H_
