// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/icons.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/containers/lru_cache.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/path_effect.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"

namespace pane {

namespace {

// Lucide's grid.
constexpr float kGrid = 24;

// Icons drawn at a size, stroke, and color, kept while in use: a hover's tint
// paints with PaintIcon, so this holds a window's few dozen.
constexpr size_t kCachedImages = 128;

// An element's tag and attributes, in order, as an icon's SVG writes them:
// `<tag name="value" .../>`.
struct Element {
  std::string_view tag;
  std::map<std::string_view, std::string_view> attributes;
};

void SkipSpaces(std::string_view& text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
}

// What runs up to `end`, taken off the front of `text`; nullopt without it.
std::optional<std::string_view> TakeUntil(std::string_view& text, char end) {
  const size_t at = text.find(end);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view taken = text.substr(0, at);
  text.remove_prefix(at + 1);
  return taken;
}

std::optional<std::vector<Element>> ReadElements(std::string_view text) {
  std::vector<Element> elements;
  while (true) {
    SkipSpaces(text);
    if (text.empty()) {
      return elements;
    }
    if (text.front() != '<') {
      return std::nullopt;
    }
    text.remove_prefix(1);
    Element element;
    const size_t name_end = text.find(' ');
    if (name_end == std::string_view::npos) {
      return std::nullopt;
    }
    element.tag = text.substr(0, name_end);
    text.remove_prefix(name_end);
    while (true) {
      SkipSpaces(text);
      if (text.starts_with("/>")) {
        text.remove_prefix(2);
        break;
      }
      const std::optional<std::string_view> name = TakeUntil(text, '=');
      if (!name || !text.starts_with('"')) {
        return std::nullopt;
      }
      text.remove_prefix(1);
      const std::optional<std::string_view> value = TakeUntil(text, '"');
      if (!value || !element.attributes.emplace(*name, *value).second) {
        return std::nullopt;
      }
    }
    elements.push_back(std::move(element));
  }
}

// An element's numeric attributes, each required.
template <size_t N>
std::optional<std::array<float, N>> Numbers(
    const Element& element,
    const std::array<std::string_view, N>& names) {
  std::array<float, N> numbers;
  for (size_t i = 0; i < N; ++i) {
    const auto found = element.attributes.find(names[i]);
    double number = 0;
    if (found == element.attributes.end() ||
        !base::StringToDouble(found->second, &number)) {
      return std::nullopt;
    }
    numbers[i] = number;
  }
  return numbers;
}

// Whether the element holds only `names`, of what it may have.
bool OnlyHas(const Element& element,
             std::initializer_list<std::string_view> names) {
  for (const auto& [name, value] : element.attributes) {
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      return false;
    }
  }
  return true;
}

std::optional<IconShape> ShapeOf(const Element& element) {
  IconShape shape;
  if (element.tag == "path") {
    const auto d = element.attributes.find("d");
    if (d == element.attributes.end() || !OnlyHas(element, {"d"})) {
      return std::nullopt;
    }
    std::optional<SkPath> path =
        SkParsePath::FromSVGString(std::string(d->second).c_str());
    if (!path) {
      return std::nullopt;
    }
    shape.path = std::move(*path);
    return shape;
  }
  if (element.tag == "circle") {
    if (!OnlyHas(element,
                 {"cx", "cy", "r", "stroke-dasharray", "fill", "stroke"})) {
      return std::nullopt;
    }
    const auto numbers = Numbers<3>(element, {"cx", "cy", "r"});
    if (!numbers) {
      return std::nullopt;
    }
    const auto [cx, cy, r] = *numbers;
    // SVG's circle starts at its right and runs clockwise, as Skia's does,
    // so a dash falls where the Swift's did.
    shape.path = SkPath::Circle(cx, cy, r, SkPathDirection::kCW);
    const auto fill = element.attributes.find("fill");
    const auto stroke = element.attributes.find("stroke");
    if (fill != element.attributes.end() ||
        stroke != element.attributes.end()) {
      // Filled and not stroked is the one other way an icon draws one.
      if (fill == element.attributes.end() || fill->second == "none" ||
          stroke == element.attributes.end() || stroke->second != "none") {
        return std::nullopt;
      }
      shape.filled = true;
    }
    if (const auto dash = element.attributes.find("stroke-dasharray");
        dash != element.attributes.end()) {
      const std::vector<std::string_view> lengths = base::SplitStringPiece(
          dash->second, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      double on = 0;
      double off = 0;
      if (lengths.size() != 2 || !base::StringToDouble(lengths[0], &on) ||
          !base::StringToDouble(lengths[1], &off)) {
        return std::nullopt;
      }
      shape.dash = std::pair(on, off);
    }
    return shape;
  }
  if (element.tag == "rect") {
    if (!OnlyHas(element, {"width", "height", "x", "y", "rx"})) {
      return std::nullopt;
    }
    const auto numbers =
        Numbers<5>(element, {"x", "y", "width", "height", "rx"});
    if (!numbers) {
      return std::nullopt;
    }
    const auto [x, y, width, height, rx] = *numbers;
    shape.path = SkPath::RRect(
        SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, width, height), rx, rx));
    return shape;
  }
  return std::nullopt;
}

// An icon drawn at one size, stroke, and color, at each scale asked for.
class IconSource : public gfx::CanvasImageSource {
 public:
  IconSource(Icon icon, int size, float stroke_width, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        icon_(icon),
        stroke_width_(stroke_width),
        color_(color) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    PaintIcon(*canvas, icon_, gfx::RectF(gfx::SizeF(size())), stroke_width_,
              color_);
  }

 private:
  const Icon icon_;
  const float stroke_width_;
  const SkColor color_;
};

gfx::ImageSkia CachedIcon(Icon icon,
                          int size,
                          float stroke_width,
                          SkColor color) {
  using Key = std::tuple<Icon, int, float, SkColor>;
  static base::NoDestructor<base::LRUCache<Key, gfx::ImageSkia>> images(
      kCachedImages);
  const Key key(icon, size, stroke_width, color);
  auto found = images->Get(key);
  if (found == images->end()) {
    found = images->Put(key, gfx::CanvasImageSource::MakeImageSkia<IconSource>(
                                 icon, size, stroke_width, color));
  }
  return found->second;
}

}  // namespace

IconShape::IconShape() = default;
IconShape::IconShape(const IconShape&) = default;
IconShape& IconShape::operator=(const IconShape&) = default;
IconShape::~IconShape() = default;

std::string_view IconElements(Icon icon) {
  switch (icon) {
    case Icon::kArrowLeft:
      return R"(<path d="m12 19-7-7 7-7"/><path d="M19 12H5"/>)";
    case Icon::kChevronDown:
      return R"(<path d="m6 9 6 6 6-6"/>)";
    case Icon::kChevronLeft:
      return R"(<path d="m15 18-6-6 6-6"/>)";
    case Icon::kChevronRight:
      return R"(<path d="m9 18 6-6-6-6"/>)";
    case Icon::kChevronUp:
      return R"(<path d="m18 15-6-6-6 6"/>)";
    case Icon::kCircleDashedX:
      return R"(<circle cx="12" cy="12" r="9" stroke-dasharray="2.6 2.6"/>)"
             R"(<path d="M9.5 9.5l5 5"/><path d="M14.5 9.5l-5 5"/>)";
    case Icon::kChevronsDownUp:
      return R"(<path d="m7 20 5-5 5 5"/><path d="m7 4 5 5 5-5"/>)";
    case Icon::kChevronsUpDown:
      return R"(<path d="m7 15 5 5 5-5"/><path d="m7 9 5-5 5 5"/>)";
    case Icon::kExternalLink:
      return R"(<path d="M15 3h6v6"/><path d="M10 14 21 3"/>)"
             R"(<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 )"
             R"(2-2h6"/>)";
    case Icon::kGlobe:
      return R"(<circle cx="12" cy="12" r="10"/>)"
             R"(<path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/>)"
             R"(<path d="M2 12h20"/>)";
    case Icon::kKeyboard:
      return R"(<rect width="20" height="16" x="2" y="4" rx="2"/>)"
             R"(<path d="M6 8h.01M10 8h.01M14 8h.01M18 8h.01M8 12h.01M12 )"
             R"(12h.01M16 12h.01M7 16h10"/>)";
    case Icon::kMonitor:
      return R"(<rect width="20" height="14" x="2" y="3" rx="2"/>)"
             R"(<path d="M8 21h8"/><path d="M12 17v4"/>)";
    case Icon::kMoon:
      return R"(<path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.)"
             R"(46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401"/>)";
    case Icon::kPencil:
      return R"(<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 )"
             R"(2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 )"
             R"(2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>)";
    case Icon::kPlus:
      return R"(<path d="M5 12h14"/><path d="M12 5v14"/>)";
    case Icon::kRotateCw:
      return R"(<path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 )"
             R"(8"/><path d="M21 3v5h-5"/>)";
    case Icon::kSearch:
      return R"(<path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/>)";
    case Icon::kSettings:
      return R"(<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 )"
             R"(0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 )"
             R"(3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 )"
             R"(2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 )"
             R"(0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 )"
             R"(6.051a2.34 2.34 0 0 0 3.319-1.915"/>)"
             R"(<circle cx="12" cy="12" r="3"/>)";
    case Icon::kSquareArrowOutUpRight:
      return R"(<path d="M21 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 )"
             R"(2-2h6"/><path d="m21 3-9 9"/><path d="M15 3h6v6"/>)";
    case Icon::kSun:
      return R"(<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/>)"
             R"(<path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/>)"
             R"(<path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/>)"
             R"(<path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/>)"
             R"(<path d="m19.07 4.93-1.41 1.41"/>)";
    case Icon::kTimerOff:
      return R"(<path d="M10 2h4"/>)"
             R"(<path d="M4.6 11a8 8 0 0 0 1.7 8.7 8 8 0 0 0 8.7 1.7"/>)"
             R"(<path d="M7.4 7.4a8 8 0 0 1 10.3 1 8 8 0 0 1 .9 10.2"/>)"
             R"(<path d="m2 2 20 20"/><path d="M12 12v-2"/>)";
    case Icon::kTrash2:
      return R"(<path d="M10 11v6"/><path d="M14 11v6"/>)"
             R"(<path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/>)"
             R"(<path d="M3 6h18"/>)"
             R"(<path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>)";
    case Icon::kTriangleAlert:
      return R"(<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 )"
             R"(21h16a2 2 0 0 0 1.73-3"/><path d="M12 9v4"/>)"
             R"(<path d="M12 17h.01"/>)";
    case Icon::kX:
      return R"(<path d="M18 6 6 18"/><path d="m6 6 12 12"/>)";
    case Icon::kDownload:
      return R"(<path d="M12 3.25v14"/>)"
             R"(<path d="m7.25 12.5 4.75 4.75 4.75-4.75"/>)"
             R"(<path d="M5 20.75h14"/>)";
    case Icon::kEllipsis:
      return R"(<circle cx="3.4" cy="12" r="1.9" fill="black" )"
             R"(stroke="none"/><circle cx="12" cy="12" r="1.9" fill="black" )"
             R"(stroke="none"/><circle cx="20.6" cy="12" r="1.9" )"
             R"(fill="black" stroke="none"/>)";
    case Icon::kPin:
      return R"(<path d="M12 14.5V21"/><path d="M8 9V6a2.5 2.5 0 0 1 )"
             R"(2.5-2.5h3A2.5 2.5 0 0 1 16 6v3l3.3 5.5H4.7Z"/>)";
    case Icon::kPinOff:
      // The pin stops short of the stroke on its upper side, and meets it
      // below.
      return R"(<path d="M5.5 14.5h-.8L8 9V6a2.5 2.5 0 0 1 2.5-2.5h3a2.5 )"
             R"(2.5 0 0 1 2 1"/><path d="M16 8v1l3.3 5.5H9.5"/>)"
             R"(<path d="M12 14.5V21"/><path d="M4.25 19.75 19.75 4.25"/>)";
    case Icon::kPuzzle:
      return R"(<path d="M6 6.25H9.67A2.35 2.35 0 1 1 14.33 6.25H18A2 2 0 0 1 )"
             R"(20 8.25V10.88A2.5 2.5 0 0 0 20 15.88V18.5A2 2 0 0 1 18 )"
             R"(20.5H6A2 2 0 0 1 4 18.5V8.25A2 2 0 0 1 6 6.25Z"/>)";
  }
}

std::optional<std::vector<IconShape>> ParseIconElements(
    std::string_view elements) {
  const std::optional<std::vector<Element>> read = ReadElements(elements);
  if (!read || read->empty()) {
    return std::nullopt;
  }
  std::vector<IconShape> shapes;
  for (const Element& element : *read) {
    std::optional<IconShape> shape = ShapeOf(element);
    if (!shape) {
      return std::nullopt;
    }
    shapes.push_back(std::move(*shape));
  }
  return shapes;
}

const std::vector<IconShape>& IconShapes(Icon icon) {
  static base::NoDestructor<std::map<Icon, std::vector<IconShape>>> parsed;
  auto found = parsed->find(icon);
  if (found == parsed->end()) {
    std::optional<std::vector<IconShape>> shapes =
        ParseIconElements(IconElements(icon));
    // Each icon's elements are this file's own, and a test parses them all.
    CHECK(shapes);
    found = parsed->emplace(icon, std::move(*shapes)).first;
  }
  return found->second;
}

void PaintIcon(gfx::Canvas& canvas,
               Icon icon,
               const gfx::RectF& bounds,
               float stroke_width,
               SkColor color) {
  gfx::ScopedCanvas scoped(&canvas);
  canvas.sk_canvas()->translate(bounds.x(), bounds.y());
  const float scale = bounds.width() / kGrid;
  canvas.sk_canvas()->scale(scale, scale);
  for (const IconShape& shape : IconShapes(icon)) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    if (shape.filled) {
      flags.setStyle(cc::PaintFlags::kFill_Style);
    } else {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(stroke_width);
      flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
      flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
      if (shape.dash) {
        const float intervals[] = {shape.dash->first, shape.dash->second};
        flags.setPathEffect(cc::PathEffect::MakeDash(intervals, 2, 0));
      }
    }
    canvas.DrawPath(shape.path, flags);
  }
}

ui::ImageModel IconImage(Icon icon,
                         int size,
                         float stroke_width,
                         ui::ColorVariant color) {
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](Icon icon, int size, float stroke_width, ui::ColorVariant color,
             const ui::ColorProvider* provider) {
            return CachedIcon(icon, size, stroke_width,
                              color.ResolveToSkColor(provider));
          },
          icon, size, stroke_width, std::move(color)),
      gfx::Size(size, size));
}

}  // namespace pane
