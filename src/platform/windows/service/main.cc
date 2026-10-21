// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Candidate service process (spec items 10-12, 14):
//   * hosts the extensible CandidateEngine (local dictionary + cloud stub)
//   * serves TSF clients over the named pipe
//   * owns the settings dialog, woken by Ctrl+Alt+P or an IPC request
//   * single instance; "PaizaService.exe --settings" opens the dialog of
//     the running instance

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>

#include "common/logging.h"
#include "common/paths.h"
#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/cloud_candidate_source.h"
#include "core/external_engine_source.h"
#include "core/local_dictionary_source.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"
#include "ipc/ime_client.h"
#include "ipc/named_pipe_server.h"
#include "service/candidate_service.h"
#include "core/input_profile.h"
#include "core/profile_switch.h"
#include "service/settings_store.h"
#include "service/settings_window.h"
#include "ui/candidate_window.h"
#include "ui/font_stem_config.h"

namespace paiza::service {
namespace {

constexpr wchar_t kSingletonMutexName[] = L"PaizaImeServiceSingleton";
constexpr wchar_t kMessageWindowClass[] = L"PaizaServiceMessageWindow";
constexpr UINT kMsgShowSettings = WM_APP + 1;
constexpr int kHotkeyId = 1;

SettingsWindow* g_settings_window = nullptr;
CloudCandidateSource* g_cloud_source = nullptr;
StaticLexicon* g_static_lexicon = nullptr;
// Input profiles (docs/input-profiles.md). The service process owns the
// candidate sources, so this is where profile gating happens; the key policy
// lives in the TSF module, so there is no InputSession to repoint here.
InputProfileRegistry g_profiles;
std::unique_ptr<ProfileSwitch> g_profile_switch;

std::wstring ExecutableDirectory() {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L'\\');
  return slash == std::wstring::npos ? dir : dir.substr(0, slash);
}

void ApplySettings(const Settings& settings) {
  // Gate the candidate sources to the selected profile. An unknown id leaves
  // the previous selection in place rather than half-switching.
  if (g_profile_switch != nullptr) {
    g_profile_switch->Activate(settings.active_profile);
  }
  if (g_cloud_source != nullptr) {
    g_cloud_source->set_enabled(settings.cloud_source_enabled);
  }
  // Rebuild the active soft-equivalence set from the per-pair toggles; a
  // disabled pair is dropped so that pair is matched precisely.
  if (g_static_lexicon != nullptr) {
    std::string equiv;
    if (settings.equiv_ou) equiv += "o u\n";
    if (settings.equiv_qv) equiv += "q v\n";
    if (settings.equiv_hg) equiv += "h g\n";
    if (settings.equiv_td) equiv += "t d\n";
    if (settings.equiv_ew) equiv += "E w\n";
    g_static_lexicon->equivalences().LoadFromString(equiv);
  }
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                   LPARAM lparam) {
  switch (message) {
    case WM_HOTKEY:
      if (wparam == kHotkeyId && g_settings_window != nullptr) {
        g_settings_window->ShowDialog();
      }
      return 0;
    case kMsgShowSettings:
      if (g_settings_window != nullptr) g_settings_window->ShowDialog();
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    default:
      return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

HWND CreateMessageWindow() {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = MessageWindowProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.lpszClassName = kMessageWindowClass;
  ::RegisterClassExW(&wc);
  return ::CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

// Developer aid ("--preview-candidates"): shows the candidate window with
// sample data (including long words) so its layout can be checked without
// registering the TSF DLL and typing in a host application.
int RunCandidatePreview() {
  ui::CandidateWindow window;
  if (!window.EnsureCreated()) return 1;
  window.SetFontFamily(SettingsStore::Load().candidate_font);

  std::vector<Candidate> samples;
  const wchar_t* words[] = {
      L"\x1820\x180A\x1820",                          // A NIRUGU A (spine)
      L"\x182E\x180A\x182E",                          // MA NIRUGU MA (spine)
      L"\x1830\x1820\x1836\x1822\x1828",              // sain
      L"\x182E\x1823\x1829\x182D\x1823\x182F",        // monggol
      L"\x1830\x1824\x1837\x182D\x1820\x182D\x1824\x182F\x1822",  // long
      L"\x182A\x1822\x1834\x1822\x182D",              // bichig
      L"\x1820\x182E\x1824\x182D\x1824\x182F\x1820\x1829",        // long
      L"\x1832\x1821\x182E\x1821\x182D\x1821",        // temee
      L"\x182A\x1820\x182D\x1830\x1822",              // bagsi
      L"\x182C\x1823\x1832\x1820",                    // hota
  };
  for (const wchar_t* word : words) {
    Candidate candidate;
    candidate.text = word;
    samples.push_back(std::move(candidate));
  }
  // Preedit strip preview (the composition as typed, e.g. "sain").
  window.SetPreedit(L"\x1830\x1820\x1822\x1828");
  window.SetCandidates(std::move(samples));
  const RECT caret = {320, 240, 324, 264};
  window.MoveNearCaret(caret);

  MSG msg = {};
  while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&msg);
    ::DispatchMessageW(&msg);
  }
  return 0;
}

int Run(bool show_settings_on_start) {
  // Build the engine with all available candidate sources. Adding a new
  // source type (AI, speech, handwriting) is one AddSource call here.
  auto engine = std::make_shared<CandidateEngine>();

  // Input profiles decide which stores are live (docs/input-profiles.md).
  // Load them before the sources so one user lexicon per profile can be
  // created; a missing directory falls back to the built-in profile.
  g_profiles.LoadFromDirectory(ExecutableDirectory() + L"\\profiles");
  g_profiles.EnsureDefault();

  // Dynamic user lexicon (dual-track overlay, spec §3.2): highest priority,
  // persisted asynchronously to SQLite in the user profile. One per profile,
  // because entries learned under one text encoding are meaningless under
  // another; ProfileSwitch keeps all but the active one unavailable.
  std::vector<std::pair<std::string, UserDictionary*>> user_dictionaries;
  for (const InputProfile& profile : g_profiles.All()) {
    const std::wstring file =
        profile.user_db.empty() ? L"user.db" : profile.user_db;
    auto dictionary = std::make_unique<UserDictionary>(
        (std::filesystem::path(UserConfigDirectory()) / file).wstring());
    user_dictionaries.emplace_back(profile.id, dictionary.get());
    engine->AddSource(std::move(dictionary));
  }

  // External engine processes, for the profiles that declare one
  // (docs/engine-contract.md). Started here rather than on first use so that a
  // missing or mismatched engine is reported now, in the log and the settings
  // page, instead of silently producing no candidates on a keystroke.
  std::vector<std::pair<std::string, ExternalEngineSource*>> engine_sources;
  for (const InputProfile& profile : g_profiles.All()) {
    if (!profile.has_engine()) continue;
    std::string reason;
    auto source = MakeEngineSource(profile, ExecutableDirectory(), &reason);
    if (source == nullptr) {
      PAIZA_LOG_ERROR(L"profile '" + Utf8ToWide(profile.id) +
                      L"': engine unavailable: " + Utf8ToWide(reason));
      continue;
    }
    PAIZA_LOG_INFO(L"profile '" + Utf8ToWide(profile.id) +
                   L"': engine started");
    engine_sources.emplace_back(profile.id, source.get());
    engine->AddSource(std::move(source));
  }

  // Compiled static lexicon (mmap): prefix + fault-tolerant search + bigram
  // context. Falls back silently to the plain-text dictionary if absent.
  auto static_lexicon = std::make_unique<StaticLexicon>();
  StaticLexicon* static_lexicon_ptr = nullptr;
  if (static_lexicon->Load(ExecutableDirectory() + L"\\lexicon.bin")) {
    // Optional tuning knobs from the user profile (missing => defaults).
    const std::wstring cfg = UserConfigDirectory();
    static_lexicon->LoadTuning(cfg + L"\\scoring.txt",
                               cfg + L"\\equivalences.txt");
    static_lexicon_ptr = static_lexicon.get();
    g_static_lexicon = static_lexicon_ptr;  // For ApplySettings (equiv toggles).
    engine->AddSource(std::move(static_lexicon));
  }

  auto dictionary = std::make_unique<LocalDictionarySource>();
  dictionary->LoadFromFile(ExecutableDirectory() + L"\\dictionary.txt");
  engine->AddSource(std::move(dictionary));

  auto cloud = std::make_unique<CloudCandidateSource>(L"");
  g_cloud_source = cloud.get();
  engine->AddSource(std::move(cloud));

  g_profile_switch =
      std::make_unique<ProfileSwitch>(&g_profiles, /*session=*/nullptr);
  for (const auto& [profile_id, dictionary] : user_dictionaries) {
    ProfileBinding binding;
    binding.user_dictionary = dictionary;
    // One compiled lexicon today; per-profile lexicons arrive with the
    // second profile.
    if (profile_id == kDefaultProfileId) {
      binding.static_lexicon = static_lexicon_ptr;
    }
    for (const auto& [engine_profile_id, source] : engine_sources) {
      if (engine_profile_id == profile_id) binding.engine_source = source;
    }
    g_profile_switch->Bind(profile_id, binding);
  }

  ApplySettings(SettingsStore::Load());

  HWND message_window = CreateMessageWindow();
  if (message_window == nullptr) return 1;

  SettingsWindow settings_window(ApplySettings, g_profile_switch.get());
  g_settings_window = &settings_window;

  // IPC callbacks arrive on worker threads; hop to the UI thread.
  CandidateService service(engine, g_profile_switch.get(),
                           [message_window] {
                             ::PostMessageW(message_window, kMsgShowSettings, 0,
                                            0);
                           });
  ipc::NamedPipeServer server(
      [&service](const ipc::Message& request) {
        return service.HandleMessage(request);
      });
  if (!server.Start()) {
    PAIZA_LOG_ERROR(L"Failed to start pipe server");
    return 1;
  }

  // Global wake-up hotkey for the settings dialog (spec item 14).
  if (!::RegisterHotKey(message_window, kHotkeyId, MOD_CONTROL | MOD_ALT,
                        'P')) {
    PAIZA_LOG_WARN(L"RegisterHotKey Ctrl+Alt+P failed (already taken?)");
  }

  if (show_settings_on_start) settings_window.ShowDialog();

  MSG msg = {};
  while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&msg);
    ::DispatchMessageW(&msg);
  }

  ::UnregisterHotKey(message_window, kHotkeyId);
  server.Stop();
  g_settings_window = nullptr;
  g_cloud_source = nullptr;
  g_profile_switch.reset();
  return 0;
}

}  // namespace
}  // namespace paiza::service

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
  // Native-pixel rendering on high-DPI displays (fixes blurry text; the
  // UI library scales its logical layout by the window DPI).
  ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Materialise the per-font stem calibration file (built-in defaults) so
  // users can discover and edit it; host processes only ever read it.
  paiza::ui::EnsureFontStemConfigFile();

  const std::wstring args =
      command_line != nullptr ? std::wstring(command_line) : std::wstring();
  if (args.find(L"--preview-candidates") != std::wstring::npos) {
    return paiza::service::RunCandidatePreview();
  }
  const bool wants_settings =
      args.find(L"--settings") != std::wstring::npos;

  // Single instance: a second launch either forwards --settings to the
  // running instance or just exits.
  HANDLE mutex =
      ::CreateMutexW(nullptr, TRUE, paiza::service::kSingletonMutexName);
  if (mutex != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS) {
    if (wants_settings) {
      paiza::ipc::ImeClient client;
      client.ShowSettings();
    }
    ::CloseHandle(mutex);
    return 0;
  }

  const int result = paiza::service::Run(wants_settings);
  if (mutex != nullptr) ::CloseHandle(mutex);
  return result;
}
