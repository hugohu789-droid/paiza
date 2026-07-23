// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// End-to-end typing smoke test: acts as an IBus *client* (like a GTK
// text entry would), so the full path daemon -> engine process ->
// InputSession -> commit is exercised with no GUI at all. Meant for the
// private-bus harness in docs/linux-port.md ("Container verification"):
//
//   eval $(dbus-launch --sh-syntax)   # or a dbus-daemon --session bus
//   ibus-daemon --panel disable &     # with paiza.xml discoverable
//   ./build/bin/paiza_typing_smoke    # exits 0 on Mongolian commit
//
// Types PAIZA_INPUT (default "sa"), then Space to commit the highlighted
// candidate; prints every preedit update and the committed text, and
// fails unless something outside ASCII (i.e. Mongolian script) was
// committed.

#include <ibus.h>

#include <cstdio>
#include <string>

namespace {

struct SmokeState {
  GMainLoop* loop = nullptr;
  std::string committed;
  bool failed = false;
};

void OnCommitText(IBusInputContext* context, IBusText* text,
                  gpointer user_data) {
  (void)context;
  auto* state = static_cast<SmokeState*>(user_data);
  const gchar* str = ibus_text_get_text(text);
  state->committed += str != nullptr ? str : "";
  std::printf("commit-text: \"%s\"\n", str != nullptr ? str : "");
  g_main_loop_quit(state->loop);
}

void OnUpdatePreedit(IBusInputContext* context, IBusText* text,
                     guint cursor_pos, gboolean visible,
                     gpointer user_data) {
  (void)context;
  (void)cursor_pos;
  (void)user_data;
  const gchar* str = ibus_text_get_text(text);
  std::printf("preedit: \"%s\" (visible=%d)\n",
              str != nullptr ? str : "", visible);
}

gboolean OnTimeout(gpointer user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  state->failed = true;
  std::fprintf(stderr, "timeout waiting for the engine\n");
  g_main_loop_quit(state->loop);
  return G_SOURCE_REMOVE;
}

// Pumps the default main context for `ms` so async D-Bus traffic (engine
// spawn, signals) can settle between steps.
void Pump(guint ms) {
  GMainContext* context = g_main_context_default();
  const gint64 deadline = g_get_monotonic_time() + ms * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(context, FALSE);
    g_usleep(2000);
  }
}

}  // namespace

int main() {
  ibus_init();
  IBusBus* bus = ibus_bus_new();
  if (!ibus_bus_is_connected(bus)) {
    std::fprintf(stderr, "cannot connect to ibus-daemon\n");
    return 1;
  }

  IBusInputContext* context =
      ibus_bus_create_input_context(bus, "paiza-smoke");
  if (context == nullptr) {
    std::fprintf(stderr, "cannot create input context\n");
    return 1;
  }
  ibus_input_context_set_capabilities(
      context, IBUS_CAP_PREEDIT_TEXT | IBUS_CAP_LOOKUP_TABLE |
                   IBUS_CAP_FOCUS);

  SmokeState state;
  state.loop = g_main_loop_new(nullptr, FALSE);
  g_signal_connect(context, "commit-text", G_CALLBACK(OnCommitText),
                   &state);
  g_signal_connect(context, "update-preedit-text",
                   G_CALLBACK(OnUpdatePreedit), &state);

  ibus_input_context_focus_in(context);
  // The daemon runs in global-engine mode by default and ignores a
  // per-context SetEngine, so switch the global engine (what `ibus
  // engine paiza-mongolian` does); the focused context receives it. The
  // daemon spawns the engine process on demand, hence the polling.
  ibus_bus_set_global_engine(bus, "paiza-mongolian");
  bool attached = false;
  for (int i = 0; i < 100 && !attached; ++i) {
    Pump(200);
    IBusEngineDesc* desc = ibus_bus_get_global_engine(bus);
    if (desc != nullptr) {
      attached =
          g_strcmp0(ibus_engine_desc_get_name(desc), "paiza-mongolian") == 0;
      g_object_unref(desc);
    }
    if (!attached && i % 10 == 9) {
      ibus_bus_set_global_engine(bus, "paiza-mongolian");
    }
  }
  if (!attached) {
    std::fprintf(stderr, "engine never attached\n");
    return 1;
  }
  std::printf("engine attached\n");
  Pump(500);

  const std::string input = std::getenv("PAIZA_INPUT") ?: "sa";
  for (const char c : input) {
    const guint keyval = static_cast<guint>(c);
    if (!ibus_input_context_process_key_event(context, keyval, 0, 0)) {
      std::fprintf(stderr, "engine did not handle '%c'\n", c);
      return 1;
    }
    ibus_input_context_process_key_event(context, keyval, 0,
                                         IBUS_RELEASE_MASK);
    Pump(150);
  }

  // Space commits the highlighted candidate.
  ibus_input_context_process_key_event(context, IBUS_KEY_space, 0, 0);
  ibus_input_context_process_key_event(context, IBUS_KEY_space, 0,
                                       IBUS_RELEASE_MASK);

  const guint timeout =
      g_timeout_add_seconds(10, OnTimeout, &state);
  if (state.committed.empty()) g_main_loop_run(state.loop);
  g_source_remove(timeout);

  bool mongolian = false;
  for (const unsigned char byte : state.committed) {
    if (byte >= 0x80) mongolian = true;  // Any non-ASCII UTF-8 byte.
  }
  if (state.failed || !mongolian) {
    std::fprintf(stderr, "FAIL: committed \"%s\"\n",
                 state.committed.c_str());
    return 1;
  }
  std::printf("TYPING OK: committed \"%s\"\n", state.committed.c_str());
  return 0;
}
