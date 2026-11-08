// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// GTK3 settings dialog — the Linux equivalent of the Windows settings
// dialog and the macOS settings window, with exactly the same options:
// candidates per page, cloud candidates, UI language, candidate font,
// and the editable key mapping (live preview + hint). Localized through
// core/localization's UiStrings; persisted by the INI SettingsStore
// (hot-reloaded by the running engine within a second of Save).
//
// Also registered as the IBus component's <setup>, so GNOME Settings
// shows a settings button next to the input source.

#include <gtk/gtk.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include "common/paths.h"
#include "common/string_utils.h"
#include "core/font_stem_config.h"
#include "core/key_mapping_store.h"
#include "core/input_profile.h"
#include "core/localization.h"
#include "core/mapping_preview.h"
#include "core/user_dictionary.h"
#include "settings_store.h"

namespace {

using paiza::linux_ime::Settings;
using paiza::linux_ime::SettingsStore;

std::string Utf8(const wchar_t* wide) {
  return paiza::WideToUtf8(std::wstring(wide != nullptr ? wide : L""));
}

// All dialog state in one bundle for the GTK callbacks.
struct Dialog {
  Settings settings;
  paiza::KeyMappingStore mapping_store;
  guint64 shown_mapping_revision = 0;
  guint64 shown_status_revision = 0;

  GtkWidget* window = nullptr;
  GtkWidget* title_label = nullptr;
  GtkWidget* subtitle_label = nullptr;
  GtkWidget* general_section = nullptr;
  GtkWidget* page_size_label = nullptr;
  GtkWidget* page_size_spin = nullptr;
  GtkWidget* cloud_label = nullptr;
  GtkWidget* cloud_switch = nullptr;
  GtkWidget* language_label = nullptr;
  GtkWidget* language_combo = nullptr;
  GtkWidget* font_label = nullptr;
  GtkWidget* font_button = nullptr;
  GtkWidget* profile_label = nullptr;
  GtkWidget* profile_combo = nullptr;
  GtkWidget* inline_label = nullptr;
  GtkWidget* inline_combo = nullptr;
  // Input profiles offered by the dropdown (docs/input-profiles.md).
  paiza::InputProfileRegistry profiles;
  GtkWidget* user_dict_label = nullptr;
  GtkWidget* user_dict_button = nullptr;
  GtkWidget* soft_equiv_section = nullptr;
  GtkWidget* soft_equiv_hint = nullptr;
  GtkWidget* equiv_checks[5] = {};  // o/u, q/v, h/g, t/d, E/w.
  GtkWidget* mapping_section = nullptr;
  GtkWidget* edit_mapping_button = nullptr;
  GtkWidget* mapping_preview = nullptr;
  GtkWidget* mapping_hint = nullptr;
  GtkWidget* mapping_error = nullptr;
  GtkWidget* vertical_sample = nullptr;
  GtkWidget* save_button = nullptr;
  GtkWidget* close_button = nullptr;
  gulong language_changed_handler = 0;
};

// Vertical Mongolian sample: shapes the word horizontally with Pango
// (letters connect properly) and rotates it 90° clockwise into a
// top-to-bottom column, stem-centred via fontstems.txt — the same
// technique as the Windows/macOS candidate windows, and the exact
// drawing the v2 Linux candidate window will use.
gboolean DrawVerticalSample(GtkWidget* widget, cairo_t* cr, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  const std::string family = paiza::WideToUtf8(
      dialog->settings.candidate_font);
  constexpr double kFontSize = 24.0;

  PangoLayout* layout = pango_cairo_create_layout(cr);
  pango_layout_set_text(layout, "ᠮᠣᠩᠭᠣᠯ", -1);
  PangoFontDescription* description = pango_font_description_new();
  pango_font_description_set_family(description, family.c_str());
  pango_font_description_set_absolute_size(description,
                                           kFontSize * PANGO_SCALE);
  pango_layout_set_font_description(layout, description);
  pango_font_description_free(description);

  int width = 0;
  int height = 0;
  pango_layout_get_pixel_size(layout, &width, &height);

  GtkStyleContext* style = gtk_widget_get_style_context(widget);
  GdkRGBA color;
  gtk_style_context_get_color(style, gtk_style_context_get_state(style),
                              &color);
  color.alpha *= 0.55;  // Subtle, like the secondary label on macOS.
  gdk_cairo_set_source_rgba(cr, &color);

  const double view_width = gtk_widget_get_allocated_width(widget);
  const double view_height = gtk_widget_get_allocated_height(widget);
  const double cx = view_width / 2;
  // After the +90° rotation the layout's top edge lands at the transform
  // origin's x and its bottom edge `height` to the left; stem-calibrated
  // fonts put the stem font_size*ratio right of the left (bottom) edge.
  const std::optional<float> ratio =
      paiza::GetFontStemRatio(dialog->settings.candidate_font);
  const double origin_x = ratio.has_value()
                              ? cx - kFontSize * *ratio + height
                              : cx + height / 2.0;
  const double y_top = std::max(0.0, (view_height - width) / 2);

  cairo_save(cr);
  cairo_translate(cr, origin_x, y_top);
  cairo_rotate(cr, G_PI_2);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  g_object_unref(layout);
  return TRUE;
}

void RefreshMappingPreview(Dialog* dialog, bool force) {
  const paiza::KeyMapping& mapping = dialog->mapping_store.Get();
  if (force ||
      dialog->mapping_store.revision() != dialog->shown_mapping_revision) {
    dialog->shown_mapping_revision = dialog->mapping_store.revision();
    const std::string preview = paiza::WideToUtf8(paiza::BuildMappingPreview(
        mapping, paiza::GetStrings(dialog->settings.ui_language)));
    gchar* escaped = g_markup_escape_text(preview.c_str(), -1);
    gchar* markup = g_strdup_printf("<tt>%s</tt>", escaped);
    gtk_label_set_markup(GTK_LABEL(dialog->mapping_preview), markup);
    g_free(markup);
    g_free(escaped);
  }

  // Validation status: a rejected keymap.txt edit shows the localized
  // error (the store already restored the file to the previous version).
  if (force || dialog->mapping_store.status_revision() !=
                   dialog->shown_status_revision) {
    dialog->shown_status_revision = dialog->mapping_store.status_revision();
    const paiza::KeyMapFileStatus& status =
        dialog->mapping_store.file_status();
    if (status.valid) {
      gtk_widget_hide(dialog->mapping_error);
    } else {
      std::wstring message = paiza::GetStrings(dialog->settings.ui_language)
                                 .mapping_error_restored;
      const size_t placeholder = message.find(L"%ls");
      if (placeholder != std::wstring::npos) {
        message.replace(placeholder, 3,
                        std::to_wstring(status.error_line));
      }
      const std::string utf8 = paiza::WideToUtf8(message);
      gchar* escaped = g_markup_escape_text(utf8.c_str(), -1);
      gchar* markup = g_strdup_printf(
          "<span foreground='#c01c28'>%s</span>", escaped);
      gtk_label_set_markup(GTK_LABEL(dialog->mapping_error), markup);
      g_free(markup);
      g_free(escaped);
      gtk_widget_show(dialog->mapping_error);
    }
  }
}

// Re-applies every localized string for the currently selected language.
void ApplyStrings(Dialog* dialog) {
  const paiza::UiStrings& s = paiza::GetStrings(dialog->settings.ui_language);
  gtk_window_set_title(GTK_WINDOW(dialog->window),
                       Utf8(s.window_title).c_str());

  auto set_bold = [](GtkWidget* label, const std::string& text,
                     const char* size) {
    gchar* escaped = g_markup_escape_text(text.c_str(), -1);
    gchar* markup = g_strdup_printf(
        "<span weight='bold' size='%s'>%s</span>", size, escaped);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    g_free(escaped);
  };
  set_bold(dialog->title_label, Utf8(s.app_title), "x-large");
  gtk_label_set_text(GTK_LABEL(dialog->subtitle_label),
                     Utf8(s.subtitle).c_str());
  set_bold(dialog->general_section, Utf8(s.section_general), "small");
  gtk_label_set_text(GTK_LABEL(dialog->page_size_label),
                     Utf8(s.candidates_per_page).c_str());
  gtk_label_set_text(GTK_LABEL(dialog->cloud_label),
                     Utf8(s.cloud_candidates).c_str());
  gtk_label_set_text(GTK_LABEL(dialog->language_label),
                     Utf8(s.language_label).c_str());
  gtk_label_set_text(GTK_LABEL(dialog->font_label),
                     Utf8(s.candidate_font_label).c_str());
  gtk_label_set_text(GTK_LABEL(dialog->profile_label),
                     Utf8(s.profile_label).c_str());
  gtk_label_set_text(GTK_LABEL(dialog->inline_label),
                     Utf8(s.inline_composition_label).c_str());
  // Order matches the InlineComposition enum, so the index is the value.
  gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(dialog->inline_combo));
  for (const wchar_t* text : {s.inline_full, s.inline_placeholder,
                              s.inline_hidden}) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->inline_combo),
                                   Utf8(text).c_str());
  }
  gtk_combo_box_set_active(
      GTK_COMBO_BOX(dialog->inline_combo),
      static_cast<gint>(dialog->settings.inline_composition));
  gtk_label_set_text(GTK_LABEL(dialog->user_dict_label),
                     Utf8(s.user_dict_label).c_str());
  gtk_button_set_label(GTK_BUTTON(dialog->user_dict_button),
                       Utf8(s.user_dict_import).c_str());
  set_bold(dialog->soft_equiv_section, Utf8(s.section_soft_equiv), "small");
  gtk_label_set_text(GTK_LABEL(dialog->soft_equiv_hint),
                     Utf8(s.soft_equiv_hint).c_str());
  set_bold(dialog->mapping_section, Utf8(s.section_key_mapping), "small");
  gtk_button_set_label(GTK_BUTTON(dialog->edit_mapping_button),
                       Utf8(s.edit_mapping_file).c_str());
  gtk_button_set_label(GTK_BUTTON(dialog->save_button),
                       Utf8(s.save).c_str());
  gtk_button_set_label(GTK_BUTTON(dialog->close_button),
                       Utf8(s.close).c_str());

  // Localized hint with the real keymap.txt path substituted for "%ls".
  std::wstring hint = s.mapping_hint_generic;
  const size_t placeholder = hint.find(L"%ls");
  if (placeholder != std::wstring::npos) {
    hint.replace(placeholder, 3, paiza::KeyMappingFilePath());
  }
  gtk_label_set_text(GTK_LABEL(dialog->mapping_hint),
                     paiza::WideToUtf8(hint).c_str());

  RefreshMappingPreview(dialog, /*force=*/true);
}

// Callbacks ------------------------------------------------------------------

void OnPageSizeChanged(GtkSpinButton* spin, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  dialog->settings.candidates_per_page =
      static_cast<uint32_t>(gtk_spin_button_get_value_as_int(spin));
}

gboolean OnCloudToggled(GtkSwitch* toggle, gboolean state, gpointer data) {
  (void)toggle;
  Dialog* dialog = static_cast<Dialog*>(data);
  dialog->settings.cloud_source_enabled = state != FALSE;
  return FALSE;  // Let the switch update its visual state.
}

void OnLanguageChanged(GtkComboBox* combo, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  dialog->settings.ui_language = paiza::UiLanguageFromValue(
      static_cast<uint32_t>(gtk_combo_box_get_active(combo)));
  ApplyStrings(dialog);
}

void OnInlineCompositionChanged(GtkComboBox* combo, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  const gint index = gtk_combo_box_get_active(combo);
  if (index >= 0) {
    dialog->settings.inline_composition =
        paiza::InlineCompositionFromValue(static_cast<uint32_t>(index));
  }
}

void OnProfileChanged(GtkComboBox* combo, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  const gint index = gtk_combo_box_get_active(combo);
  const auto& all = dialog->profiles.All();
  if (index >= 0 && static_cast<size_t>(index) < all.size()) {
    dialog->settings.active_profile = all[index].id;
  }
}

void OnFontSet(GtkFontButton* button, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  gchar* font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));
  if (font == nullptr) return;
  PangoFontDescription* description =
      pango_font_description_from_string(font);
  const char* family = pango_font_description_get_family(description);
  if (family != nullptr && *family != '\0') {
    dialog->settings.candidate_font = paiza::Utf8ToWide(family);
  }
  pango_font_description_free(description);
  g_free(font);
  gtk_widget_queue_draw(dialog->vertical_sample);
}

void OnEquivToggled(GtkToggleButton* toggle, gpointer data) {
  Dialog* dialog = static_cast<Dialog*>(data);
  const bool on = gtk_toggle_button_get_active(toggle) != FALSE;
  const int index = GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(toggle), "equiv-index"));
  switch (index) {
    case 0: dialog->settings.equiv_ou = on; break;
    case 1: dialog->settings.equiv_qv = on; break;
    case 2: dialog->settings.equiv_hg = on; break;
    case 3: dialog->settings.equiv_td = on; break;
    case 4: dialog->settings.equiv_ew = on; break;
    default: break;
  }
}

// Imports a user TXT (latin<TAB>mongolian[<TAB>freq]) into the shared
// user.db. The dialog is a separate process from the engine, so the
// import is written to the SQLite file and the running engine picks the
// entries up on its next start (ibus restart / next login).
void OnImportUserDict(GtkButton* button, gpointer data) {
  (void)button;
  Dialog* dialog = static_cast<Dialog*>(data);
  const paiza::UiStrings& s = paiza::GetStrings(dialog->settings.ui_language);

  GtkWidget* chooser = gtk_file_chooser_dialog_new(
      Utf8(s.user_dict_import).c_str(), GTK_WINDOW(dialog->window),
      GTK_FILE_CHOOSER_ACTION_OPEN, Utf8(s.close).c_str(),
      GTK_RESPONSE_CANCEL, Utf8(s.user_dict_import).c_str(),
      GTK_RESPONSE_ACCEPT, nullptr);
  GtkFileFilter* filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Text files (*.txt)");
  gtk_file_filter_add_pattern(filter, "*.txt");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

  const gint response = gtk_dialog_run(GTK_DIALOG(chooser));
  gchar* filename = nullptr;
  if (response == GTK_RESPONSE_ACCEPT) {
    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
  }
  gtk_widget_destroy(chooser);
  if (filename == nullptr) return;  // Cancelled.

  paiza::ImportResult result;
  {
    // Own SQLite connection on the same db the engine reads (WAL allows
    // concurrent access); the destructor flushes synchronously.
    // The import must land in the profile that is currently selected, not
    // a hardcoded file (docs/input-profiles.md).
    const paiza::InputProfile* profile =
        dialog->profiles.Find(dialog->settings.active_profile);
    const std::wstring file =
        (profile != nullptr && !profile->user_db.empty()) ? profile->user_db
                                                          : L"user.db";
    paiza::UserDictionary dictionary(
        (std::filesystem::path(paiza::UserConfigDirectory()) / file)
            .wstring());
    result = dictionary.ImportFromFile(paiza::Utf8ToWide(filename));
    dictionary.Flush();
  }
  g_free(filename);

  GtkWidget* message;
  if (!result.opened) {
    message = gtk_message_dialog_new(
        GTK_WINDOW(dialog->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK, "%s", Utf8(s.user_dict_import_failed).c_str());
  } else {
    message = gtk_message_dialog_new(
        GTK_WINDOW(dialog->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, Utf8(s.user_dict_import_result).c_str(),
        result.imported, result.skipped);
  }
  gtk_window_set_title(GTK_WINDOW(message), Utf8(s.user_dict_label).c_str());
  gtk_dialog_run(GTK_DIALOG(message));
  gtk_widget_destroy(message);
}

void OnEditMapping(GtkButton* button, gpointer data) {
  (void)button;
  Dialog* dialog = static_cast<Dialog*>(data);
  const std::wstring path = paiza::KeyMappingFilePath();
  if (path.empty()) return;
  const std::string utf8 = paiza::WideToUtf8(path);
  // First use: materialise the default mapping so the user edits a
  // complete, commented file rather than starting from scratch.
  if (!g_file_test(utf8.c_str(), G_FILE_TEST_EXISTS)) {
    dialog->mapping_store.Get().SaveToFile(path);
  }
  gchar* command = g_strdup_printf("xdg-open \"%s\"", utf8.c_str());
  g_spawn_command_line_async(command, nullptr);
  g_free(command);
}

void OnSave(GtkButton* button, gpointer data) {
  (void)button;
  Dialog* dialog = static_cast<Dialog*>(data);
  SettingsStore::Save(dialog->settings);
  gtk_main_quit();
}

void OnClose(GtkButton* button, gpointer data) {
  (void)button;
  (void)data;
  gtk_main_quit();
}

// Polls for external edits of the mapping file while visible (mirrors
// the Windows dialog's refresh timer).
gboolean OnMappingRefreshTick(gpointer data) {
  RefreshMappingPreview(static_cast<Dialog*>(data), /*force=*/false);
  return G_SOURCE_CONTINUE;
}

GtkWidget* MakeLeftLabel() {
  GtkWidget* label = gtk_label_new("");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  return label;
}

void BuildDialog(Dialog* dialog) {
  dialog->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(dialog->window), 560, -1);
  gtk_window_set_position(GTK_WINDOW(dialog->window), GTK_WIN_POS_CENTER);
  gtk_container_set_border_width(GTK_CONTAINER(dialog->window), 18);
  g_signal_connect(dialog->window, "destroy", G_CALLBACK(gtk_main_quit),
                   nullptr);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_add(GTK_CONTAINER(dialog->window), root);

  // Everything except the footer lives in a scrolled area so the dialog
  // never exceeds the monitor's work area: on small screens the content
  // scrolls while Save/Close stay pinned and visible at the bottom.
  GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(
      GTK_SCROLLED_WINDOW(scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_width(
      GTK_SCROLLED_WINDOW(scroller), TRUE);
  GdkDisplay* display = gdk_display_get_default();
  GdkMonitor* monitor =
      display != nullptr ? gdk_display_get_primary_monitor(display) : nullptr;
  if (monitor == nullptr && display != nullptr) {
    monitor = gdk_display_get_monitor(display, 0);
  }
  GdkRectangle work = {0, 0, 1920, 1080};
  if (monitor != nullptr) gdk_monitor_get_workarea(monitor, &work);
  // Leave room for the window frame, dialog margins and the footer row.
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller),
                                             std::max(320, work.height - 170));
  gtk_box_pack_start(GTK_BOX(root), scroller, TRUE, TRUE, 0);

  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_add(GTK_CONTAINER(scroller), content);

  // ---- Header ----------------------------------------------------------
  dialog->title_label = MakeLeftLabel();
  dialog->subtitle_label = MakeLeftLabel();
  gtk_box_pack_start(GTK_BOX(content), dialog->title_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(content), dialog->subtitle_label, FALSE, FALSE,
                     0);

  // ---- Card 1: General (a framed grid) ---------------------------------
  GtkWidget* general_frame = gtk_frame_new(nullptr);
  GtkWidget* general_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(general_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(general_grid), 12);
  gtk_container_set_border_width(GTK_CONTAINER(general_grid), 14);
  gtk_container_add(GTK_CONTAINER(general_frame), general_grid);
  gtk_box_pack_start(GTK_BOX(content), general_frame, FALSE, FALSE, 6);

  dialog->general_section = MakeLeftLabel();
  gtk_grid_attach(GTK_GRID(general_grid), dialog->general_section, 0, 0, 2,
                  1);

  dialog->page_size_label = MakeLeftLabel();
  gtk_widget_set_hexpand(dialog->page_size_label, TRUE);
  dialog->page_size_spin = gtk_spin_button_new_with_range(1, 9, 1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->page_size_label, 0, 1, 1,
                  1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->page_size_spin, 1, 1, 1,
                  1);

  dialog->cloud_label = MakeLeftLabel();
  dialog->cloud_switch = gtk_switch_new();
  gtk_widget_set_halign(dialog->cloud_switch, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->cloud_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->cloud_switch, 1, 2, 1, 1);

  dialog->language_label = MakeLeftLabel();
  dialog->language_combo = gtk_combo_box_text_new();
  for (uint32_t i = 0; i < paiza::kUiLanguageCount; ++i) {
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(dialog->language_combo),
        Utf8(paiza::GetStrings(paiza::UiLanguageFromValue(i)).language_name)
            .c_str());
  }
  gtk_grid_attach(GTK_GRID(general_grid), dialog->language_label, 0, 3, 1,
                  1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->language_combo, 1, 3, 1,
                  1);

  dialog->font_label = MakeLeftLabel();
  dialog->font_button = gtk_font_button_new();
  gtk_grid_attach(GTK_GRID(general_grid), dialog->font_label, 0, 4, 1, 1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->font_button, 1, 4, 1, 1);

  // Row: input profile (text encoding + candidate engine). Profiles are
  // installed next to the other data; a missing directory falls back to the
  // built-in Unicode profile.
  dialog->profile_label = MakeLeftLabel();
  dialog->profile_combo = gtk_combo_box_text_new();
  dialog->profiles.LoadFromDirectory(
      paiza::Utf8ToWide(std::string(PAIZA_DATA_DIR) + "/profiles"));
  dialog->profiles.EnsureDefault();
  for (const paiza::InputProfile& profile : dialog->profiles.All()) {
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(dialog->profile_combo),
        paiza::WideToUtf8(profile.display_name).c_str());
  }
  gtk_grid_attach(GTK_GRID(general_grid), dialog->profile_label, 0, 5, 1, 1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->profile_combo, 1, 5, 1, 1);

  dialog->inline_label = MakeLeftLabel();
  dialog->inline_combo = gtk_combo_box_text_new();
  gtk_grid_attach(GTK_GRID(general_grid), dialog->inline_label, 0, 6, 1, 1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->inline_combo, 1, 6, 1, 1);

  dialog->user_dict_label = MakeLeftLabel();
  dialog->user_dict_button = gtk_button_new_with_label("");
  gtk_widget_set_halign(dialog->user_dict_button, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->user_dict_label, 0, 7, 1,
                  1);
  gtk_grid_attach(GTK_GRID(general_grid), dialog->user_dict_button, 1, 7, 1,
                  1);

  // ---- Card 2: Soft equivalence (per-pair recall toggles) --------------
  GtkWidget* equiv_frame = gtk_frame_new(nullptr);
  GtkWidget* equiv_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(equiv_box), 14);
  gtk_container_add(GTK_CONTAINER(equiv_frame), equiv_box);
  gtk_box_pack_start(GTK_BOX(content), equiv_frame, FALSE, FALSE, 6);

  dialog->soft_equiv_section = MakeLeftLabel();
  gtk_box_pack_start(GTK_BOX(equiv_box), dialog->soft_equiv_section, FALSE,
                     FALSE, 0);
  dialog->soft_equiv_hint = MakeLeftLabel();
  gtk_label_set_line_wrap(GTK_LABEL(dialog->soft_equiv_hint), TRUE);
  gtk_box_pack_start(GTK_BOX(equiv_box), dialog->soft_equiv_hint, FALSE,
                     FALSE, 0);

  GtkWidget* checks_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  const char* const captions[5] = {"o / u", "q / v", "h / g", "t / d",
                                    "E / w"};
  for (int i = 0; i < 5; ++i) {
    dialog->equiv_checks[i] = gtk_check_button_new_with_label(captions[i]);
    g_object_set_data(G_OBJECT(dialog->equiv_checks[i]), "equiv-index",
                      GINT_TO_POINTER(i));
    gtk_box_pack_start(GTK_BOX(checks_row), dialog->equiv_checks[i], FALSE,
                       FALSE, 0);
  }
  gtk_box_pack_start(GTK_BOX(equiv_box), checks_row, FALSE, FALSE, 0);

  // ---- Card 3: Key mapping ---------------------------------------------
  GtkWidget* mapping_frame = gtk_frame_new(nullptr);
  GtkWidget* mapping_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(mapping_grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(mapping_grid), 12);
  gtk_container_set_border_width(GTK_CONTAINER(mapping_grid), 14);
  gtk_container_add(GTK_CONTAINER(mapping_frame), mapping_grid);
  gtk_box_pack_start(GTK_BOX(content), mapping_frame, TRUE, TRUE, 6);

  dialog->mapping_section = MakeLeftLabel();
  gtk_widget_set_hexpand(dialog->mapping_section, TRUE);
  dialog->edit_mapping_button = gtk_button_new_with_label("");
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->mapping_section, 0, 0, 1,
                  1);
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->edit_mapping_button, 1, 0,
                  1, 1);

  dialog->mapping_preview = MakeLeftLabel();
  gtk_label_set_line_wrap(GTK_LABEL(dialog->mapping_preview), TRUE);
  gtk_widget_set_valign(dialog->mapping_preview, GTK_ALIGN_START);
  gtk_widget_set_hexpand(dialog->mapping_preview, TRUE);
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->mapping_preview, 0, 1, 1,
                  1);

  // Vertical script sample on the card's right edge (same drawing the v2
  // candidate window will use).
  dialog->vertical_sample = gtk_drawing_area_new();
  gtk_widget_set_size_request(dialog->vertical_sample, 44, 190);
  g_signal_connect(dialog->vertical_sample, "draw",
                   G_CALLBACK(DrawVerticalSample), dialog);
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->vertical_sample, 1, 1, 1,
                  1);

  dialog->mapping_hint = MakeLeftLabel();
  gtk_label_set_line_wrap(GTK_LABEL(dialog->mapping_hint), TRUE);
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->mapping_hint, 0, 2, 2, 1);

  // Validation error for rejected keymap.txt edits; hidden while valid
  // (no-show-all keeps gtk_widget_show_all from revealing it).
  dialog->mapping_error = MakeLeftLabel();
  gtk_label_set_line_wrap(GTK_LABEL(dialog->mapping_error), TRUE);
  gtk_widget_set_no_show_all(dialog->mapping_error, TRUE);
  gtk_grid_attach(GTK_GRID(mapping_grid), dialog->mapping_error, 0, 3, 2, 1);

  // ---- Footer ----------------------------------------------------------
  GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(footer, GTK_ALIGN_END);
  dialog->close_button = gtk_button_new_with_label("");
  dialog->save_button = gtk_button_new_with_label("");
  gtk_style_context_add_class(
      gtk_widget_get_style_context(dialog->save_button),
      "suggested-action");
  gtk_box_pack_start(GTK_BOX(footer), dialog->close_button, FALSE, FALSE,
                     0);
  gtk_box_pack_start(GTK_BOX(footer), dialog->save_button, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(root), footer, FALSE, FALSE, 0);

  // ---- Signals + initial values -----------------------------------------
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(dialog->page_size_spin),
                            dialog->settings.candidates_per_page);
  gtk_switch_set_active(GTK_SWITCH(dialog->cloud_switch),
                        dialog->settings.cloud_source_enabled);
  gtk_combo_box_set_active(
      GTK_COMBO_BOX(dialog->language_combo),
      static_cast<gint>(dialog->settings.ui_language));
  {
    const auto& all = dialog->profiles.All();
    gint selected = 0;
    for (size_t i = 0; i < all.size(); ++i) {
      if (all[i].id == dialog->settings.active_profile) {
        selected = static_cast<gint>(i);
      }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->profile_combo), selected);
  }
  const std::string font_spec =
      paiza::WideToUtf8(dialog->settings.candidate_font) + " 16";
  gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog->font_button),
                            font_spec.c_str());

  const bool equiv_enabled[5] = {
      dialog->settings.equiv_ou, dialog->settings.equiv_qv,
      dialog->settings.equiv_hg, dialog->settings.equiv_td,
      dialog->settings.equiv_ew};
  for (int i = 0; i < 5; ++i) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->equiv_checks[i]),
                                 equiv_enabled[i]);
    g_signal_connect(dialog->equiv_checks[i], "toggled",
                     G_CALLBACK(OnEquivToggled), dialog);
  }

  g_signal_connect(dialog->user_dict_button, "clicked",
                   G_CALLBACK(OnImportUserDict), dialog);
  g_signal_connect(dialog->page_size_spin, "value-changed",
                   G_CALLBACK(OnPageSizeChanged), dialog);
  g_signal_connect(dialog->cloud_switch, "state-set",
                   G_CALLBACK(OnCloudToggled), dialog);
  g_signal_connect(dialog->language_combo, "changed",
                   G_CALLBACK(OnLanguageChanged), dialog);
  g_signal_connect(dialog->profile_combo, "changed",
                   G_CALLBACK(OnProfileChanged), dialog);
  g_signal_connect(dialog->inline_combo, "changed",
                   G_CALLBACK(OnInlineCompositionChanged), dialog);
  g_signal_connect(dialog->font_button, "font-set", G_CALLBACK(OnFontSet),
                   dialog);
  g_signal_connect(dialog->edit_mapping_button, "clicked",
                   G_CALLBACK(OnEditMapping), dialog);
  g_signal_connect(dialog->save_button, "clicked", G_CALLBACK(OnSave),
                   dialog);
  g_signal_connect(dialog->close_button, "clicked", G_CALLBACK(OnClose),
                   dialog);
  g_timeout_add_seconds(1, OnMappingRefreshTick, dialog);
}

}  // namespace

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);

  Dialog dialog;
  dialog.settings = SettingsStore::Load();
  BuildDialog(&dialog);
  ApplyStrings(&dialog);

  gtk_widget_show_all(dialog.window);
  gtk_main();
  return 0;
}
