#include "DashboardTiles.h"

#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "I18nKeys.h"

namespace fui = freeink::ui;

namespace dashboard_tiles {
namespace {

constexpr int16_t GAP = 8;
constexpr int16_t CARD_COLUMNS = 2;
constexpr int16_t BAR_HEIGHT = 36;
constexpr int16_t SWATCH = 18;

// 8x8 fills for breakdown slices, darkest first so the biggest consumer is
// darkest. The last, empty one is kept for "Other".
constexpr uint8_t PATTERNS[][8] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // solid
    {0xFF, 0x55, 0xFF, 0x55, 0xFF, 0x55, 0xFF, 0x55},  // 75 %
    {0xCC, 0x66, 0x33, 0x99, 0xCC, 0x66, 0x33, 0x99},  // wide diagonal
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},  // checker
    {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00},  // horizontal bands
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},  // cross-hatch
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88},  // vertical lines
    {0xFF, 0x80, 0x80, 0x80, 0xFF, 0x80, 0x80, 0x80},  // grid
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},  // thin diagonal
    {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00},  // dots
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // empty
};
constexpr size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);
constexpr size_t OTHER_PATTERN = PATTERN_COUNT - 1;

fui::Paint patternPaint(const dashboard::Slice& slice, const size_t index) {
  fui::BitmapRef ref;
  ref.data = PATTERNS[slice.row ? index % OTHER_PATTERN : OTHER_PATTERN];
  ref.width = 8;
  ref.height = 8;
  return fui::Paint::bitmapFill({ref, fui::BitmapMode::Tile});
}

fui::Rect sub(const fui::Rect r, const int x, const int y, const int w, const int h) {
  return {static_cast<int16_t>(r.x + x), static_cast<int16_t>(r.y + y), static_cast<int16_t>(w),
          static_cast<int16_t>(h)};
}

class Painter {
 public:
  Painter(UiAppHost::UiScreen& screen, const dashboard::Cache& cache)
      : screen(screen), target(screen.target()), cache(cache) {
    small = screen.theme().smallText;
    body = screen.theme().bodyText;
    title = screen.theme().titleText;
    heading = small;
    heading.bold = true;
    smallLine = target.lineHeight(small.font);
    bodyLine = target.lineHeight(body.font);
    titleLine = target.lineHeight(title.font);
  }

  // Height used, or -1 when the section doesn't fit `rect`.
  int section(const fui::Rect rect, const dashboard::Section& section) {
    switch (section.style) {
      case dashboard::Style::Breakdown:
        return breakdown(rect, section);
      case dashboard::Style::Rows:
        return rows(rect, section);
      case dashboard::Style::Cards:
      default:
        return cards(rect, section);
    }
  }

 private:
  static std::vector<const dashboard::Row*> shownRows(const dashboard::Section& section) {
    std::vector<const dashboard::Row*> out;
    out.reserve(section.rows.size());
    for (const auto& row : section.rows) {
      if (!row.toggle) out.push_back(&row);
    }
    return out;
  }

  int headingHeight(const dashboard::Section& section) const { return section.title.empty() ? 0 : smallLine + 4; }

  void drawHeading(const fui::Rect rect, const dashboard::Section& section, const char* trailing = nullptr) {
    if (section.title.empty()) return;
    target.text(sub(rect, 0, 0, rect.width, smallLine), section.title.c_str(), heading);
    if (trailing) {
      fui::TextStyle right = heading;
      right.align = fui::TextAlign::Right;
      target.text(sub(rect, 0, 0, rect.width, smallLine), trailing, right);
    }
  }

  int cards(const fui::Rect rect, const dashboard::Section& section) {
    const auto shown = shownRows(section);
    if (shown.empty()) return 0;
    bool anyCaption = false;
    for (const auto* row : shown) anyCaption |= row->entities.size() > 1;

    const fui::Insets padding{6, 8, 6, 8};
    const int cardHeight = padding.top + smallLine + 2 + titleLine + (anyCaption ? 2 + smallLine : 0) + padding.bottom;
    const int columns = shown.size() == 1 ? 1 : CARD_COLUMNS;
    const int lines = (static_cast<int>(shown.size()) + columns - 1) / columns;
    const int top = headingHeight(section);
    const int height = top + lines * cardHeight + (lines - 1) * GAP;
    if (height > rect.height) return -1;

    drawHeading(rect, section);
    static const fui::StyleSet styles = [] {
      fui::StyleSet set;
      set.explicitlySet = true;
      set.normal.background = fui::Paint::solid(fui::Color::White);
      set.normal.foreground = fui::Paint::solid(fui::Color::Black);
      set.normal.border = fui::Paint::solid(fui::Color::Black);
      set.normal.borderWidth = 1;
      set.normal.radius = 6;
      return set;
    }();

    const int cardWidth = (rect.width - (columns - 1) * GAP) / columns;
    for (size_t i = 0; i < shown.size(); ++i) {
      const int col = static_cast<int>(i) % columns;
      const int line = static_cast<int>(i) / columns;
      const dashboard::Reading value = dashboard::reading(*shown[i], 0, cache);
      const std::string caption = dashboard::restText(*shown[i], cache);

      fui::MetricCardProps card;
      card.label = shown[i]->label.c_str();
      card.value = value.value.c_str();
      card.unit = value.unit.empty() ? nullptr : value.unit.c_str();
      card.caption = caption.empty() ? nullptr : caption.c_str();
      card.labelText = small;
      card.valueText = title;
      card.captionText = small;
      card.styles = styles;
      card.padding = padding;
      card.gap = 4;
      fui::metricCard(screen.frame(),
                      sub(rect, col * (cardWidth + GAP), top + line * (cardHeight + GAP), cardWidth, cardHeight), card);
    }
    return height;
  }

  int breakdown(const fui::Rect rect, const dashboard::Section& section) {
    const std::vector<dashboard::Slice> slices = dashboard::breakdown(section, cache);
    if (slices.empty()) return 0;
    const int top = headingHeight(section);
    const int legendLine = std::max<int>(smallLine + 6, SWATCH + 4);
    const int minHeight = top + BAR_HEIGHT + GAP + legendLine;
    if (minHeight > rect.height) return -1;
    const size_t legendRows =
        std::min(slices.size(), static_cast<size_t>((rect.height - top - BAR_HEIGHT - GAP) / legendLine));

    double sum = 0;
    for (const auto& slice : slices) sum += slice.amount;
    const std::string total = dashboard::formatAmount(sum, slices.front().unit).text();
    drawHeading(rect, section, total.c_str());

    // The bar: slices side by side, a white seam between them, one outline around.
    const fui::Rect bar = sub(rect, 0, top, rect.width, BAR_HEIGHT);
    // Edges come from the running sum so rounding never adds up past the bar.
    const int inner = bar.width - 2;
    double running = 0;
    int x = 0;
    for (size_t i = 0; i < slices.size(); ++i) {
      running += slices[i].amount;
      const int edge = static_cast<int>(running / sum * inner + 0.5);
      const int width = edge - x;
      if (width > 0) {
        target.fill(sub(bar, 1 + x, 1, width, bar.height - 2), patternPaint(slices[i], i));
        if (width > 1 && i + 1 < slices.size()) {
          target.fill(sub(bar, x + width, 1, 1, bar.height - 2), fui::Paint::solid(fui::Color::White));
        }
      }
      x = edge;
    }
    target.stroke(bar, fui::Paint::solid(fui::Color::Black), 1);

    // Legend in the same order.
    fui::TextStyle value = small;
    value.align = fui::TextAlign::Right;
    const int legendTop = top + BAR_HEIGHT + GAP;
    for (size_t i = 0; i < legendRows; ++i) {
      const fui::Rect line = sub(rect, 0, legendTop + static_cast<int>(i) * legendLine, rect.width, legendLine);
      const fui::Rect swatch = sub(line, 0, (legendLine - SWATCH) / 2, SWATCH, SWATCH);
      target.fill(swatch, patternPaint(slices[i], i));
      target.stroke(swatch, fui::Paint::solid(fui::Color::Black), 1);
      const char* label = slices[i].row ? slices[i].row->label.c_str() : tr(STR_DASH_OTHER);
      const std::string amount = slices[i].reading.text();
      const int textX = SWATCH + GAP;
      const fui::Rect text = sub(line, textX, (legendLine - smallLine) / 2, line.width - textX, smallLine);
      target.text(text, amount.c_str(), value);
      const int valueWidth = target.measureText(small.font, amount.c_str(), small).width;
      target.text(sub(text, 0, 0, text.width - valueWidth - GAP, smallLine), label, small);
    }
    return legendTop + static_cast<int>(legendRows) * legendLine;
  }

  int rows(const fui::Rect rect, const dashboard::Section& section) {
    const auto shown = shownRows(section);
    if (shown.empty()) return 0;
    const int top = headingHeight(section);
    const int line = bodyLine + 8;
    const int height = top + static_cast<int>(shown.size()) * line;
    if (height > rect.height) return -1;

    drawHeading(rect, section);
    fui::TextStyle value = body;
    value.align = fui::TextAlign::Right;
    for (size_t i = 0; i < shown.size(); ++i) {
      const fui::Rect r = sub(rect, 0, top + static_cast<int>(i) * line + 4, rect.width, bodyLine);
      const std::string text = dashboard::rowText(*shown[i], cache);
      target.text(r, text.c_str(), value);
      const int valueWidth = target.measureText(body.font, text.c_str(), body).width;
      target.text(sub(r, 0, 0, r.width - valueWidth - GAP, bodyLine), shown[i]->label.c_str(), body);
    }
    return height;
  }

  UiAppHost::UiScreen& screen;
  fui::DrawTarget& target;
  const dashboard::Cache& cache;
  fui::TextStyle small, body, title, heading;
  int smallLine = 0, bodyLine = 0, titleLine = 0;
};

}  // namespace

void drawTab(UiAppHost::UiScreen& screen, const fui::Rect rect, const dashboard::Tab& tab,
             const dashboard::Cache& cache) {
  Painter painter(screen, cache);
  int y = 0;
  for (const auto& section : tab.sections) {
    const int used = painter.section(sub(rect, 0, y, rect.width, rect.height - y), section);
    if (used < 0) break;
    if (used > 0) y += used + 2 * GAP;
  }
}

}  // namespace dashboard_tiles
