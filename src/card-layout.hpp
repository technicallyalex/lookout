// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

namespace SentinelLayout {
// Rectangles use Qt's inclusive right/bottom coordinates. Spacing is the number
// of clear pixels between cards. Screen insets apply when screen snapping is on;
// cards are kept accessible on the screen even when snapping is off.
QPoint snapPosition(QRect moving, const QVector<QRect> &others, QRect screen,
                    int spacing, int snapDistance, bool snapCards, bool snapScreen,
                    QMargins screenInsets = QMargins(24,24,24,24));
// 0 Small, 1 Normal, 2 Large, 3 Extra large. Qt logical screen dimensions avoid
// applying Windows DPI scaling twice. Final scale is limited to 0.65–2.5.
double presetScale(int preset, QSize availableLogicalScreen);
int presetWidth(int preset, QSize availableLogicalScreen);

struct GroupedWidths {
 int window;
 int metrics;
 int thumbnail;
 bool alongside;
};
// Widths are logical pixels. Metrics keep the same column width when the
// thumbnail moves beside them. The window width includes its outer padding.
GroupedWidths groupedWidths(int screenWidth, int panelWidth, int thumbnailWidth,
                           int spacing, bool sideRequested, bool showMetrics, bool showThumbnail);
}
