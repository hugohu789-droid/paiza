// Copyright 2026 Paiza. All rights reserved.

#include "service/settings_window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>

#include "core/mapping_preview.h"

namespace paiza::service {
namespace {

// All layout constants below are CLIENT-area coordinates; the outer window
// size is derived with AdjustWindowRectEx (fix: the caption bar used to be
// subtracted from the content, clipping the bottom controls).
constexpr int kClientWidth = 520;
constexpr int kMargin = 20;
constexpr int kCardPadding = 16;
constexpr int kRowHeight = 30;
// Fixed footer strip hosting Save/Close: always visible; the content
// above scrolls when the dialog is clamped to a small work area.
constexpr int kFooterHeight = 54;
constexpr int kWheelStep = 48;  // Logical px per wheel notch.

constexpr uint32_t kColorWindowBg = 0xFFF3F4F6;
constexpr uint32_t kColorCardBg = 0xFFFFFFFF;
constexpr uint32_t kColorCardBorder = 0xFFE3E5E8;
constexpr uint32_t kColorTitle = 0xFF202124;
constexpr uint32_t kColorSubtitle = 0xFF80868B;
constexpr uint32_t kColorSection = 0xFF5F6368;
constexpr uint32_t kColorBody = 0xFF3C4043;
constexpr uint32_t kColorPreview = 0xFF3C4043;
constexpr uint32_t kColorHint = 0xFF9AA0A6;
constexpr uint32_t kColorError = 0xFFC5221F;

// Opaque strip behind the pinned Save/Close buttons; painted after the
// (scrolled) content widgets so nothing shows through underneath, and
// hit-tested as a plain rectangle so clicks never reach content that
// scrolled under it.
class FooterStrip : public ui::Widget {
 public:
  void OnPaint(ui::RenderContext& context) override {
    context.FillRect(bounds(), kColorWindowBg);
    context.FillRect({bounds().x, bounds().y, bounds().width, 1},
                     kColorCardBorder);
  }
};

}  // namespace

SettingsWindow::SettingsWindow(
    std::function<void(const Settings&)> on_changed)
    : on_changed_(std::move(on_changed)) {}

void SettingsWindow::ShowDialog() {
  settings_ = SettingsStore::Load();
  if (hwnd() == nullptr) {
    // Widgets first: they need no HWND and their layout determines
    // content_height_, which the window size derives from.
    BuildWidgets();

    CreateParams params;
    params.title = L"Paiza Mongolian IME Settings";
    // A regular (activatable) top-level window, unlike the IME popups.
    // WS_VSCROLL: the scrollbar auto-hides while the content fits.
    params.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    params.ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;

    // Compute the OUTER size in physical pixels from the logical client
    // size (the service process is per-monitor-V2 DPI aware, so all
    // Win32 coordinates here are physical), clamp it to the work area —
    // the content then scrolls under the pinned footer — and centre it.
    const UINT dpi = ::GetDpiForSystem();
    const float scale = dpi / 96.0f;
    const int desired_client_height = content_height_ + kFooterHeight;
    RECT frame = {0, 0, static_cast<LONG>(kClientWidth * scale + 0.5f),
                  static_cast<LONG>(desired_client_height * scale + 0.5f)};
    ::AdjustWindowRectExForDpi(&frame, params.style, FALSE, params.ex_style,
                               dpi);
    int outer_width = frame.right - frame.left;
    int outer_height = frame.bottom - frame.top;

    RECT work = {};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (outer_height > work.bottom - work.top) {
      outer_height = work.bottom - work.top;
      // The scrollbar will be visible; AdjustWindowRectEx does not
      // account for it, so widen the window to keep the full logical
      // client width for the content.
      outer_width += ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
    }
    params.bounds = {
        work.left + ((work.right - work.left) - outer_width) / 2,
        work.top + ((work.bottom - work.top) - outer_height) / 2,
        outer_width, outer_height};
    if (!Create(params)) return;
    ApplyScrollLayout();
  }
  ApplyStrings();
  RefreshLabels();
  RefreshMappingPreview();
  // Poll for external edits of the mapping file while visible.
  ::SetTimer(hwnd(), kMappingRefreshTimerId, 1000, nullptr);
  ::ShowWindow(hwnd(), SW_SHOW);
  if (!::IsWindowVisible(hwnd())) {
    // A process's first ShowWindow call can be overridden by the
    // launcher's STARTUPINFO (STARTF_USESHOWWINDOW, e.g. started from a
    // hidden shell); the second call always honours its parameter.
    ::ShowWindow(hwnd(), SW_SHOW);
  }
  ::SetForegroundWindow(hwnd());
  Invalidate();
}

void SettingsWindow::BuildWidgets() {
  const int content_width = kClientWidth - 2 * kMargin;
  int y = kMargin;

  // Captions and label texts are assigned in ApplyStrings() (localized).
  // ---- Header -------------------------------------------------------------
  auto title = std::make_unique<ui::Label>();
  title->style().font_family = L"Segoe UI";
  title->style().font_size = 20.0f;
  title->style().bold = true;
  title->style().color = kColorTitle;
  title->set_bounds({kMargin, y, content_width, 28});
  title_label_ = AddWidget(std::move(title));
  y += 28;

  auto subtitle = std::make_unique<ui::Label>();
  subtitle->style().font_family = L"Segoe UI";
  subtitle->style().font_size = 12.0f;
  subtitle->style().color = kColorSubtitle;
  subtitle->set_bounds({kMargin, y, content_width, 18});
  subtitle_label_ = AddWidget(std::move(subtitle));
  y += 18 + 14;

  // ---- Card 1: General ------------------------------------------------
  general_card_ = {kMargin, y, content_width, 192};
  const int card1_x = general_card_.x + kCardPadding;
  int row_y = general_card_.y + 12;

  auto section = std::make_unique<ui::Label>();
  section->style().font_family = L"Segoe UI";
  section->style().font_size = 11.0f;
  section->style().bold = true;
  section->style().color = kColorSection;
  section->set_bounds({card1_x, row_y, 200, 16});
  general_section_label_ = AddWidget(std::move(section));
  row_y += 16 + 10;

  // Row: candidates per page  [−] 7 [+]
  auto page_label = std::make_unique<ui::Label>();
  page_label->style().font_family = L"Segoe UI";
  page_label->style().font_size = 14.0f;
  page_label->style().color = kColorBody;
  page_label->style().center_vertical = true;
  page_label->set_bounds({card1_x, row_y, 230, kRowHeight});
  page_size_label_ = AddWidget(std::move(page_label));

  const int controls_right = general_card_.right() - kCardPadding;
  auto plus = std::make_unique<ui::Button>(L"+");
  plus->style().font_family = L"Segoe UI";
  plus->set_bounds({controls_right - 32, row_y, 32, kRowHeight});
  plus->SetOnClick([this] {
    settings_.candidates_per_page = std::min<uint32_t>(
        9, settings_.candidates_per_page + 1);
    RefreshLabels();
  });
  page_size_plus_ = AddWidget(std::move(plus));

  auto value = std::make_unique<ui::Label>();
  value->style().font_family = L"Segoe UI";
  value->style().font_size = 15.0f;
  value->style().bold = true;
  value->style().color = kColorTitle;
  value->style().center_horizontal = true;
  value->style().center_vertical = true;
  value->set_bounds({controls_right - 32 - 40, row_y, 40, kRowHeight});
  page_size_value_ = AddWidget(std::move(value));

  auto minus = std::make_unique<ui::Button>(L"−");
  minus->style().font_family = L"Segoe UI";
  minus->set_bounds({controls_right - 32 - 40 - 32, row_y, 32, kRowHeight});
  minus->SetOnClick([this] {
    settings_.candidates_per_page = std::max<uint32_t>(
        1, settings_.candidates_per_page - 1);
    RefreshLabels();
  });
  page_size_minus_ = AddWidget(std::move(minus));
  row_y += kRowHeight + 8;

  // Row: cloud source toggle.
  auto cloud_label = std::make_unique<ui::Label>();
  cloud_label->style().font_family = L"Segoe UI";
  cloud_label->style().font_size = 14.0f;
  cloud_label->style().color = kColorBody;
  cloud_label->style().center_vertical = true;
  cloud_label->set_bounds({card1_x, row_y, 230, kRowHeight});
  cloud_label_ = AddWidget(std::move(cloud_label));

  auto cloud_toggle = std::make_unique<ui::Button>();
  cloud_toggle->style().font_family = L"Segoe UI";
  cloud_toggle->set_bounds({controls_right - 96, row_y, 96, kRowHeight});
  cloud_toggle->SetOnClick([this] {
    settings_.cloud_source_enabled = !settings_.cloud_source_enabled;
    RefreshLabels();
  });
  cloud_toggle_ = AddWidget(std::move(cloud_toggle));
  row_y += kRowHeight + 8;

  // Row: UI language (cycles through the supported languages; the whole
  // dialog re-labels immediately, persisted on Save).
  auto language_label = std::make_unique<ui::Label>();
  language_label->style().font_family = L"Segoe UI";
  language_label->style().font_size = 14.0f;
  language_label->style().color = kColorBody;
  language_label->style().center_vertical = true;
  language_label->set_bounds({card1_x, row_y, 230, kRowHeight});
  language_label_ = AddWidget(std::move(language_label));

  auto language_button = std::make_unique<ui::Button>();
  language_button->style().font_family = L"Segoe UI";
  language_button->set_bounds(
      {controls_right - 128, row_y, 128, kRowHeight});
  language_button->SetOnClick([this] {
    settings_.ui_language = UiLanguageFromValue(
        (static_cast<uint32_t>(settings_.ui_language) + 1) %
        kUiLanguageCount);
    ApplyStrings();
    RefreshLabels();
  });
  language_button_ = AddWidget(std::move(language_button));
  row_y += kRowHeight + 8;

  // Row: candidate window font (dropdown of installed font families,
  // Mongolian-capable fonts listed first).
  auto font_label = std::make_unique<ui::Label>();
  font_label->style().font_family = L"Segoe UI";
  font_label->style().font_size = 14.0f;
  font_label->style().color = kColorBody;
  font_label->style().center_vertical = true;
  font_label->set_bounds({card1_x, row_y, 200, kRowHeight});
  font_label_ = AddWidget(std::move(font_label));

  auto font_button = std::make_unique<ui::Button>();
  font_button->style().font_family = L"Segoe UI";
  font_button->set_bounds({controls_right - 230, row_y, 230, kRowHeight});
  font_button->SetOnClick([this] { OpenFontPicker(); });
  font_button_ = AddWidget(std::move(font_button));
  y = general_card_.bottom() + 14;

  // ---- Card 2: Key mapping ----------------------------------------------
  mapping_card_ = {kMargin, y, content_width, 286};
  const int card2_x = mapping_card_.x + kCardPadding;
  row_y = mapping_card_.y + 12;

  auto mapping_title = std::make_unique<ui::Label>();
  mapping_title->style().font_family = L"Segoe UI";
  mapping_title->style().font_size = 11.0f;
  mapping_title->style().bold = true;
  mapping_title->style().color = kColorSection;
  mapping_title->style().center_vertical = true;
  mapping_title->set_bounds(
      {card2_x, row_y, content_width - 2 * kCardPadding - 185, 26});
  mapping_title_label_ = AddWidget(std::move(mapping_title));

  // Wide enough for the longest localized caption (Cyrillic).
  auto edit_mapping = std::make_unique<ui::Button>();
  edit_mapping->style().font_family = L"Segoe UI";
  edit_mapping->set_bounds(
      {mapping_card_.right() - kCardPadding - 175, row_y, 175, 26});
  edit_mapping->SetOnClick([this] { OpenMappingFileForEditing(); });
  edit_mapping_button_ = AddWidget(std::move(edit_mapping));
  row_y += 26 + 10;

  auto mapping_preview = std::make_unique<ui::Label>();
  mapping_preview->style().font_size = 15.0f;
  mapping_preview->style().color = kColorPreview;
  mapping_preview->set_bounds(
      {card2_x, row_y, content_width - 2 * kCardPadding - 52, 186});
  mapping_preview_label_ = AddWidget(std::move(mapping_preview));

  // Vertical script sample on the card's right edge (also exercises the
  // vertical Label rendering path).
  auto preview = std::make_unique<ui::Label>(
      L"\x182E\x1823\x1829\x182D\x1823\x182F");  // "Monggol"
  preview->style().orientation = ui::TextOrientation::kVertical;
  preview->style().font_size = 24.0f;
  preview->style().color = kColorSubtitle;
  preview->set_bounds(
      {mapping_card_.right() - kCardPadding - 36, row_y, 36, 186});
  vertical_preview_ = AddWidget(std::move(preview));
  row_y += 186 + 6;

  auto hint = std::make_unique<ui::Label>();
  hint->style().font_family = L"Segoe UI";
  hint->style().font_size = 11.0f;
  hint->style().color = kColorHint;
  hint->set_bounds(
      {card2_x, row_y, content_width - 2 * kCardPadding, 16});
  mapping_hint_label_ = AddWidget(std::move(hint));
  row_y += 16 + 6;

  // Validation error for rejected keymap.txt edits; hidden while valid.
  auto mapping_error = std::make_unique<ui::Label>();
  mapping_error->style().font_family = L"Segoe UI";
  mapping_error->style().font_size = 11.0f;
  mapping_error->style().color = kColorError;
  mapping_error->set_bounds(
      {card2_x, row_y, content_width - 2 * kCardPadding, 16});
  mapping_error->set_visible(false);
  mapping_error_label_ = AddWidget(std::move(mapping_error));
  row_y += 16;

  mapping_card_.height = row_y + 12 - mapping_card_.y;

  // ---- Pinned footer (strip + Save/Close) --------------------------------
  // Positioned by ApplyScrollLayout relative to the visible client area.
  footer_strip_ = AddWidget(std::make_unique<FooterStrip>());

  auto save = std::make_unique<ui::Button>();
  save->style().font_family = L"Segoe UI";
  save->set_kind(ui::Button::Kind::kPrimary);
  save->SetOnClick([this] {
    SettingsStore::Save(settings_);
    if (on_changed_) on_changed_(settings_);
    if (font_popup_ != nullptr) font_popup_->Hide();
    ::ShowWindow(hwnd(), SW_HIDE);
  });
  save_button_ = AddWidget(std::move(save));

  auto close = std::make_unique<ui::Button>();
  close->style().font_family = L"Segoe UI";
  close->SetOnClick([this] {
    if (font_popup_ != nullptr) font_popup_->Hide();
    ::ShowWindow(hwnd(), SW_HIDE);
  });
  close_button_ = AddWidget(std::move(close));

  // Scroll bookkeeping: everything except the footer scrolls; the base
  // (unscrolled) bounds recorded here are the single source of layout.
  general_card_base_ = general_card_;
  mapping_card_base_ = mapping_card_;
  content_height_ = mapping_card_.bottom() + kMargin;
  scrolled_widgets_.clear();
  for (const auto& widget : widgets()) {
    ui::Widget* raw = widget.get();
    if (raw == footer_strip_ || raw == save_button_ ||
        raw == close_button_) {
      continue;
    }
    scrolled_widgets_.emplace_back(raw, raw->bounds());
  }
}

void SettingsWindow::ApplyScrollLayout() {
  if (hwnd() == nullptr) return;
  const ui::Size client = logical_client_size();
  const int viewport = client.height - kFooterHeight;
  const int max_offset = std::max(0, content_height_ - viewport);
  scroll_offset_ = std::clamp(scroll_offset_, 0, max_offset);

  for (const auto& [widget, base] : scrolled_widgets_) {
    widget->set_bounds(
        {base.x, base.y - scroll_offset_, base.width, base.height});
  }
  general_card_ = {general_card_base_.x,
                   general_card_base_.y - scroll_offset_,
                   general_card_base_.width, general_card_base_.height};
  mapping_card_ = {mapping_card_base_.x,
                   mapping_card_base_.y - scroll_offset_,
                   mapping_card_base_.width, mapping_card_base_.height};

  footer_strip_->set_bounds(
      {0, client.height - kFooterHeight, client.width, kFooterHeight});
  const int button_y = client.height - kFooterHeight + (kFooterHeight - 34) / 2;
  save_button_->set_bounds({kClientWidth - kMargin - 96, button_y, 96, 34});
  close_button_->set_bounds(
      {kClientWidth - kMargin - 96 - 10 - 96, button_y, 96, 34});

  // Native scrollbar mirrors the offset (logical units); with
  // page >= range Windows hides it automatically.
  SCROLLINFO info = {};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = content_height_ - 1;
  info.nPage = static_cast<UINT>(std::max(0, viewport));
  info.nPos = scroll_offset_;
  ::SetScrollInfo(hwnd(), SB_VERT, &info, TRUE);
}

void SettingsWindow::SetScrollOffset(int offset) {
  scroll_offset_ = offset;
  ApplyScrollLayout();
  Invalidate();
}

void SettingsWindow::OnDpiChanged() {
  ApplyScrollLayout();
}

void SettingsWindow::ApplyStrings() {
  const UiStrings& strings = GetStrings(settings_.ui_language);
  ::SetWindowTextW(hwnd(), strings.window_title);
  title_label_->SetText(strings.app_title);
  subtitle_label_->SetText(strings.subtitle);
  general_section_label_->SetText(strings.section_general);
  page_size_label_->SetText(strings.candidates_per_page);
  cloud_label_->SetText(strings.cloud_candidates);
  language_label_->SetText(strings.language_label);
  language_button_->SetCaption(strings.language_name);
  font_label_->SetText(strings.candidate_font_label);
  mapping_title_label_->SetText(strings.section_key_mapping);
  edit_mapping_button_->SetCaption(strings.edit_mapping_file);
  mapping_hint_label_->SetText(strings.mapping_hint);
  save_button_->SetCaption(strings.save);
  close_button_->SetCaption(strings.close);
  // Force the mapping preview and validation error to rebuild with the
  // localized strings.
  shown_mapping_revision_ = 0;
  shown_status_revision_ = 0;
  RefreshMappingPreview();
  Invalidate();
}

void SettingsWindow::RefreshLabels() {
  const UiStrings& strings = GetStrings(settings_.ui_language);
  page_size_value_->SetText(
      std::to_wstring(settings_.candidates_per_page));
  cloud_toggle_->SetCaption(settings_.cloud_source_enabled
                                ? strings.toggle_on
                                : strings.toggle_off);
  font_button_->SetCaption(settings_.candidate_font + L"   ▾");
  vertical_preview_->style().font_family = settings_.candidate_font;
  Invalidate();
}

void SettingsWindow::OpenFontPicker() {
  if (font_popup_ != nullptr && font_popup_->IsVisible()) {
    font_popup_->Hide();
    return;
  }
  if (font_families_.empty()) {
    font_families_ = ui::RenderContext::EnumerateFontFamilies();
    if (font_families_.empty()) return;
  }
  if (font_popup_ == nullptr) {
    font_popup_ = std::make_unique<ui::DropdownPopup>();
    font_popup_->SetOnSelect([this](size_t index) {
      if (index < font_families_.size()) {
        ApplyCandidateFont(font_families_[index].name);
      }
    });
  }

  std::vector<ui::DropdownItem> items;
  items.reserve(font_families_.size());
  size_t selected = 0;
  for (size_t i = 0; i < font_families_.size(); ++i) {
    ui::DropdownItem item;
    item.text = font_families_[i].name;
    if (font_families_[i].supports_mongolian) item.badge = L"\x1820";
    if (font_families_[i].name == settings_.candidate_font) selected = i;
    items.push_back(std::move(item));
  }
  font_popup_->SetItems(std::move(items), selected);

  // Anchor rect of the font button in physical screen coordinates.
  POINT origin = {ToPhysical(font_button_->bounds().x),
                  ToPhysical(font_button_->bounds().y)};
  ::ClientToScreen(hwnd(), &origin);
  const ui::Rect anchor = {origin.x, origin.y,
                           ToPhysical(font_button_->bounds().width),
                           ToPhysical(font_button_->bounds().height)};
  font_popup_->ShowBelow(hwnd(), anchor);
}

void SettingsWindow::ApplyCandidateFont(const std::wstring& font) {
  settings_.candidate_font = font;
  RefreshLabels();
}

void SettingsWindow::RefreshMappingPreview() {
  const KeyMapping& mapping = mapping_store_.Get();
  if (mapping_store_.revision() != shown_mapping_revision_) {
    shown_mapping_revision_ = mapping_store_.revision();
    mapping_preview_label_->SetText(
        BuildMappingPreview(mapping, GetStrings(settings_.ui_language)));
    Invalidate();
  }

  // Validation status: a rejected keymap.txt edit shows the localized
  // error (the store already restored the file to the previous version).
  if (mapping_store_.status_revision() != shown_status_revision_) {
    shown_status_revision_ = mapping_store_.status_revision();
    const KeyMapFileStatus& status = mapping_store_.file_status();
    if (status.valid) {
      mapping_error_label_->set_visible(false);
    } else {
      std::wstring message =
          GetStrings(settings_.ui_language).mapping_error_restored;
      const size_t placeholder = message.find(L"%ls");
      if (placeholder != std::wstring::npos) {
        message.replace(placeholder, 3,
                        std::to_wstring(status.error_line));
      }
      mapping_error_label_->SetText(std::move(message));
      mapping_error_label_->set_visible(true);
    }
    Invalidate();
  }
}

void SettingsWindow::OpenMappingFileForEditing() {
  const std::wstring& path = mapping_store_.path();
  if (path.empty()) return;
  // First use: materialise the current (default) mapping so the user edits
  // a complete, commented file rather than starting from scratch.
  if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    mapping_store_.Get().SaveToFile(path);
  }
  ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

void SettingsWindow::OnPaintBackground(ui::RenderContext& context) {
  context.Clear(kColorWindowBg);
  for (const ui::Rect& card : {general_card_, mapping_card_}) {
    context.FillRoundedRect(card, 8.0f, kColorCardBg);
    context.DrawBorder(card, kColorCardBorder);
  }
}

bool SettingsWindow::OnMessage(UINT message, WPARAM wparam, LPARAM lparam,
                               LRESULT* result) {
  if (message == WM_TIMER && wparam == kMappingRefreshTimerId) {
    RefreshMappingPreview();
    *result = 0;
    return true;
  }
  if (message == WM_MOUSEWHEEL) {
    if (font_popup_ != nullptr) font_popup_->Hide();
    const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    SetScrollOffset(scroll_offset_ - (delta / WHEEL_DELTA) * kWheelStep);
    *result = 0;
    return true;
  }
  if (message == WM_VSCROLL) {
    if (font_popup_ != nullptr) font_popup_->Hide();
    const int viewport = logical_client_size().height - kFooterHeight;
    int offset = scroll_offset_;
    switch (LOWORD(wparam)) {
      case SB_LINEUP:
        offset -= 24;
        break;
      case SB_LINEDOWN:
        offset += 24;
        break;
      case SB_PAGEUP:
        offset -= viewport;
        break;
      case SB_PAGEDOWN:
        offset += viewport;
        break;
      case SB_TOP:
        offset = 0;
        break;
      case SB_BOTTOM:
        offset = content_height_;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION: {
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_TRACKPOS;
        ::GetScrollInfo(hwnd(), SB_VERT, &info);
        offset = info.nTrackPos;
        break;
      }
      default:
        break;
    }
    SetScrollOffset(offset);
    *result = 0;
    return true;
  }
  if (message == WM_LBUTTONDOWN && font_popup_ != nullptr &&
      font_popup_->IsVisible()) {
    // Click anywhere else in the dialog closes the dropdown; a click on
    // the font button itself is left to its toggle logic.
    const ui::Point p = {
        static_cast<int>(GET_X_LPARAM(lparam) / scale()),
        static_cast<int>(GET_Y_LPARAM(lparam) / scale())};
    if (!font_button_->bounds().Contains(p)) font_popup_->Hide();
    // Fall through to normal handling.
  }
  if (message == WM_CLOSE) {
    // Hide instead of destroy so the window can be reshown cheaply.
    ::KillTimer(hwnd(), kMappingRefreshTimerId);
    if (font_popup_ != nullptr) font_popup_->Hide();
    ::ShowWindow(hwnd(), SW_HIDE);
    *result = 0;
    return true;
  }
  return ui::Window::OnMessage(message, wparam, lparam, result);
}

}  // namespace paiza::service
