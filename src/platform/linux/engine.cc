// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "engine.h"

#include <filesystem>
#include <memory>
#include <string>

#include "common/logging.h"
#include "common/paths.h"
#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/cloud_candidate_source.h"
#include "core/input_profile.h"
#include "core/input_session.h"
#include "core/profile_switch.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"
#include "core/localization.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"
#include "candidate_window.h"
#include "settings_store.h"

namespace {

using paiza::linux_ime::Settings;
using paiza::linux_ime::SettingsWatcher;

// Process-wide cloud candidate source (registered with the shared engine,
// toggled from the settings dialog); mirrors the Windows/macOS wiring.
paiza::CloudCandidateSource* g_cloud_source = nullptr;
// Dynamic user lexicon (dual-track overlay, spec §3.2); persisted to
// ~/.config/paiza-ime/user.db asynchronously.
paiza::UserDictionary* g_user_dictionary = nullptr;
// Static lexicon (compiled lexicon.bin, mmap): prefix + fault-tolerant
// search + bigram context; ranked below the user overlay, above cloud.
paiza::StaticLexicon* g_static_lexicon = nullptr;

// Input profiles (docs/input-profiles.md). The switch is global because the
// candidate sources are; each engine instance has its own session, so it is
// wired through ApplyTo when the engine is initialised.
paiza::InputProfileRegistry g_profiles;
std::unique_ptr<paiza::ProfileSwitch> g_profile_switch;

// Builds the profile switch once, binding the shared sources to the Unicode
// profile. Safe to call repeatedly.
paiza::ProfileSwitch* SharedProfileSwitch() {
  if (g_profile_switch == nullptr) {
    // Profiles are installed next to the other data; a missing directory
    // falls back to the built-in Unicode profile.
    g_profiles.LoadFromDirectory(
        paiza::Utf8ToWide(std::string(PAIZA_DATA_DIR) + "/profiles"));
    g_profiles.EnsureDefault();
    g_profile_switch = std::make_unique<paiza::ProfileSwitch>(
        &g_profiles, /*session=*/nullptr);
    paiza::ProfileBinding unicode;
    unicode.user_dictionary = g_user_dictionary;
    unicode.static_lexicon = g_static_lexicon;
    if (g_static_lexicon != nullptr) {
      unicode.fixed_sequences = &g_static_lexicon->fixed_sequence_table();
    }
    g_profile_switch->Bind(paiza::kDefaultProfileId, unicode);
  }
  return g_profile_switch.get();
}

// Directory of the running executable (build-tree fallback lookups).
std::string ExecutableDirectory() {
  char* path = g_file_read_link("/proc/self/exe", nullptr);
  if (path == nullptr) return {};
  char* dir = g_path_get_dirname(path);
  std::string result = dir != nullptr ? dir : "";
  g_free(dir);
  g_free(path);
  return result;
}

// The candidate engine and key-mapping store are process-wide (one
// engine process serves every client); sessions are per engine instance.
paiza::CandidateEngine* SharedEngine() {
  static paiza::CandidateEngine* engine = [] {
    auto* built = new paiza::CandidateEngine();
    auto user = std::make_unique<paiza::UserDictionary>(
        (std::filesystem::path(paiza::UserConfigDirectory()) / L"user.db")
            .wstring());
    g_user_dictionary = user.get();
    built->AddSource(std::move(user));
    // Compiled static lexicon: installed location first, then build tree.
    auto lexicon = std::make_unique<paiza::StaticLexicon>();
    const std::string lexicon_installed =
        std::string(PAIZA_DATA_DIR) + "/lexicon.bin";
    if (lexicon->Load(paiza::Utf8ToWide(lexicon_installed)) ||
        lexicon->Load(paiza::Utf8ToWide(ExecutableDirectory() +
                                        "/lexicon.bin"))) {
      const std::filesystem::path cfg(paiza::UserConfigDirectory());
      lexicon->LoadTuning((cfg / L"scoring.txt").wstring(),
                          (cfg / L"equivalences.txt").wstring());
      g_static_lexicon = lexicon.get();
      built->AddSource(std::move(lexicon));
    }
    auto dictionary = std::make_unique<paiza::LocalDictionarySource>();
    // Installed location first, then next to the binary (build tree).
    const std::string installed =
        std::string(PAIZA_DATA_DIR) + "/dictionary.txt";
    if (dictionary->LoadFromFile(paiza::Utf8ToWide(installed)) == 0) {
      dictionary->LoadFromFile(
          paiza::Utf8ToWide(ExecutableDirectory() + "/dictionary.txt"));
    }
    built->AddSource(std::move(dictionary));
    auto cloud = std::make_unique<paiza::CloudCandidateSource>(L"");
    g_cloud_source = cloud.get();
    built->AddSource(std::move(cloud));
    return built;
  }();
  return engine;
}

paiza::KeyMappingStore* SharedMappings() {
  // Hot-reloads ~/.config/paiza-ime/keymap.txt — same format as
  // Windows/macOS.
  static paiza::KeyMappingStore* store = new paiza::KeyMappingStore();
  return store;
}

// Per-engine C++ state (GObject instances are g_malloc0'ed, so C++
// members live behind this pointer, created in init / freed in finalize).
struct EngineState {
  std::unique_ptr<paiza::InputSession> session;
  SettingsWatcher settings;
  IBusLookupTable* table = nullptr;   // Owned (ref_sink'ed).
  IBusPropList* properties = nullptr; // Owned (ref_sink'ed).
  // v2 custom vertical candidate window (X11 only; nullptr on Wayland or
  // when no display is reachable — the system lookup table then draws
  // the candidates as in v1). Created lazily on the first composition.
  std::unique_ptr<paiza::linux_ime::CandidateWindow> window;
  bool window_checked = false;
};

}  // namespace

extern "C" {

struct PaizaEngine {
  IBusEngine parent;
  EngineState* state;
};

struct PaizaEngineClass {
  IBusEngineClass parent;
};

G_DEFINE_TYPE(PaizaEngine, paiza_engine, IBUS_TYPE_ENGINE)

}  // extern "C"

namespace {

// Packs the five soft-equivalence toggles into a bitmask so the (mildly
// expensive) LoadFromString only runs when a pair actually changed.
int EquivBits(const Settings& s) {
  return (s.equiv_ou << 0) | (s.equiv_qv << 1) | (s.equiv_hg << 2) |
         (s.equiv_td << 3) | (s.equiv_ew << 4);
}

// Pushes the current settings into this engine's session (page size), the
// shared engine (cloud toggle) and the static lexicon (soft-equivalence
// set). Cheap and idempotent; called on every sync so saves from the
// settings dialog apply on the next keystroke (the watcher re-reads the
// INI when its mtime changes).
void ApplyCurrentSettings(PaizaEngine* engine) {
  const Settings& settings = engine->state->settings.Get();
  engine->state->session->set_page_size(settings.candidates_per_page);
  SharedEngine();
  // Gate the candidate sources to the selected profile. An unknown id leaves
  // the previous selection in place rather than half-switching.
  SharedProfileSwitch()->Activate(settings.active_profile);
  if (g_cloud_source != nullptr) {
    g_cloud_source->set_enabled(settings.cloud_source_enabled);
  }
  // Rebuild the active soft-equivalence set from the per-pair toggles and
  // load it into the static lexicon — identical to Windows/macOS
  // ApplySettings. Guarded so it only reloads when a pair changed.
  static int g_applied_equiv_bits = -1;
  if (g_static_lexicon != nullptr) {
    const int bits = EquivBits(settings);
    if (bits != g_applied_equiv_bits) {
      g_applied_equiv_bits = bits;
      std::string equiv;
      if (settings.equiv_ou) equiv += "o u\n";
      if (settings.equiv_qv) equiv += "q v\n";
      if (settings.equiv_hg) equiv += "h g\n";
      if (settings.equiv_td) equiv += "t d\n";
      if (settings.equiv_ew) equiv += "E w\n";
      g_static_lexicon->equivalences().LoadFromString(equiv);
    }
  }
}

gboolean ApplyResultForward(PaizaEngine* engine,
                            paiza::InputSession::KeyResult result);
void SyncState(PaizaEngine* engine);

// Lazily creates the v2 custom candidate window (X11 only). Returns
// nullptr when the platform cannot host it — v1 lookup-table path then.
paiza::linux_ime::CandidateWindow* CustomWindow(PaizaEngine* engine) {
  EngineState* state = engine->state;
  if (!state->window_checked) {
    state->window_checked = true;
    if (paiza::linux_ime::CandidateWindow::Available()) {
      state->window =
          std::make_unique<paiza::linux_ime::CandidateWindow>();
      state->window->on_candidate_chosen = [engine](size_t index) {
        ApplyResultForward(
            engine, engine->state->session->CommitCandidate(index));
      };
      state->window->on_page_backward = [engine] {
        engine->state->session->PageBackward();
        SyncState(engine);
      };
      state->window->on_page_forward = [engine] {
        engine->state->session->PageForward();
        SyncState(engine);
      };
      PAIZA_LOG_INFO(L"custom candidate window enabled (X11)");
    } else {
      PAIZA_LOG_INFO(L"custom candidate window unavailable, "
                     L"using the system lookup table");
    }
  }
  return state->window.get();
}

// Mirrors the session state into the client: preedit + candidates. The
// candidates go to the custom vertical window on X11 (v2) and to the
// system IBusLookupTable panel elsewhere (v1).
void SyncState(PaizaEngine* engine) {
  ApplyCurrentSettings(engine);
  IBusEngine* base = IBUS_ENGINE(engine);
  paiza::InputSession* session = engine->state->session.get();
  paiza::linux_ime::CandidateWindow* window = CustomWindow(engine);

  if (session->composing()) {
    // How much goes inline is a setting (docs/input-profiles.md): the client
    // renders it in its own font. A placeholder keeps the preedit non-empty
    // so cursor-following clients still have something to anchor to.
    std::wstring inline_text = session->preedit();
    switch (engine->state->settings.Get().inline_composition) {
      case paiza::InlineComposition::kHidden:
        inline_text.clear();
        break;
      case paiza::InlineComposition::kPlaceholder:
        inline_text = L" ";
        break;
      case paiza::InlineComposition::kFull:
        break;
    }
    const std::string preedit = paiza::WideToUtf8(inline_text);
    const glong length = g_utf8_strlen(preedit.c_str(), -1);
    IBusText* text = ibus_text_new_from_string(preedit.c_str());
    ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
                               IBUS_ATTR_UNDERLINE_SINGLE, 0,
                               static_cast<guint>(length));
    ibus_engine_update_preedit_text(base, text,
                                    static_cast<guint>(length), TRUE);

    if (window != nullptr) {
      window->SetFont(paiza::WideToUtf8(
          engine->state->settings.Get().candidate_font));
      window->UpdateFromSession(session);
      // cursor_area holds the caret rectangle in root coordinates,
      // refreshed by the client through set-cursor-location.
      window->ShowNear(base->cursor_area.x, base->cursor_area.y,
                       base->cursor_area.width, base->cursor_area.height);
      return;
    }

    IBusLookupTable* table = engine->state->table;
    ibus_lookup_table_clear(table);
    ibus_lookup_table_set_page_size(
        table, static_cast<guint>(session->page_size()));
    for (const paiza::Candidate& candidate : session->candidates()) {
      const std::string utf8 = paiza::WideToUtf8(candidate.text);
      ibus_lookup_table_append_candidate(
          table, ibus_text_new_from_string(utf8.c_str()));
    }
    // The panel derives the visible page from the cursor position, so
    // paging through InputSession keeps panel and session in step.
    ibus_lookup_table_set_cursor_pos(
        table, static_cast<guint>(session->selected_index()));
    ibus_engine_update_lookup_table(base, table, TRUE);
  } else {
    ibus_engine_hide_preedit_text(base);
    if (window != nullptr) window->Hide();
    ibus_engine_hide_lookup_table(base);
  }
}

// Applies a key result: commit text if any, then re-sync. Returns the
// "handled" flag for process_key_event.
gboolean ApplyResult(PaizaEngine* engine,
                     paiza::InputSession::KeyResult result) {
  if (result.commit.has_value()) {
    const std::string utf8 = paiza::WideToUtf8(*result.commit);
    ibus_engine_commit_text(IBUS_ENGINE(engine),
                            ibus_text_new_from_string(utf8.c_str()));
  }
  SyncState(engine);
  return result.handled ? TRUE : FALSE;
}

// Out-of-line so the candidate-window callbacks (declared before
// ApplyResult) can reach it.
gboolean ApplyResultForward(PaizaEngine* engine,
                            paiza::InputSession::KeyResult result) {
  return ApplyResult(engine, result);
}

// Launches the settings dialog (sibling binary; falls back to $PATH).
void LaunchSettingsDialog() {
  const std::string sibling = ExecutableDirectory() + "/paiza-ime-settings";
  const char* command = g_file_test(sibling.c_str(), G_FILE_TEST_IS_EXECUTABLE)
                            ? sibling.c_str()
                            : "paiza-ime-settings";
  GError* error = nullptr;
  if (!g_spawn_command_line_async(command, &error)) {
    PAIZA_LOG_WARN(L"failed to launch settings dialog");
    g_clear_error(&error);
  }
}

}  // namespace

extern "C" {

static void paiza_engine_init(PaizaEngine* engine) {
  engine->state = new EngineState();
  engine->state->session = std::make_unique<paiza::InputSession>(
      SharedEngine(), SharedMappings());
  // Commits feed the dynamic user lexicon (frequency + coined words) and
  // set the static lexicon's bigram context for the next word.
  // The active profile decides which dictionary and lexicon this session
  // records into, and whether the GB/T fixed sequences apply.
  SharedProfileSwitch()->ApplyTo(*engine->state->session);

  IBusLookupTable* table = ibus_lookup_table_new(
      /*page_size=*/7, /*cursor_pos=*/0, /*cursor_visible=*/TRUE,
      /*round=*/FALSE);
  ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_VERTICAL);
  engine->state->table =
      IBUS_LOOKUP_TABLE(g_object_ref_sink(table));

  // Panel menu: one "Paiza Settings…" entry (localized), the Linux
  // equivalent of the macOS input-menu item / Windows hotkey.
  const paiza::UiStrings& strings = paiza::GetStrings(
      engine->state->settings.Get().ui_language);
  const std::string label = paiza::WideToUtf8(strings.menu_settings);
  IBusProperty* property = ibus_property_new(
      "paiza-settings", PROP_TYPE_NORMAL,
      ibus_text_new_from_string(label.c_str()),
      /*icon=*/nullptr, ibus_text_new_from_string(label.c_str()),
      /*sensitive=*/TRUE, /*visible=*/TRUE, PROP_STATE_UNCHECKED,
      /*prop_list=*/nullptr);
  IBusPropList* properties = ibus_prop_list_new();
  ibus_prop_list_append(properties, property);
  engine->state->properties =
      IBUS_PROP_LIST(g_object_ref_sink(properties));
}

static void paiza_engine_finalize(GObject* object) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(object);
  if (engine->state != nullptr) {
    if (engine->state->table != nullptr) {
      g_object_unref(engine->state->table);
    }
    if (engine->state->properties != nullptr) {
      g_object_unref(engine->state->properties);
    }
    delete engine->state;
    engine->state = nullptr;
  }
  G_OBJECT_CLASS(paiza_engine_parent_class)->finalize(object);
}

static gboolean paiza_engine_process_key_event(IBusEngine* base,
                                               guint keyval, guint keycode,
                                               guint modifiers) {
  (void)keycode;
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  paiza::InputSession* session = engine->state->session.get();

  if ((modifiers & IBUS_RELEASE_MASK) != 0) return FALSE;
  // Control/Alt/Super chords belong to the application.
  if ((modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK |
                    IBUS_MOD4_MASK | IBUS_META_MASK | IBUS_HYPER_MASK)) !=
      0) {
    return FALSE;
  }

  switch (keyval) {
    case IBUS_KEY_Return:
    case IBUS_KEY_KP_Enter:
      return ApplyResult(engine, session->HandleEnter());
    case IBUS_KEY_space:
      return ApplyResult(engine, session->HandleSpace());
    case IBUS_KEY_BackSpace:
      return ApplyResult(engine, session->HandleBackspace());
    case IBUS_KEY_Escape:
      return ApplyResult(engine, session->HandleEscape());
    case IBUS_KEY_Page_Down:
      if (!session->composing()) return FALSE;
      session->PageForward();
      SyncState(engine);
      return TRUE;
    case IBUS_KEY_Page_Up:
      if (!session->composing()) return FALSE;
      session->PageBackward();
      SyncState(engine);
      return TRUE;
    case IBUS_KEY_Right:
    case IBUS_KEY_Down:
      if (!session->composing()) return FALSE;
      session->MoveSelection(+1);
      SyncState(engine);
      return TRUE;
    case IBUS_KEY_Left:
    case IBUS_KEY_Up:
      if (!session->composing()) return FALSE;
      session->MoveSelection(-1);
      SyncState(engine);
      return TRUE;
    default:
      break;
  }

  // The keyval already resolves Shift/Caps, giving the session the
  // case-resolved letter (second letter set) or the mapped punctuation
  // (- _ @ # $ % * by default; keymap.txt-driven).
  if (keyval < 128 &&
      session->IsComposableChar(static_cast<char>(keyval))) {
    return ApplyResult(engine,
                       session->HandleLetter(static_cast<char>(keyval)));
  }
  if (keyval >= '1' && keyval <= '9') {
    return ApplyResult(engine,
                       session->HandleDigit(static_cast<int>(keyval - '0')));
  }

  // Anything else: commit the raw conversion, let the key through.
  return ApplyResult(engine, session->HandleOtherKey());
}

static void paiza_engine_focus_in(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  ibus_engine_register_properties(base, engine->state->properties);
}

static void paiza_engine_focus_out(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  engine->state->session->Reset();
  SyncState(engine);
}

static void paiza_engine_reset(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  engine->state->session->Reset();
  SyncState(engine);
}

// Panel interactions --------------------------------------------------------

static void paiza_engine_candidate_clicked(IBusEngine* base, guint index,
                                           guint button, guint state) {
  (void)button;
  (void)state;
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  paiza::InputSession* session = engine->state->session.get();
  // `index` is relative to the visible page (see docs/linux-port.md
  // "known unknowns" — verify on-device).
  const size_t global =
      session->current_page() * session->page_size() + index;
  ApplyResult(engine, session->CommitCandidate(global));
}

static void paiza_engine_page_down_button(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  if (!engine->state->session->composing()) return;
  engine->state->session->PageForward();
  SyncState(engine);
}

static void paiza_engine_page_up_button(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  if (!engine->state->session->composing()) return;
  engine->state->session->PageBackward();
  SyncState(engine);
}

static void paiza_engine_cursor_down(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  if (!engine->state->session->composing()) return;
  engine->state->session->MoveSelection(+1);
  SyncState(engine);
}

static void paiza_engine_cursor_up(IBusEngine* base) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  if (!engine->state->session->composing()) return;
  engine->state->session->MoveSelection(-1);
  SyncState(engine);
}

static void paiza_engine_set_cursor_location(IBusEngine* base, gint x,
                                             gint y, gint w, gint h) {
  PaizaEngine* engine = reinterpret_cast<PaizaEngine*>(base);
  // Keep the public field current whether or not the dispatcher already
  // wrote it, then follow the caret while a composition is showing.
  base->cursor_area = {x, y, w, h};
  if (engine->state->window != nullptr &&
      engine->state->session->composing()) {
    engine->state->window->ShowNear(x, y, w, h);
  }
}

static void paiza_engine_property_activate(IBusEngine* base,
                                           const gchar* name, guint state) {
  (void)base;
  (void)state;
  if (g_strcmp0(name, "paiza-settings") == 0) {
    LaunchSettingsDialog();
  }
}

static void paiza_engine_class_init(PaizaEngineClass* klass) {
  GObjectClass* object_class = G_OBJECT_CLASS(klass);
  IBusEngineClass* engine_class = IBUS_ENGINE_CLASS(klass);

  object_class->finalize = paiza_engine_finalize;
  engine_class->process_key_event = paiza_engine_process_key_event;
  engine_class->focus_in = paiza_engine_focus_in;
  engine_class->focus_out = paiza_engine_focus_out;
  engine_class->reset = paiza_engine_reset;
  engine_class->candidate_clicked = paiza_engine_candidate_clicked;
  engine_class->page_up = paiza_engine_page_up_button;
  engine_class->page_down = paiza_engine_page_down_button;
  engine_class->cursor_up = paiza_engine_cursor_up;
  engine_class->cursor_down = paiza_engine_cursor_down;
  engine_class->set_cursor_location = paiza_engine_set_cursor_location;
  engine_class->property_activate = paiza_engine_property_activate;
}

}  // extern "C"
