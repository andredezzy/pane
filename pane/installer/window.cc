// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/window.h"

#include <windows.h>

#include <commctrl.h>
#include <d2d1_1.h>
#include <dwmapi.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "pane/installer/look.h"
#include "pane/installer/resources.h"
#include "pane/installer/uia_element.h"

#include <uiautomationcoreapi.h>

namespace pane::installer {

namespace {

// A path drawn on one line, %LOCALAPPDATA%\Programs\Pane whole: line
// breaking allows a break inside one, before a backslash or a percent sign
// (UAX #14's prefix and postfix classes), so each word that holds a backslash
// takes a word joiner between its characters. Drawn alone: UI Automation reads
// the words as they are.
std::wstring KeepPathsWhole(std::wstring_view text) {
  std::wstring kept;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t space = std::min(text.find(L' ', start), text.size());
    const std::wstring_view word = text.substr(start, space - start);
    const bool path = word.find(L'\\') != std::wstring_view::npos;
    for (size_t i = 0; i < word.size(); ++i) {
      // Never inside a surrogate pair.
      if (path && i > 0 && !(word[i] >= 0xDC00 && word[i] <= 0xDFFF)) {
        kept += L'\u2060';
      }
      kept += word[i];
    }
    if (space < text.size()) {
      kept += L' ';
    }
    start = space + 1;
  }
  return kept;
}

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Paper 11.12.6 to 11.12.10, in points (Windows' DIPs).
constexpr float kWidth = 440;
constexpr float kPaddingX = 32;
constexpr float kPaddingY = 28;
constexpr float kContentWidth = kWidth - 2 * kPaddingX;
constexpr float kIconSize = 48;
constexpr float kGap = 16;
constexpr float kTitleToMessage = 4;
// Minimize and Close, Chrome's Windows 11 glyphs, 1 apart in the corner.
constexpr float kCaptionWidth = 45;
constexpr float kCaptionHeight = 32;
constexpr float kCaptionGap = 1;
constexpr float kGlyphSize = 10;
// The bar: 4 above it, 4 high (6 under a contrast theme, in its edge), 8 to
// its labels' row.
constexpr float kBarAbove = 4;
constexpr float kBarHeight = 4;
constexpr float kContrastBarHeight = 6;
constexpr float kBarToLabels = 8;
constexpr float kLabelLine = 16;
// The keyed buttons (11.13 A).
constexpr float kButtonsAbove = 8;
constexpr float kButtonHeight = 36;
constexpr float kButtonGap = 8;
constexpr float kButtonRadius = 15;
constexpr float kButtonLeft = 14;
constexpr float kButtonRight = 8;
constexpr float kLabelToChip = 8;
constexpr float kChipHeight = 20;
constexpr float kChipRadius = 9;
constexpr float kChipPadding = 6;
// The box (11.12.9).
constexpr float kBoxAbove = 2;
constexpr float kBoxSize = 18;
constexpr float kBoxRadius = 6;
constexpr float kBoxDown = 1;
constexpr float kBoxToText = 10;
constexpr float kNameToSize = 8;
constexpr float kNameLine = 20;
constexpr float kNameToLine = 2;
// The keyboard's ring (11.17 B, 11.18): 2 wide, 1 off a strong fill.
constexpr float kRingWidth = 2;
constexpr float kRingOffFill = 1;
// Each control's window reaches past its shape by its ring.
constexpr float kRingRoom = kRingOffFill + kRingWidth;
// A contrast theme's edge around the App icon.
constexpr float kIconEdgeRadius = 11;
// Pane's hover: 0.15 s, easing out (motion::Hover, cubic-bezier(0, 0, 0.58,
// 1)).
constexpr base::TimeDelta kHover = base::Milliseconds(150);
// A frame, as Windows' timers give one.
constexpr UINT kFrameMilliseconds = 10;

constexpr wchar_t kWindowClass[] = L"PaneInstallerWindow";
constexpr UINT kTaskMessage = WM_APP + 1;
constexpr UINT_PTR kFrameTimer = 1;
// The first screen's message, said a moment after a screen reader first
// reaches the window (UiaElement::SayDescription).
constexpr UINT_PTR kDescriptionTimer = 2;
constexpr UINT kDescriptionMilliseconds = 400;
constexpr int kProgressId = 900;
constexpr int kBoxId = 901;

// cubic-bezier(0, 0, 0.58, 1) at `t`, 0 to 1.
float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // x(s) = 3(1-s)²s·0 + 3(1-s)s²·0.58 + s³; y(s) = 3(1-s)²s·0 + 3(1-s)s² + s³
  // with the first control point at the origin: solve x(s) = t by bisection.
  float low = 0;
  float high = 1;
  float s = t;
  for (int i = 0; i < 24; ++i) {
    s = (low + high) / 2;
    const float x = 3 * (1 - s) * s * s * 0.58f + s * s * s;
    (x < t ? low : high) = s;
  }
  return 3 * (1 - s) * s * s + s * s * s;
}

// A value moving to its target over Pane's hover, or at once.
class Fade {
 public:
  void To(float target, bool animate) {
    if (target == target_) {
      return;
    }
    from_ = Value();
    target_ = target;
    start_ = base::TimeTicks::Now();
    animating_ = animate;
  }
  float Value() const {
    if (!animating_) {
      return target_;
    }
    const float t =
        static_cast<float>((base::TimeTicks::Now() - start_) / kHover);
    if (t >= 1) {
      return target_;
    }
    return from_ + (target_ - from_) * EaseOut(t);
  }
  bool Moving() const {
    return animating_ && base::TimeTicks::Now() - start_ < kHover;
  }
  float target() const { return target_; }

 private:
  float from_ = 0;
  float target_ = 0;
  base::TimeTicks start_;
  bool animating_ = false;
};

// The factories, made once.
struct Graphics {
  ComPtr<ID2D1Factory1> d2d;
  ComPtr<IDWriteFactory> dwrite;
  // Windows 10 2004 and later: a variable font's axes.
  ComPtr<IDWriteFactory6> dwrite6;
  ComPtr<IDWriteFontCollection2> typographic;
  ComPtr<IWICImagingFactory> wic;
  // Segoe UI Variable, Windows 11's UI font (ADR 0126): none on Windows 10.
  bool variable = false;

  static Graphics& Get() {
    static base::NoDestructor<Graphics> graphics;
    return *graphics;
  }

  Graphics() {
    D2D1_FACTORY_OPTIONS options = {};
    ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                        __uuidof(ID2D1Factory1), &options, &d2d);
    ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                          reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    if (dwrite && SUCCEEDED(dwrite.As(&dwrite6)) &&
        SUCCEEDED(dwrite6->GetSystemFontCollection(
            FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &typographic))) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      variable = SUCCEEDED(typographic->FindFamilyName(L"Segoe UI Variable",
                                                       &index, &exists)) &&
                 exists;
    }
    ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&wic));
  }
};

// Pane's text on Windows (pane/ui/design/text_win.cc): Segoe UI Variable at
// each weight on its axis and the optical size of its size, or Segoe UI on
// Windows 10, its Regular in Medium's place.
enum class Style {
  kTitle,
  kMessage,
  kLabel,
  kNumbers,
  kButton,
  kKey,
  kBoxName,
  kBoxSize,
  kBoxLine,
  kCount,
};

struct StyleSpec {
  float size;
  float line;
  int weight;
  bool tabular;
  float tracking;
};

constexpr std::array<StyleSpec, static_cast<size_t>(Style::kCount)> kStyles = {{
    {18, 24, 600, false, -0.01f},  // kTitle
    {14, 20, 400, false, 0},       // kMessage
    {12, 16, 400, false, 0},       // kLabel
    {12, 16, 400, true, 0},        // kNumbers
    {14, 20, 500, false, 0},       // kButton
    {12, 16, 500, false, 0},       // kKey
    {14, 20, 500, false, 0},       // kBoxName
    {12, 16, 400, false, 0},       // kBoxSize
    {12, 16, 400, false, 0},       // kBoxLine
}};

const StyleSpec& SpecOf(Style style) {
  return kStyles[static_cast<size_t>(style)];
}

class Fonts {
 public:
  Fonts() {
    Graphics& graphics = Graphics::Get();
    for (size_t i = 0; i < kStyles.size(); ++i) {
      const StyleSpec& spec = kStyles[i];
      ComPtr<IDWriteTextFormat> format;
      if (graphics.variable) {
        const std::array<DWRITE_FONT_AXIS_VALUE, 2> axes = {{
            {DWRITE_FONT_AXIS_TAG_WEIGHT, static_cast<float>(spec.weight)},
            {DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE,
             std::clamp(spec.size, 8.0f, 36.0f)},
        }};
        ComPtr<IDWriteTextFormat3> format3;
        if (SUCCEEDED(graphics.dwrite6->CreateTextFormat(
                L"Segoe UI Variable", graphics.typographic.Get(), axes.data(),
                static_cast<UINT32>(axes.size()), spec.size, L"en-us",
                &format3))) {
          format = format3;
        }
      }
      if (!format) {
        const DWRITE_FONT_WEIGHT weight = spec.weight >= 600
                                              ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                              : DWRITE_FONT_WEIGHT_NORMAL;
        graphics.dwrite->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, spec.size, L"en-us", &format);
      }
      CHECK(format);
      // CSS's line height: the line's half of what the font leaves, above
      // its ascent.
      const auto [ascent, descent] = Metrics(format.Get(), spec.size);
      const float baseline = (spec.line - (ascent + descent)) / 2 + ascent;
      format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, spec.line,
                             baseline);
      format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
      formats_[i] = format;
    }
  }

  // `text` laid out in `style`, `width` wide (0: on one line).
  ComPtr<IDWriteTextLayout> Layout(Style style,
                                   const std::u16string& text,
                                   float width) const {
    const StyleSpec& spec = SpecOf(style);
    const std::wstring wide = KeepPathsWhole(base::UTF16ToWide(text));
    ComPtr<IDWriteTextLayout> layout;
    Graphics::Get().dwrite->CreateTextLayout(
        wide.c_str(), static_cast<UINT32>(wide.size()),
        formats_[static_cast<size_t>(style)].Get(),
        width > 0 ? width : 100000.0f, 1000.0f, &layout);
    if (!layout) {
      return layout;
    }
    if (width <= 0) {
      layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    const DWRITE_TEXT_RANGE all = {0, static_cast<UINT32>(wide.size())};
    if (spec.tracking != 0) {
      ComPtr<IDWriteTextLayout1> spaced;
      if (SUCCEEDED(layout.As(&spaced))) {
        spaced->SetCharacterSpacing(0, spec.tracking * spec.size, 0, all);
      }
    }
    if (spec.tabular) {
      ComPtr<IDWriteTypography> typography;
      if (SUCCEEDED(Graphics::Get().dwrite->CreateTypography(&typography))) {
        typography->AddFontFeature(
            {DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
        layout->SetTypography(typography.Get(), all);
      }
    }
    return layout;
  }

  static float Width(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics = {};
    if (!layout || FAILED(layout->GetMetrics(&metrics))) {
      return 0;
    }
    return metrics.widthIncludingTrailingWhitespace;
  }

  static float Height(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics = {};
    if (!layout || FAILED(layout->GetMetrics(&metrics))) {
      return 0;
    }
    return metrics.height;
  }

 private:
  // The face's ascent and descent at `size`, as its first line lays out.
  static std::pair<float, float> Metrics(IDWriteTextFormat* format,
                                         float size) {
    ComPtr<IDWriteTextLayout> layout;
    Graphics::Get().dwrite->CreateTextLayout(L"x", 1, format, 1000, 1000,
                                             &layout);
    ComPtr<IDWriteFontCollection> collection;
    std::array<wchar_t, 128> family = {};
    if (layout && SUCCEEDED(layout->GetFontCollection(0, &collection)) &&
        SUCCEEDED(layout->GetFontFamilyName(
            0, family.data(), static_cast<UINT32>(family.size())))) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      ComPtr<IDWriteFontFamily> found;
      ComPtr<IDWriteFont> font;
      if (SUCCEEDED(
              collection->FindFamilyName(family.data(), &index, &exists)) &&
          exists && SUCCEEDED(collection->GetFontFamily(index, &found)) &&
          SUCCEEDED(found->GetFirstMatchingFont(
              DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
              DWRITE_FONT_STYLE_NORMAL, &font))) {
        DWRITE_FONT_METRICS metrics = {};
        font->GetMetrics(&metrics);
        const float unit = size / metrics.designUnitsPerEm;
        return {metrics.ascent * unit, metrics.descent * unit};
      }
    }
    return {size * 0.92f, size * 0.22f};
  }

  std::array<ComPtr<IDWriteTextFormat>, kStyles.size()> formats_;
};

// Half a superellipse's corner as one cubic (pane/ui/design/squircle.cc,
// Blink's fit for a curvature of 4, CSS's superellipse(2)).
std::array<D2D1_POINT_2F, 3> HalfCorner() {
  static const std::array<D2D1_POINT_2F, 3> half = [] {
    constexpr std::array<double, 7> p = {
        1.2430920942724248, 2.010479023614843,  0.32922901179443753,
        0.2823023142212073, 1.3473704261055421, 2.9149468637949814,
        0.9106507102917086};
    const float curvature = 4;
    const float s = std::log2(curvature);
    const float slope =
        p[0] + (p[6] - p[0]) * 0.5 * (1 + std::tanh(p[5] * (s - p[1])));
    const float base = 1 / (1 + std::exp(-slope * (0 - p[1])));
    const float logistic = 1 / (1 + std::exp(-slope * (s - p[1])));
    const float a = (logistic - base) / (1 - base);
    const float b = p[2] * std::exp(-p[3] * std::pow(s, p[4]));
    const float middle = std::pow(0.5f, 1 / curvature);
    return std::array<D2D1_POINT_2F, 3>{
        {{a, 1}, {middle - b, middle + b}, {middle, middle}}};
  }();
  return half;
}

void AddCorner(ID2D1GeometrySink* sink,
               D2D1_POINT_2F start,
               D2D1_POINT_2F outer,
               D2D1_POINT_2F end,
               D2D1_POINT_2F center) {
  const std::array<D2D1_POINT_2F, 3> half = HalfCorner();
  const D2D1_POINT_2F along = {outer.x - start.x, outer.y - start.y};
  const D2D1_POINT_2F across = {start.x - center.x, start.y - center.y};
  const auto map = [&](D2D1_POINT_2F point) {
    return D2D1::Point2F(center.x + along.x * point.x + across.x * point.y,
                         center.y + along.y * point.x + across.y * point.y);
  };
  const auto swapped = [](D2D1_POINT_2F point) {
    return D2D1::Point2F(point.y, point.x);
  };
  sink->AddLine(start);
  sink->AddBezier(
      D2D1::BezierSegment(map(half[0]), map(half[1]), map(half[2])));
  sink->AddBezier(
      D2D1::BezierSegment(map(swapped(half[1])), map(swapped(half[0])), end));
}

// The squircle in `rect`, rounded `radius` (pane/ui/design/squircle.cc's
// SquirclePath).
ComPtr<ID2D1PathGeometry> Squircle(const D2D1_RECT_F& rect, float radius) {
  const float width = rect.right - rect.left;
  const float height = rect.bottom - rect.top;
  const float r = std::max(0.0f, std::min(radius, std::min(width, height) / 2));
  ComPtr<ID2D1PathGeometry> path;
  Graphics::Get().d2d->CreatePathGeometry(&path);
  ComPtr<ID2D1GeometrySink> sink;
  if (!path || FAILED(path->Open(&sink))) {
    return path;
  }
  const float x = rect.left;
  const float y = rect.top;
  sink->BeginFigure(D2D1::Point2F(x, y + r), D2D1_FIGURE_BEGIN_FILLED);
  if (r > 0) {
    AddCorner(sink.Get(), {x, y + r}, {x, y}, {x + r, y}, {x + r, y + r});
    AddCorner(sink.Get(), {x + width - r, y}, {x + width, y},
              {x + width, y + r}, {x + width - r, y + r});
    AddCorner(sink.Get(), {x + width, y + height - r}, {x + width, y + height},
              {x + width - r, y + height}, {x + width - r, y + height - r});
    AddCorner(sink.Get(), {x + r, y + height}, {x, y + height},
              {x, y + height - r}, {x + r, y + height - r});
  } else {
    sink->AddLine(D2D1::Point2F(x + width, y));
    sink->AddLine(D2D1::Point2F(x + width, y + height));
    sink->AddLine(D2D1::Point2F(x, y + height));
  }
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  sink->Close();
  return path;
}

D2D1_COLOR_F ToD2D(const Color& color) {
  return D2D1::ColorF(color.r, color.g, color.b, color.a);
}

D2D1_RECT_F Outset(const D2D1_RECT_F& rect, float by) {
  return D2D1::RectF(rect.left - by, rect.top - by, rect.right + by,
                     rect.bottom + by);
}

D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float by) {
  return Outset(rect, -by);
}

// Draws with Direct2D into a GDI device context's rectangle, Windows' paint
// for a window and for each control: in software, so the window's first
// frame waits on no graphics device.
class Painter {
 public:
  Painter() {
    const D2D1_RENDER_TARGET_PROPERTIES properties =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE));
    Graphics::Get().d2d->CreateDCRenderTarget(&properties, &target_);
  }

  bool Begin(HDC dc, const RECT& rect, float dpi, HMONITOR monitor) {
    if (!target_ || FAILED(target_->BindDC(dc, &rect))) {
      return false;
    }
    target_->SetDpi(dpi, dpi);
    if (monitor != monitor_) {
      monitor_ = monitor;
      params_.Reset();
      Graphics::Get().dwrite->CreateMonitorRenderingParams(monitor, &params_);
    }
    if (params_) {
      target_->SetTextRenderingParams(params_.Get());
    }
    // ClearType on the opaque ground under each label (ADR 0126), as Pane's
    // labels draw it.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (!brush_) {
      target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
    }
    return true;
  }

  void End() {
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
      brush_.Reset();
      bitmaps_.clear();
    }
  }

  ID2D1DCRenderTarget* target() { return target_.Get(); }

  void FillRect(const D2D1_RECT_F& rect, const Color& color) {
    if (color.a <= 0) {
      return;
    }
    brush_->SetColor(ToD2D(color));
    target_->FillRectangle(rect, brush_.Get());
  }

  void FillSquircle(const D2D1_RECT_F& rect, float radius, const Color& color) {
    if (color.a <= 0) {
      return;
    }
    brush_->SetColor(ToD2D(color));
    target_->FillGeometry(Squircle(rect, radius).Get(), brush_.Get());
  }

  // A line `width` wide inside the squircle `rect`, as Pane strokes one
  // (SquircleStrokePath).
  void StrokeSquircle(const D2D1_RECT_F& rect,
                      float radius,
                      float width,
                      const Color& color) {
    if (color.a <= 0) {
      return;
    }
    brush_->SetColor(ToD2D(color));
    target_->DrawGeometry(
        Squircle(Inset(rect, width / 2), std::max(radius - width / 2, 0.0f))
            .Get(),
        brush_.Get(), width);
  }

  void FillCapsule(const D2D1_RECT_F& rect, const Color& color) {
    if (color.a <= 0 || rect.right <= rect.left) {
      return;
    }
    const float r =
        std::min(rect.right - rect.left, rect.bottom - rect.top) / 2;
    brush_->SetColor(ToD2D(color));
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), brush_.Get());
  }

  void StrokeCapsule(const D2D1_RECT_F& rect, float width, const Color& color) {
    const D2D1_RECT_F inner = Inset(rect, width / 2);
    const float r =
        std::min(inner.right - inner.left, inner.bottom - inner.top) / 2;
    brush_->SetColor(ToD2D(color));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inner, r, r), brush_.Get(),
                                  width);
  }

  void Line(D2D1_POINT_2F from,
            D2D1_POINT_2F to,
            float width,
            const Color& color) {
    brush_->SetColor(ToD2D(color));
    target_->DrawLine(from, to, brush_.Get(), width);
  }

  void Text(IDWriteTextLayout* layout, D2D1_POINT_2F at, const Color& color) {
    if (!layout) {
      return;
    }
    brush_->SetColor(ToD2D(color));
    target_->DrawTextLayout(at, layout, brush_.Get());
  }

  // The App icon's `resource` at `pixels` square, drawn from the .ico's
  // nearest size at or over it, Windows' high-quality cubic between.
  void Icon(int resource, int pixels, const D2D1_RECT_F& rect) {
    const auto key = std::make_pair(resource, pixels);
    auto found = bitmaps_.find(key);
    if (found == bitmaps_.end()) {
      found = bitmaps_.emplace(key, LoadIcon(resource, pixels)).first;
    }
    if (found->second) {
      target_->DrawBitmap(found->second.Get(), rect, 1.0f,
                          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
  }

 private:
  ComPtr<ID2D1Bitmap> LoadIcon(int resource, int pixels) {
    IWICImagingFactory* wic = Graphics::Get().wic.Get();
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC found =
        ::FindResourceW(module, MAKEINTRESOURCEW(resource), RT_RCDATA);
    HGLOBAL loaded = found ? ::LoadResource(module, found) : nullptr;
    const void* bytes = loaded ? ::LockResource(loaded) : nullptr;
    const DWORD size = found ? ::SizeofResource(module, found) : 0;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (!wic || !bytes || FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(
            static_cast<BYTE*>(const_cast<void*>(bytes)), size)) ||
        FAILED(wic->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
      return nullptr;
    }
    UINT frames = 0;
    decoder->GetFrameCount(&frames);
    ComPtr<IWICBitmapFrameDecode> best;
    UINT best_side = 0;
    for (UINT i = 0; i < frames; ++i) {
      ComPtr<IWICBitmapFrameDecode> frame;
      UINT width = 0;
      UINT height = 0;
      if (FAILED(decoder->GetFrame(i, &frame)) ||
          FAILED(frame->GetSize(&width, &height))) {
        continue;
      }
      const bool fits = width >= static_cast<UINT>(pixels);
      const bool best_fits = best_side >= static_cast<UINT>(pixels);
      if (!best || (fits && (!best_fits || width < best_side)) ||
          (!fits && !best_fits && width > best_side)) {
        best = frame;
        best_side = width;
      }
    }
    if (!best) {
      return nullptr;
    }
    ComPtr<IWICBitmapSource> source = best;
    if (best_side != static_cast<UINT>(pixels)) {
      ComPtr<IWICBitmapScaler> scaler;
      if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
          SUCCEEDED(
              scaler->Initialize(best.Get(), pixels, pixels,
                                 WICBitmapInterpolationModeHighQualityCubic))) {
        source = scaler;
      }
    }
    ComPtr<IWICFormatConverter> converter;
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(wic->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(),
                                     GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeMedianCut)) ||
        FAILED(target_->CreateBitmapFromWicBitmap(converter.Get(), &bitmap))) {
      return nullptr;
    }
    return bitmap;
  }

  ComPtr<ID2D1DCRenderTarget> target_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  ComPtr<IDWriteRenderingParams> params_;
  HMONITOR monitor_ = nullptr;
  std::map<std::pair<int, int>, ComPtr<ID2D1Bitmap>> bitmaps_;
};

// Where each part of a screen goes, in points.
struct Placed {
  float height = 0;
  D2D1_RECT_F icon = {};
  D2D1_POINT_2F title_at = {};
  ComPtr<IDWriteTextLayout> title;
  D2D1_POINT_2F message_at = {};
  ComPtr<IDWriteTextLayout> message;
  D2D1_RECT_F track = {};
  float labels_top = 0;
  ComPtr<IDWriteTextLayout> left_label;
  // The box's row.
  D2D1_RECT_F box_row = {};
  struct Button {
    Screen::Button button;
    D2D1_RECT_F shape = {};
    ComPtr<IDWriteTextLayout> label;
    ComPtr<IDWriteTextLayout> key;
    float label_width = 0;
    float key_width = 0;
  };
  std::vector<Button> buttons;
};

// A control of Windows' own the window draws: its hover and press fade as
// Pane's do (ControlHighlight).
struct Control {
  HWND hwnd = nullptr;
  int id = 0;
  // Its shape's place in its window, which leaves room for its ring.
  D2D1_RECT_F shape = {};
  size_t button = 0;
  Fade hover;
  Fade press;
  bool pressed = false;
};

}  // namespace

class InstallWindow::Impl {
 public:
  Impl(Delegate* delegate, std::wstring title)
      : delegate_(delegate), title_(std::move(title)) {}

  ~Impl() {
    if (hwnd_) {
      ::DestroyWindow(hwnd_);
    }
  }

  void Show(const Screen& screen) {
    // The same screen shown again (the box ticked, a new scale or contrast)
    // keeps the focus where it was; a new one starts from the window.
    const bool again = hwnd_ && screen.title == screen_.title;
    screen_ = screen;
    if (!hwnd_) {
      Create();
    }
    Rebuild(/*keep_focus=*/again);
  }

  void SetBoxSize(const std::u16string& size) {
    if (!screen_.box || screen_.box->size == size) {
      return;
    }
    screen_.box->size = size;
    if (box_) {
      ::InvalidateRect(box_, nullptr, FALSE);
    }
    if (box_element_) {
      box_element_->Set(base::UTF16ToWide(screen_.box->label),
                        BoxDescription());
    }
  }

  void Close() {
    if (hwnd_) {
      ::DestroyWindow(hwnd_);
    }
  }

  void Post(base::OnceClosure task) {
    auto* owned = new base::OnceClosure(std::move(task));
    if (!hwnd_ || !::PostMessageW(hwnd_, kTaskMessage, 0,
                                  reinterpret_cast<LPARAM>(owned))) {
      delete owned;
    }
  }

  void Run() {
    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
      if (TakesKey(message)) {
        continue;
      }
      if (hwnd_ && ::IsDialogMessageW(hwnd_, &message)) {
        continue;
      }
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
  }

  HWND hwnd() const { return hwnd_; }

 private:
  // MARK: The window

  void Create() {
    INITCOMMONCONTROLSEX controls = {
        .dwSize = sizeof(controls),
        .dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    ::InitCommonControlsEx(&controls);
    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class = {
        .cbSize = sizeof(window_class),
        .lpfnWndProc = &Impl::WindowProc,
        .hInstance = instance,
        .hIcon = static_cast<HICON>(::LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_PANE), IMAGE_ICON,
            ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), 0)),
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = kWindowClass,
        .hIconSm = static_cast<HICON>(
            ::LoadImageW(instance, MAKEINTRESOURCEW(IDI_PANE), IMAGE_ICON,
                         ::GetSystemMetrics(SM_CXSMICON),
                         ::GetSystemMetrics(SM_CYSMICON), 0)),
    };
    ::RegisterClassExW(&window_class);
    appearance_ = CurrentAppearance();
    palette_ = std::make_unique<Palette>(appearance_);
    // Made hidden, sized for the first screen, then shown at once.
    hwnd_ = ::CreateWindowExW(0, kWindowClass, title_.c_str(),
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                  WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                              nullptr, instance, this);
    CHECK(hwnd_);
    window_element_ =
        Microsoft::WRL::Make<UiaElement>(hwnd_, UiaElement::Kind::kDialog);
    dpi_ = ::GetDpiForWindow(hwnd_);
    ApplyFrame();
    // No ring until a key moves the focus, as in Pane's dialogs.
    ::SendMessageW(hwnd_, WM_CHANGEUISTATE,
                   MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
    first_show_ = true;
  }

  // Windows 11's frame without its title band: its corners, hairline, and
  // shadow, dark where Windows' apps are, and edged in WindowText under a
  // contrast theme.
  void ApplyFrame() {
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners,
                            sizeof(corners));
    const BOOL dark = appearance_.dark && !appearance_.contrast;
    ::DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                            sizeof(dark));
    const COLORREF border =
        appearance_.contrast ? appearance_.window_text : DWMWA_COLOR_DEFAULT;
    ::DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const MARGINS margins = {0, 0, 0, 0};
    ::DwmExtendFrameIntoClientArea(hwnd_, &margins);
  }

  float scale() const { return dpi_ / 96.0f; }

  int Pixels(float points) const {
    return static_cast<int>(std::lround(points * scale()));
  }

  // MARK: The screen

  void Rebuild(bool keep_focus) {
    if (!keep_focus) {
      // A new screen starts from the window, in front or not.
      focus_before_ = 0;
    }
    int focused = 0;
    if (HWND focus = ::GetFocus();
        keep_focus && focus && ::GetParent(focus) == hwnd_) {
      focused = ::GetDlgCtrlID(focus);
    }
    // The controls of the screen before go; the focus with them, to the
    // window, so Tab reaches the first control first, or back to the control
    // that had it when the same screen shows again.
    for (Control& control : controls_) {
      ::DestroyWindow(control.hwnd);
    }
    controls_.clear();
    if (box_) {
      ::DestroyWindow(std::exchange(box_, nullptr));
    }
    box_element_.Reset();
    if (progress_) {
      ::DestroyWindow(std::exchange(progress_, nullptr));
    }
    bar_right_.clear();
    bar_fraction_ = 0;
    Lay();
    MakeControls();
    window_element_->Set(base::UTF16ToWide(screen_.title),
                         base::UTF16ToWide(screen_.message));
    ResizeToScreen();
    HWND refocus = focused ? ::GetDlgItem(hwnd_, focused) : nullptr;
    ::SetFocus(refocus ? refocus : hwnd_);
    if (screen_.bar) {
      Tick();
    }
    UpdateTimer();
    if (std::exchange(first_show_, false)) {
      ::ShowWindow(hwnd_, SW_SHOWNORMAL);
      ::SetForegroundWindow(hwnd_);
    }
    ::RedrawWindow(hwnd_, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }

  void Lay() {
    Placed placed;
    float y = kPaddingY;
    placed.icon =
        D2D1::RectF(kPaddingX, y, kPaddingX + kIconSize, y + kIconSize);
    y += kIconSize + kGap;
    placed.title = fonts_.Layout(Style::kTitle, screen_.title, kContentWidth);
    placed.title_at = D2D1::Point2F(kPaddingX, y);
    y += Fonts::Height(placed.title.Get());
    if (!screen_.message.empty()) {
      y += kTitleToMessage;
      placed.message =
          fonts_.Layout(Style::kMessage, screen_.message, kContentWidth);
      placed.message_at = D2D1::Point2F(kPaddingX, y);
      y += Fonts::Height(placed.message.Get());
    }
    if (screen_.bar) {
      y += kGap + kBarAbove;
      const float height =
          appearance_.contrast ? kContrastBarHeight : kBarHeight;
      placed.track =
          D2D1::RectF(kPaddingX, y, kPaddingX + kContentWidth, y + height);
      y += height + kBarToLabels;
      placed.labels_top = y;
      placed.left_label = fonts_.Layout(Style::kLabel, screen_.bar->left, 0);
      y += kLabelLine;
    }
    if (screen_.box) {
      y += kGap + kBoxAbove;
      const float text_width = kContentWidth - kBoxSize - kBoxToText;
      ComPtr<IDWriteTextLayout> line =
          fonts_.Layout(Style::kBoxLine, screen_.box->line, text_width);
      const float row =
          std::max(kBoxDown + kBoxSize,
                   kNameLine + kNameToLine + Fonts::Height(line.Get()));
      placed.box_row =
          D2D1::RectF(kPaddingX, y, kPaddingX + kContentWidth, y + row);
      y += row;
    }
    if (!screen_.buttons.empty()) {
      y += kGap + kButtonsAbove;
      float right = kPaddingX + kContentWidth;
      std::vector<Placed::Button> buttons;
      for (auto it = screen_.buttons.rbegin(); it != screen_.buttons.rend();
           ++it) {
        Placed::Button placed_button;
        placed_button.button = *it;
        placed_button.label = fonts_.Layout(Style::kButton, it->label, 0);
        placed_button.label_width = Fonts::Width(placed_button.label.Get());
        float width = placed_button.label_width;
        if (it->key.empty()) {
          width += 2 * kButtonLeft;
        } else {
          placed_button.key = fonts_.Layout(Style::kKey, it->key, 0);
          placed_button.key_width = Fonts::Width(placed_button.key.Get());
          width += kButtonLeft + kLabelToChip + placed_button.key_width +
                   2 * kChipPadding + kButtonRight;
        }
        width = std::ceil(width);
        placed_button.shape =
            D2D1::RectF(right - width, y, right, y + kButtonHeight);
        right -= width + kButtonGap;
        buttons.insert(buttons.begin(), std::move(placed_button));
      }
      placed.buttons = std::move(buttons);
      y += kButtonHeight;
    }
    y += kPaddingY;
    placed.height = std::ceil(y);
    placed_ = std::move(placed);
  }

  void MakeControls() {
    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    // The box first: Tab reaches it before the buttons (11.12.9).
    if (screen_.box) {
      const D2D1_RECT_F& row = placed_.box_row;
      box_ = ::CreateWindowExW(
          0, WC_BUTTONW, base::UTF16ToWide(screen_.box->label).c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
          hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoxId)),
          instance, nullptr);
      ::SendMessageW(box_, BM_SETCHECK,
                     screen_.box->ticked ? BST_CHECKED : BST_UNCHECKED, 0);
      PlaceChild(box_, Outset(row, kRingWidth));
      box_element_ =
          Microsoft::WRL::Make<UiaElement>(box_, UiaElement::Kind::kBox);
      box_element_->Set(base::UTF16ToWide(screen_.box->label),
                        BoxDescription());
      ::SetWindowSubclass(box_, &Impl::BoxProc, 1,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    for (size_t i = 0; i < placed_.buttons.size(); ++i) {
      const Placed::Button& button = placed_.buttons[i];
      Control control;
      control.id = button.button.id;
      control.button = i;
      control.hwnd = ::CreateWindowExW(
          0, WC_BUTTONW, base::UTF16ToWide(button.button.label).c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(control.id)), instance,
          nullptr);
      PlaceChild(control.hwnd, Outset(button.shape, kRingRoom));
      controls_.push_back(std::move(control));
    }
    if (screen_.bar) {
      // Windows' own progress bar, which a screen reader reads with its
      // percent (11.12.8), named by the bar's label; drawn as the board draws
      // it.
      progress_ = ::CreateWindowExW(
          0, PROGRESS_CLASSW, base::UTF16ToWide(screen_.bar->left).c_str(),
          WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, hwnd_,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProgressId)), instance,
          nullptr);
      ::SendMessageW(progress_, PBM_SETRANGE32, 0, 100);
      ::SetWindowSubclass(progress_, &Impl::ProgressProc, 1,
                          reinterpret_cast<DWORD_PTR>(this));
      PlaceChild(progress_, placed_.track);
    }
  }

  // A child at `rect` in points, on whole pixels.
  void PlaceChild(HWND child, const D2D1_RECT_F& rect) {
    const int left = Pixels(rect.left);
    const int top = Pixels(rect.top);
    ::SetWindowPos(child, nullptr, left, top, Pixels(rect.right) - left,
                   Pixels(rect.bottom) - top, SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // The box's description: its size, then its line (11.12.9).
  std::wstring BoxDescription() const {
    const std::u16string& size = screen_.box->size;
    return base::UTF16ToWide(
        size.empty() ? screen_.box->line
                     : base::StrCat({size, u". ", screen_.box->line}));
  }

  // The window's size for the screen: 440 wide, as high as it lays out,
  // its top where it was (centred on the work area the first time).
  void ResizeToScreen() {
    const int width = Pixels(kWidth);
    const int height = Pixels(placed_.height);
    RECT frame = {};
    ::GetWindowRect(hwnd_, &frame);
    int left = frame.left;
    int top = frame.top;
    if (first_show_) {
      POINT cursor = {};
      ::GetCursorPos(&cursor);
      MONITORINFO monitor = {.cbSize = sizeof(monitor)};
      ::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY),
                        &monitor);
      const RECT& work = monitor.rcWork;
      left = work.left + (work.right - work.left - width) / 2;
      top = work.top + (work.bottom - work.top - height) / 2;
    }
    ::SetWindowPos(hwnd_, nullptr, left, top, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // MARK: Frames

  bool Animating() const {
    for (const Control& control : controls_) {
      if (control.hover.Moving() || control.press.Moving()) {
        return true;
      }
    }
    for (const Fade& fade : caption_hover_) {
      if (fade.Moving()) {
        return true;
      }
    }
    return false;
  }

  void UpdateTimer() {
    const bool wanted = screen_.bar.has_value() || Animating();
    if (wanted && !timer_) {
      ::SetTimer(hwnd_, kFrameTimer, kFrameMilliseconds, nullptr);
      timer_ = true;
    } else if (!wanted && timer_) {
      ::KillTimer(hwnd_, kFrameTimer);
      timer_ = false;
    }
  }

  void Tick() {
    if (screen_.bar) {
      std::u16string right;
      const double fraction = std::clamp(delegate_->BarAt(right), 0.0, 1.0);
      const bool moved = fraction != bar_fraction_;
      const bool relabeled = !right.empty() && right != bar_right_;
      if (moved) {
        bar_fraction_ = fraction;
        if (progress_) {
          // The percent the label beside the bar shows.
          ::SendMessageW(progress_, PBM_SETPOS,
                         static_cast<WPARAM>(std::floor(fraction * 100)), 0);
          ::InvalidateRect(progress_, nullptr, FALSE);
        }
      }
      if (relabeled) {
        bar_right_ = right;
        const RECT labels = {0, Pixels(placed_.labels_top), Pixels(kWidth),
                             Pixels(placed_.labels_top + kLabelLine)};
        ::InvalidateRect(hwnd_, &labels, FALSE);
      }
    }
    for (Control& control : controls_) {
      if (control.hover.Moving() || control.press.Moving()) {
        ::InvalidateRect(control.hwnd, nullptr, FALSE);
      }
    }
    for (size_t i = 0; i < caption_hover_.size(); ++i) {
      if (caption_hover_[i].Moving()) {
        const RECT button = CaptionPixels(i);
        ::InvalidateRect(hwnd_, &button, FALSE);
      }
    }
    UpdateTimer();
  }

  // MARK: Painting

  void PaintWindow(HDC dc) {
    RECT client = {};
    ::GetClientRect(hwnd_, &client);
    if (!painter_.Begin(dc, client, static_cast<float>(dpi_),
                        ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST))) {
      return;
    }
    const Palette& colors = *palette_;
    painter_.FillRect(
        D2D1::RectF(0, 0, client.right / scale(), client.bottom / scale()),
        colors.card);
    PaintIcon();
    painter_.Text(placed_.title.Get(), placed_.title_at, colors.foreground);
    if (placed_.message) {
      painter_.Text(placed_.message.Get(), placed_.message_at, colors.message);
    }
    if (screen_.bar) {
      painter_.Text(placed_.left_label.Get(),
                    D2D1::Point2F(kPaddingX, placed_.labels_top), colors.muted);
      const std::u16string& right =
          bar_right_.empty() ? screen_.bar->right : bar_right_;
      if (!right.empty()) {
        ComPtr<IDWriteTextLayout> numbers =
            fonts_.Layout(Style::kNumbers, right, 0);
        painter_.Text(numbers.Get(),
                      D2D1::Point2F(kPaddingX + kContentWidth -
                                        Fonts::Width(numbers.Get()),
                                    placed_.labels_top),
                      colors.muted);
      }
    }
    for (size_t i = 0; i < caption_hover_.size(); ++i) {
      PaintCaptionButton(i);
    }
    painter_.End();
  }

  // The App icon's sky as Automatic shows it: Day in Windows' light, Night
  // in its dark; edged in WindowText under a contrast theme.
  void PaintIcon() {
    const int resource =
        appearance_.dark && !appearance_.contrast ? IDR_SKY_NIGHT : IDR_SKY_DAY;
    painter_.Icon(resource, Pixels(kIconSize), placed_.icon);
    if (appearance_.contrast) {
      painter_.StrokeSquircle(Outset(placed_.icon, 1), kIconEdgeRadius + 1, 1,
                              palette_->edge);
    }
  }

  D2D1_RECT_F CaptionRect(size_t index) const {
    // 0: Minimize, 1: Close, in the corner.
    const float right = kWidth - (index == 1 ? 0 : kCaptionWidth + kCaptionGap);
    return D2D1::RectF(right - kCaptionWidth, 0, right, kCaptionHeight);
  }

  RECT CaptionPixels(size_t index) const {
    const D2D1_RECT_F rect = CaptionRect(index);
    return {Pixels(rect.left), Pixels(rect.top), Pixels(rect.right),
            Pixels(rect.bottom)};
  }

  void PaintCaptionButton(size_t index) {
    const Palette& colors = *palette_;
    const D2D1_RECT_F rect = CaptionRect(index);
    const bool close = index == 1;
    const float hover = caption_hover_[index].Value();
    const bool pressed = caption_pressed_ == static_cast<int>(index);
    Color fill;
    if (pressed) {
      fill = close ? colors.close_press_fill : colors.glyph_press_fill;
    } else {
      fill = Faded(close ? colors.close_fill : colors.glyph_hover_fill, hover);
    }
    painter_.FillRect(rect, fill);
    Color glyph = colors.glyph;
    if (close || colors.contrast) {
      glyph = Mix(colors.glyph, close ? colors.close_glyph : colors.glyph_lit,
                  pressed ? 1.0f : hover);
    }
    // An inactive window's glyphs at 40%, but under the pointer.
    if (!active_ && hover <= 0 && !pressed) {
      glyph = Faded(glyph, 0.4f);
    }
    const float x = (rect.left + rect.right - kGlyphSize) / 2;
    const float y = (rect.top + rect.bottom - kGlyphSize) / 2;
    if (close) {
      painter_.Line(D2D1::Point2F(x, y),
                    D2D1::Point2F(x + kGlyphSize, y + kGlyphSize), 1.05f,
                    glyph);
      painter_.Line(D2D1::Point2F(x + kGlyphSize, y),
                    D2D1::Point2F(x, y + kGlyphSize), 1.05f, glyph);
    } else {
      // On whole pixels, as Chrome's 1-point line.
      const float middle = (std::floor((y + 5) * scale()) + 0.5f) / scale();
      painter_.Line(D2D1::Point2F(x, middle),
                    D2D1::Point2F(x + kGlyphSize, middle),
                    std::max(1.0f, std::round(scale())) / scale(), glyph);
    }
  }

  Control* ControlFor(HWND hwnd) {
    for (Control& control : controls_) {
      if (control.hwnd == hwnd) {
        return &control;
      }
    }
    return nullptr;
  }

  bool ShowsRing(HWND control) const {
    return ::GetFocus() == control &&
           !(::SendMessageW(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
  }

  // A keyed button, as Pane's ButtonLook and PaintControl draw it.
  void PaintButton(Control& control, HDC dc, const RECT& rect, UINT state) {
    if (!painter_.Begin(dc, rect, static_cast<float>(dpi_),
                        ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST))) {
      return;
    }
    const Palette& colors = *palette_;
    const Placed::Button& placed = placed_.buttons[control.button];
    const bool pressed = (state & CDIS_SELECTED) != 0;
    if (pressed != control.pressed) {
      // A press shows at once, and fades as it lets go.
      control.press.To(pressed ? 1 : 0,
                       !pressed && !appearance_.reduced_motion);
      control.pressed = pressed;
    }
    const float hover = control.hover.Value();
    const float press = control.press.Value();
    painter_.FillRect(
        D2D1::RectF(0, 0, rect.right / scale(), rect.bottom / scale()),
        colors.card);
    const D2D1_RECT_F shape =
        D2D1::RectF(kRingRoom, kRingRoom,
                    kRingRoom + placed.shape.right - placed.shape.left,
                    kRingRoom + kButtonHeight);
    const Screen::ButtonKind kind = placed.button.kind;
    const bool strong = kind != Screen::ButtonKind::kOutline;
    Color ground = colors.card;
    Color text = colors.foreground;
    Color chip = colors.chip;
    Color chip_text = colors.chip_text;
    if (strong) {
      const Color fill = kind == Screen::ButtonKind::kPrimary
                             ? colors.primary
                             : colors.destructive;
      // Lit, the fill at 90% (lit_fill_opacity).
      ground = Over(Faded(fill, colors.contrast ? 1 : 1 - 0.1f * hover),
                    colors.card);
      text = kind == Screen::ButtonKind::kPrimary ? colors.primary_text
                                                  : colors.destructive_text;
      chip = colors.primary_chip;
      chip_text = colors.primary_chip_text;
      ground = Over(Faded(colors.press_on_strong, press), ground);
    } else {
      ground = Over(Faded(colors.outline_hover, hover), ground);
      ground = Over(Faded(colors.press, press), ground);
      text = Mix(colors.foreground, colors.outline_lit_text, hover);
    }
    painter_.FillSquircle(shape, kButtonRadius, ground);
    if (!strong) {
      painter_.StrokeSquircle(shape, kButtonRadius, 1, colors.outline_edge);
    }
    const float label_top = shape.top + (kButtonHeight - 20) / 2;
    painter_.Text(placed.label.Get(),
                  D2D1::Point2F(shape.left + kButtonLeft, label_top),
                  Over(text, ground));
    if (placed.key) {
      const D2D1_RECT_F chip_rect = D2D1::RectF(
          shape.left + kButtonLeft + placed.label_width + kLabelToChip,
          shape.top + (kButtonHeight - kChipHeight) / 2,
          shape.left + kButtonLeft + placed.label_width + kLabelToChip +
              placed.key_width + 2 * kChipPadding,
          shape.top + (kButtonHeight + kChipHeight) / 2);
      const Color chip_ground = Over(chip, ground);
      painter_.FillSquircle(chip_rect, kChipRadius, chip_ground);
      if (colors.contrast) {
        painter_.StrokeSquircle(chip_rect, kChipRadius, 1, chip_text);
      }
      painter_.Text(placed.key.Get(),
                    D2D1::Point2F(chip_rect.left + kChipPadding,
                                  chip_rect.top + (kChipHeight - 16) / 2),
                    Over(chip_text, chip_ground));
    }
    if (ShowsRing(control.hwnd)) {
      // 2 points in the text's colour just outside the edge; on a strong
      // fill 1 point off it, the card between (11.17 B, 11.18).
      const float off = strong ? kRingOffFill : 0;
      painter_.StrokeSquircle(Outset(shape, off + kRingWidth),
                              kButtonRadius + off + kRingWidth, kRingWidth,
                              colors.ring);
    }
    painter_.End();
  }

  // The box and its words (11.12.9).
  void PaintBox(HDC dc, const RECT& rect, UINT state) {
    if (!painter_.Begin(dc, rect, static_cast<float>(dpi_),
                        ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST))) {
      return;
    }
    const Palette& colors = *palette_;
    const bool ticked = ::SendMessageW(box_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    painter_.FillRect(
        D2D1::RectF(0, 0, rect.right / scale(), rect.bottom / scale()),
        colors.card);
    const float left = kRingWidth;
    const float top = kRingWidth;
    const D2D1_RECT_F box = D2D1::RectF(left, top + kBoxDown, left + kBoxSize,
                                        top + kBoxDown + kBoxSize);
    if (ticked) {
      painter_.FillSquircle(box, kBoxRadius, colors.box_fill);
      // The check, in its 12-point box (M2.5 6.2 5 8.6 9.6 3.6), 1.6 wide,
      // round.
      const float x = box.left + 3;
      const float y = box.top + 3;
      ComPtr<ID2D1PathGeometry> check;
      Graphics::Get().d2d->CreatePathGeometry(&check);
      ComPtr<ID2D1GeometrySink> sink;
      if (check && SUCCEEDED(check->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(x + 2.5f, y + 6.2f),
                          D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(x + 5, y + 8.6f));
        sink->AddLine(D2D1::Point2F(x + 9.6f, y + 3.6f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        ComPtr<ID2D1StrokeStyle> round;
        Graphics::Get().d2d->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
            nullptr, 0, &round);
        ComPtr<ID2D1SolidColorBrush> brush;
        painter_.target()->CreateSolidColorBrush(
            ToD2D(Over(colors.box_check, colors.box_fill)), &brush);
        painter_.target()->DrawGeometry(check.Get(), brush.Get(), 1.6f,
                                        round.Get());
      }
    } else {
      painter_.FillSquircle(box, kBoxRadius, colors.card);
      painter_.StrokeSquircle(box, kBoxRadius, 1,
                              Over(colors.box_edge, colors.card));
    }
    if (ShowsRing(box_)) {
      painter_.StrokeSquircle(Outset(box, kRingWidth), kBoxRadius + kRingWidth,
                              kRingWidth, colors.ring);
    }
    const Screen::Box& words = *screen_.box;
    const float text_left = left + kBoxSize + kBoxToText;
    const float text_width = kContentWidth - kBoxSize - kBoxToText;
    ComPtr<IDWriteTextLayout> name =
        fonts_.Layout(Style::kBoxName, words.label, 0);
    painter_.Text(name.Get(), D2D1::Point2F(text_left, top), colors.foreground);
    if (!words.size.empty()) {
      // On the name's baseline: a 12/16 line under a 14/20 one sits 3 lower
      // (each centred in its line, the baselines meeting).
      ComPtr<IDWriteTextLayout> size =
          fonts_.Layout(Style::kBoxSize, words.size, 0);
      std::array<DWRITE_LINE_METRICS, 1> name_line = {};
      std::array<DWRITE_LINE_METRICS, 1> size_line = {};
      UINT32 count = 0;
      name->GetLineMetrics(name_line.data(), 1, &count);
      size->GetLineMetrics(size_line.data(), 1, &count);
      painter_.Text(
          size.Get(),
          D2D1::Point2F(text_left + Fonts::Width(name.Get()) + kNameToSize,
                        top + name_line[0].baseline - size_line[0].baseline),
          colors.muted);
    }
    ComPtr<IDWriteTextLayout> line =
        fonts_.Layout(Style::kBoxLine, words.line, text_width);
    painter_.Text(line.Get(),
                  D2D1::Point2F(text_left, top + kNameLine + kNameToLine),
                  colors.muted);
    painter_.End();
  }

  // The bar: a capsule track, the fill over it (11.12.6); 6 high in a
  // WindowText edge under a contrast theme (11.12.8).
  void PaintProgress(HWND progress, HDC dc) {
    RECT rect = {};
    ::GetClientRect(progress, &rect);
    if (!painter_.Begin(dc, rect, static_cast<float>(dpi_),
                        ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST))) {
      return;
    }
    const Palette& colors = *palette_;
    const float width = rect.right / scale();
    const float height = rect.bottom / scale();
    painter_.FillRect(D2D1::RectF(0, 0, width, height), colors.card);
    const D2D1_RECT_F track = D2D1::RectF(0, 0, width, height);
    painter_.FillCapsule(track, colors.track);
    // Whole pixels, as the board's round(62%, 1px).
    const float filled =
        std::round(static_cast<float>(bar_fraction_) * width * scale()) /
        scale();
    if (filled > 0) {
      painter_.FillCapsule(D2D1::RectF(0, 0, filled, height), colors.fill);
    }
    if (colors.contrast) {
      painter_.StrokeCapsule(track, 1, colors.edge);
    }
    painter_.End();
  }

  // MARK: Input

  // Enter and Esc, before the dialog's own keys: Enter presses the focused
  // button, or the screen's default when none has the focus (the box
  // ticked, nothing); Esc its Cancel or Close, or closes as Close does.
  bool TakesKey(const MSG& message) {
    if (message.message != WM_KEYDOWN ||
        (message.hwnd != hwnd_ && !::IsChild(hwnd_, message.hwnd))) {
      return false;
    }
    if (message.wParam == VK_RETURN) {
      if (Control* focused = ControlFor(::GetFocus())) {
        Press(focused->id);
      } else if (screen_.enter) {
        Press(screen_.enter);
      }
      return true;
    }
    if (message.wParam == VK_ESCAPE) {
      if (screen_.escape) {
        Press(screen_.escape);
      } else if (delegate_->OnCloseAsked()) {
        Close();
      }
      return true;
    }
    return false;
  }

  void Press(int id) {
    // The delegate may show another screen, which ends this one's controls.
    delegate_->OnButton(id);
  }

  int CaptionAt(POINT client) const {
    for (size_t i = 0; i < caption_hover_.size(); ++i) {
      const RECT rect = CaptionPixels(i);
      if (::PtInRect(&rect, client)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void HoverCaption(int index) {
    for (size_t i = 0; i < caption_hover_.size(); ++i) {
      const float target = static_cast<int>(i) == index ? 1 : 0;
      if (caption_hover_[i].target() != target) {
        caption_hover_[i].To(target, !appearance_.reduced_motion);
        const RECT rect = CaptionPixels(i);
        ::InvalidateRect(hwnd_, &rect, FALSE);
      }
    }
    UpdateTimer();
  }

  void Refresh() {
    const Appearance appearance = CurrentAppearance();
    if (appearance == appearance_) {
      return;
    }
    const bool contrast_changed = appearance.contrast != appearance_.contrast;
    appearance_ = appearance;
    palette_ = std::make_unique<Palette>(appearance_);
    ApplyFrame();
    if (contrast_changed && screen_.bar) {
      // The bar is 2 higher in a contrast theme.
      const Screen screen = screen_;
      Show(screen);
      return;
    }
    ::RedrawWindow(hwnd_, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }

  // MARK: Messages

  static LRESULT CALLBACK WindowProc(HWND hwnd,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* impl = static_cast<Impl*>(create->lpCreateParams);
      impl->hwnd_ = hwnd;
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(impl));
    }
    auto* impl =
        reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!impl) {
      return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return impl->Handle(message, wparam, lparam);
  }

  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_NCCALCSIZE:
        // The client is the whole window: no title band (11.12.6).
        if (wparam) {
          return 0;
        }
        break;
      case WM_ACTIVATE:
        // As a dialog keeps it: the control that had the focus has it again
        // when the window comes back to the front (DefDlgProc's way, and
        // Pane's own windows'), where DefWindowProc would give it to the
        // window and lose the keyboard's place.
        if (LOWORD(wparam) == WA_INACTIVE) {
          HWND focus = ::GetFocus();
          focus_before_ =
              focus && ::IsChild(hwnd_, focus) ? ::GetDlgCtrlID(focus) : 0;
        } else if (!HIWORD(wparam)) {
          HWND focus =
              focus_before_ ? ::GetDlgItem(hwnd_, focus_before_) : nullptr;
          ::SetFocus(focus ? focus : hwnd_);
          return 0;
        }
        break;
      case WM_NCACTIVATE:
        // Nothing of the system's caption to draw.
        active_ = wparam != FALSE;
        for (size_t i = 0; i < caption_hover_.size(); ++i) {
          const RECT rect = CaptionPixels(i);
          ::InvalidateRect(hwnd_, &rect, FALSE);
        }
        return ::DefWindowProcW(hwnd_, message, wparam, -1);
      case WM_NCHITTEST: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ::ScreenToClient(hwnd_, &point);
        if (CaptionAt(point) >= 0) {
          return HTCLIENT;
        }
        // The card moves the window, as a title band would.
        return HTCAPTION;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = ::BeginPaint(hwnd_, &paint);
        PaintWindow(dc);
        ::EndPaint(hwnd_, &paint);
        return 0;
      }
      case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header->code == NM_CUSTOMDRAW) {
          auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lparam);
          if (draw->dwDrawStage != CDDS_PREPAINT) {
            return CDRF_DODEFAULT;
          }
          if (Control* control = ControlFor(header->hwndFrom)) {
            PaintButton(*control, draw->hdc, draw->rc, draw->uItemState);
            UpdateTimer();
            return CDRF_SKIPDEFAULT;
          }
          if (header->hwndFrom == box_) {
            PaintBox(draw->hdc, draw->rc, draw->uItemState);
            return CDRF_SKIPDEFAULT;
          }
          return CDRF_DODEFAULT;
        }
        if (header->code == BCN_HOTITEMCHANGE) {
          auto* hot = reinterpret_cast<NMBCHOTITEM*>(lparam);
          const bool entering = (hot->dwFlags & HICF_ENTERING) != 0;
          if (Control* control = ControlFor(header->hwndFrom)) {
            control->hover.To(entering ? 1 : 0, !appearance_.reduced_motion);
            ::InvalidateRect(control->hwnd, nullptr, FALSE);
          }
          UpdateTimer();
          return 0;
        }
        break;
      }
      case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
          const int id = LOWORD(wparam);
          if (id == kBoxId && box_) {
            const bool ticked =
                ::SendMessageW(box_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            delegate_->OnBoxToggled(ticked);
          } else if (ControlFor(reinterpret_cast<HWND>(lparam))) {
            Press(id);
          }
          return 0;
        }
        break;
      case WM_TIMER:
        if (wparam == kFrameTimer) {
          Tick();
          return 0;
        }
        if (wparam == kDescriptionTimer) {
          ::KillTimer(hwnd_, kDescriptionTimer);
          if (window_element_ && ::GetForegroundWindow() == hwnd_) {
            window_element_->SayDescription();
          }
          return 0;
        }
        break;
      case WM_MOUSEMOVE: {
        if (!tracking_) {
          TRACKMOUSEEVENT track = {.cbSize = sizeof(track),
                                   .dwFlags = TME_LEAVE,
                                   .hwndTrack = hwnd_};
          tracking_ = ::TrackMouseEvent(&track);
        }
        HoverCaption(CaptionAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}));
        return 0;
      }
      case WM_MOUSELEAVE:
        tracking_ = false;
        if (caption_pressed_ < 0) {
          HoverCaption(-1);
        }
        return 0;
      case WM_LBUTTONDOWN: {
        const int at = CaptionAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (at >= 0) {
          caption_pressed_ = at;
          ::SetCapture(hwnd_);
          const RECT rect = CaptionPixels(at);
          ::InvalidateRect(hwnd_, &rect, FALSE);
        }
        return 0;
      }
      case WM_LBUTTONUP: {
        const int pressed = std::exchange(caption_pressed_, -1);
        if (pressed >= 0) {
          ::ReleaseCapture();
          const RECT rect = CaptionPixels(pressed);
          ::InvalidateRect(hwnd_, &rect, FALSE);
          const int at =
              CaptionAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
          HoverCaption(at);
          if (at == pressed) {
            if (pressed == 0) {
              ::ShowWindow(hwnd_, SW_MINIMIZE);
            } else {
              ::PostMessageW(hwnd_, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
          }
        }
        return 0;
      }
      case WM_GETOBJECT:
        // UI Automation's element for the window (uia_element.h); MSAA's
        // stays Windows' own.
        if (static_cast<LONG>(lparam) == static_cast<LONG>(UiaRootObjectId) &&
            window_element_) {
          // The first screen's message, said once, after the name a screen
          // reader says as it comes: an event raised before any client has
          // reached the window goes nowhere, so the first activation's went
          // unsaid (commit 490757e).
          if (!description_said_) {
            description_said_ = true;
            ::SetTimer(hwnd_, kDescriptionTimer, kDescriptionMilliseconds,
                       nullptr);
          }
          return window_element_->Answer(wparam, lparam);
        }
        break;
      case WM_CLOSE:
        if (delegate_->OnCloseAsked()) {
          Close();
        }
        return 0;
      case WM_DESTROY:
        if (window_element_) {
          window_element_->Detach();
        }
        ::KillTimer(hwnd_, kFrameTimer);
        ::KillTimer(hwnd_, kDescriptionTimer);
        timer_ = false;
        hwnd_ = nullptr;
        ::PostQuitMessage(0);
        return 0;
      case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const RECT* suggested = reinterpret_cast<RECT*>(lparam);
        ::SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        const Screen screen = screen_;
        Show(screen);
        return 0;
      }
      case WM_SETTINGCHANGE:
      case WM_SYSCOLORCHANGE:
      case WM_THEMECHANGED:
        Refresh();
        break;
      case kTaskMessage: {
        std::unique_ptr<base::OnceClosure> task(
            reinterpret_cast<base::OnceClosure*>(lparam));
        std::move(*task).Run();
        return 0;
      }
    }
    return ::DefWindowProcW(hwnd_, message, wparam, lparam);
  }

  // The box answers for itself to UI Automation (uia_element.h).
  static LRESULT CALLBACK BoxProc(HWND hwnd,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam,
                                  UINT_PTR id,
                                  DWORD_PTR data) {
    auto* impl = reinterpret_cast<Impl*>(data);
    switch (message) {
      case WM_GETOBJECT:
        if (static_cast<LONG>(lparam) == static_cast<LONG>(UiaRootObjectId) &&
            impl->box_element_) {
          return impl->box_element_->Answer(wparam, lparam);
        }
        break;
      case WM_DESTROY:
        if (impl->box_element_) {
          impl->box_element_->Detach();
        }
        break;
      case WM_NCDESTROY:
        ::RemoveWindowSubclass(hwnd, &Impl::BoxProc, id);
        break;
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
  }

  static LRESULT CALLBACK ProgressProc(HWND hwnd,
                                       UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam,
                                       UINT_PTR id,
                                       DWORD_PTR data) {
    auto* impl = reinterpret_cast<Impl*>(data);
    switch (message) {
      case WM_ERASEBKGND:
        return 1;
      case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = ::BeginPaint(hwnd, &paint);
        impl->PaintProgress(hwnd, dc);
        ::EndPaint(hwnd, &paint);
        return 0;
      }
      case WM_NCDESTROY:
        ::RemoveWindowSubclass(hwnd, &Impl::ProgressProc, id);
        break;
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
  }

  const raw_ptr<Delegate> delegate_;
  // What Taskbar and Alt+Tab name it.
  const std::wstring title_;
  HWND hwnd_ = nullptr;
  Screen screen_;
  Placed placed_;
  Appearance appearance_;
  std::unique_ptr<Palette> palette_;
  UINT dpi_ = 96;
  Fonts fonts_;
  Painter painter_;
  std::vector<Control> controls_;
  HWND box_ = nullptr;
  HWND progress_ = nullptr;
  std::array<Fade, 2> caption_hover_;
  int caption_pressed_ = -1;
  bool tracking_ = false;
  bool active_ = true;
  bool timer_ = false;
  bool first_show_ = false;
  // The control that had the focus as the window went to the back.
  int focus_before_ = 0;
  double bar_fraction_ = 0;
  std::u16string bar_right_;
  ComPtr<UiaElement> window_element_;
  ComPtr<UiaElement> box_element_;
  // Whether the first screen's message is said, or about to be
  // (kDescriptionTimer).
  bool description_said_ = false;
};

InstallWindow::InstallWindow(Delegate* delegate, std::wstring title)
    : impl_(std::make_unique<Impl>(delegate, std::move(title))) {}

InstallWindow::~InstallWindow() = default;

void InstallWindow::Show(const Screen& screen) {
  impl_->Show(screen);
}

void InstallWindow::SetBoxSize(const std::u16string& size) {
  impl_->SetBoxSize(size);
}

void InstallWindow::Close() {
  impl_->Close();
}

void InstallWindow::Post(base::OnceClosure task) {
  impl_->Post(std::move(task));
}

void InstallWindow::Run() {
  impl_->Run();
}

HWND InstallWindow::hwnd() const {
  return impl_->hwnd();
}

}  // namespace pane::installer
