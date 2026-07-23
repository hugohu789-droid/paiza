// Copyright 2026 Paiza. All rights reserved.
//
// Entry point of the IBus engine process. ibus-daemon spawns it with
// --ibus (per the component XML installed to share/ibus/component/);
// running it without the flag registers the component on the bus
// directly, which is handy while developing:
//
//   ./build/bin/ibus-engine-paiza      # then: ibus engine paiza-mongolian

#include <ibus.h>

#include <string>

#include "common/logging.h"
#include "common/string_utils.h"
#include "core/font_stem_config.h"
#include "engine.h"

namespace {

gboolean g_by_ibus = FALSE;

const GOptionEntry kOptions[] = {
    {"ibus", 'i', 0, G_OPTION_ARG_NONE, &g_by_ibus,
     "component is executed by ibus", nullptr},
    {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
};

void OnDisconnected(IBusBus* bus, gpointer user_data) {
  (void)bus;
  (void)user_data;
  ibus_quit();
}

}  // namespace

int main(int argc, char* argv[]) {
  GError* error = nullptr;
  GOptionContext* context = g_option_context_new("- Paiza Mongolian IME");
  g_option_context_add_main_entries(context, kOptions, nullptr);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("option parsing failed: %s\n", error->message);
    g_clear_error(&error);
    return 1;
  }
  g_option_context_free(context);

  // Owner process writes the calibration file so users can edit it
  // (same rule as the Windows service / macOS app).
  paiza::EnsureFontStemConfigFile();

  ibus_init();
  IBusBus* bus = ibus_bus_new();
  if (!ibus_bus_is_connected(bus)) {
    g_printerr("cannot connect to ibus-daemon\n");
    return 1;
  }
  g_signal_connect(bus, "disconnected", G_CALLBACK(OnDisconnected),
                   nullptr);

  IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(bus));
  ibus_factory_add_engine(factory, "paiza-mongolian", PAIZA_TYPE_ENGINE);

  if (g_by_ibus) {
    ibus_bus_request_name(bus, "com.paiza.inputmethod.mongolian", 0);
  } else {
    // Developer mode: register the component ourselves.
    IBusComponent* component = ibus_component_new(
        "com.paiza.inputmethod.mongolian", "Paiza Mongolian IME", "0.1.0",
        "AGPL-3.0", "Paiza", "https://github.com/hugohu789-droid/paiza",
        /*command_line=*/"", "ibus-paiza");
    ibus_component_add_engine(
        component,
        ibus_engine_desc_new("paiza-mongolian", "Paiza Mongolian",
                             "Traditional Mongolian (vertical script)",
                             "mn", "AGPL-3.0", "Paiza",
                             PAIZA_DATA_DIR "/paiza-mn.png", "us"));
    ibus_bus_register_component(bus, component);
  }

  PAIZA_LOG_INFO(L"ibus engine up");
  ibus_main();
  return 0;
}
