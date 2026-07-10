// Copyright 2026 Paiza. All rights reserved.
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

#include "core/key_mapping_store.h"
#include "service/settings_store.h"
#include "ui/button.h"
#include "ui/dropdown.h"
#include "ui/label.h"
#include "ui/render_context.h"
#include "ui/window.h"

namespace paiza::service {

class SettingsWindow : public ui::Window {
 public:
  // `on_changed` fires after the user saves; the service re-applies the
  // settings to the engine.
  explicit SettingsWindow(std::function<void(const Settings&)> on_changed);

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

  Settings settings_;
  std::function<void(const Settings&)> on_changed_;
  KeyMappingStore mapping_store_;
  uint64_t shown_mapping_revision_ = 0;

  // Section card rectangles, painted in OnPaintBackground.
  ui::Rect general_card_;
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
  std::unique_ptr<ui::DropdownPopup> font_popup_;
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
  ui::Rect mapping_card_base_;
  int content_height_ = 0;
  int scroll_offset_ = 0;
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_SETTINGS_WINDOW_H_
