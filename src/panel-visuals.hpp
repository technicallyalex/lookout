// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QColor>
#include <QPainter>
#include <QWidget>
namespace SentinelVisuals {
constexpr int PanelRadius=8;
constexpr double BorderWidth=1.0;
constexpr int PreviewRadius=8;
QPalette panelPalette(const QPalette &native,int design,bool dark);
void paintCardSurface(QPainter &painter,const QRectF &rect,const QPalette &palette,int design,double opacity);
void roundWidget(QWidget *widget,int radius);
void paintGlow(QPainter &painter,const QRectF &panel,const QColor &color,double pulse,double intensity,double spread);
}
