// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Settings dialog, hosted in the service process (design decision, spec
// item 14): the service owns the settings, no UI ever runs inside host
// application processes, and the global wake-up hotkey lives here too.
// Built with the project's own widget toolkit (dogfooding Label/Button).

#ifndef PAIZA_SERVICE_SETTINGS_WINDOW_H_
#define PAIZA_SERVICE_SETTINGS_WINDOW_H_

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/input_profile.h"
#include "core/profile_switch.h"
#include "core/key_mapping_store.h"
#include "core/user_dictionary.h"
#include "service/settings_store.h"
#include "ui/button.h"
#include "ui/checkbox.h"
#include "ui/dropdown.h"
#include "ui/label.h"
#include "ui/render_context.h"
#include "ui/window.h"

namespace paiza::service {

class SettingsWindow : public ui::Window {
 public:
  // `on_changed` fires after the user saves; the service re-applies the
  // settings to the engine. `user_dictionary` (may be null in previews)
  // receives TXT imports from the settings UI and must outlive the window.
  // `profiles` supplies both the list the dropdown offers and the active
  // profile's user dictionary for imports, so the dialog never caches a
  // dictionary that a profile change would invalidate. Must outlive the
  // window.
  SettingsWindow(std::function<void(const Settings&)> on_changed,
                 ProfileSwitch* profiles);

  // Creates (if needed), reloads current settings and shows centred.
  void ShowDialog();

 protected:
  bool OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                 LRESULT* result) override;
  // Card-based visual: light background with white rounded section cards.
  void OnPaintBackground(ui::RenderContext& context) override;
  void OnDpiChanged() override;

 private:
  static constexpr UINT_PTR kMappingRefreshTimerId = 2;

  void BuildWidgets();
  // Positions the scrollable content at the current scroll offset and
  // pins the footer (strip + Save/Close) to the bottom of the visible
  // client area; also syncs the native scrollbar. Called after build,
  // on scroll, and on DPI changes.
  void ApplyScrollLayout();
  void SetScrollOffset(int offset);
  // Re-applies every localized string (window caption, labels, buttons)
  // for the currently selected UI language.
  void ApplyStrings();
  void RefreshLabels();
  // Key mapping section: writes the default file if missing, then opens it
  // in the user's text editor. The store hot-reloads on save.
  void OpenMappingFileForEditing();
  void RefreshMappingPreview();
  // Candidate font picker: enumerates system fonts (cached) and shows the
  // dropdown below the font button.
  void OpenFontPicker();
  void ApplyCandidateFont(const std::wstring& font);
  void OpenProfilePicker();
  void ApplyProfile(const std::string& profile_id);
  void OpenInlinePicker();
  // Hides every open dropdown; called wherever the dialog changes state.
  void HideAllDropdowns();
  // File-picker -> UserDictionary::ImportFromFile -> result message box.
  void ImportUserDictionary();

  Settings settings_;
  std::function<void(const Settings&)> on_changed_;
  KeyMappingStore mapping_store_;
  uint64_t shown_mapping_revision_ = 0;

  // Section card rectangles, painted in OnPaintBackground.
  ui::Rect general_card_;
  ui::Rect soft_equiv_card_;
  ui::Rect mapping_card_;

  ui::Label* title_label_ = nullptr;
  ui::Label* subtitle_label_ = nullptr;
  ui::Label* general_section_label_ = nullptr;
  ui::Label* page_size_label_ = nullptr;
  ui::Label* page_size_value_ = nullptr;
  ui::Button* page_size_minus_ = nullptr;
  ui::Button* page_size_plus_ = nullptr;
  ui::Label* cloud_label_ = nullptr;
  ui::Button* cloud_toggle_ = nullptr;
  ui::Label* language_label_ = nullptr;
  ui::Button* language_button_ = nullptr;
  ui::Label* font_label_ = nullptr;
  ui::Button* font_button_ = nullptr;
  ui::Label* profile_label_ = nullptr;
  ui::Button* profile_button_ = nullptr;
  ui::Label* inline_label_ = nullptr;
  ui::Button* inline_button_ = nullptr;
  ui::Label* user_dict_label_ = nullptr;
  ui::Button* import_button_ = nullptr;
  ui::Label* soft_equiv_section_label_ = nullptr;
  ui::Label* soft_equiv_hint_label_ = nullptr;
  ui::Checkbox* equiv_ou_check_ = nullptr;
  ui::Checkbox* equiv_qv_check_ = nullptr;
  ui::Checkbox* equiv_hg_check_ = nullptr;
  ui::Checkbox* equiv_td_check_ = nullptr;
  ui::Checkbox* equiv_ew_check_ = nullptr;
  std::unique_ptr<ui::DropdownPopup> font_popup_;
  std::unique_ptr<ui::DropdownPopup> profile_popup_;
  std::unique_ptr<ui::DropdownPopup> inline_popup_;
  // Input profiles (docs/input-profiles.md); owned by the service.
  ProfileSwitch* profiles_ = nullptr;
  std::vector<ui::FontFamilyInfo> font_families_;  // Lazy cache.
  ui::Label* mapping_title_label_ = nullptr;
  ui::Label* mapping_preview_label_ = nullptr;
  ui::Label* mapping_hint_label_ = nullptr;
  ui::Label* mapping_error_label_ = nullptr;
  ui::Button* edit_mapping_button_ = nullptr;
  ui::Button* save_button_ = nullptr;
  ui::Button* close_button_ = nullptr;
  ui::Label* vertical_preview_ = nullptr;
  uint64_t shown_status_revision_ = 0;

  // Scrolling: the dialog never exceeds the work area; content scrolls
  // under a pinned footer. Content widgets keep their layout ("base")
  // bounds here and are shifted by scroll_offset_ (logical units).
  ui::Widget* footer_strip_ = nullptr;
  std::vector<std::pair<ui::Widget*, ui::Rect>> scrolled_widgets_;
  ui::Rect general_card_base_;
  ui::Rect soft_equiv_card_base_;
  ui::Rect mapping_card_base_;
  int content_height_ = 0;
  int scroll_offset_ = 0;
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_SETTINGS_WINDOW_H_
