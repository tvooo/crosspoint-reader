#pragma once

#include "components/UiAppHost.h"
#include "dashboard/Dashboard.h"

// Read-only drawing of a dashboard tab: cards, breakdown bars and plain rows.
// Toggle rows are skipped; the activity lists them as selectable rows.
namespace dashboard_tiles {

// Draws the tab's sections top-down into `rect`, stopping at the first that doesn't fit.
void drawTab(UiAppHost::UiScreen& screen, freeink::ui::Rect rect, const dashboard::Tab& tab,
             const dashboard::Cache& cache);

}  // namespace dashboard_tiles
