// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

class PanelTransition {
public:
 enum class Style { Instant, Fade, Flash, Slide, Blur, Fall };
 struct Frame {
  QVector<QRectF> cards;
  qreal opacity=1, blur=0, flash=0;
 };

 static Style parse(const QString &name);
 void begin(Style style, bool showing, const QVector<QRectF> &layout,
            const QRectF &screen, bool right, const Frame *previous=nullptr);
 Frame sample(int elapsedMs) const;
 int duration() const { return duration_; }
 QRectF envelope() const { return envelope_; }

private:
 Style style_=Style::Instant;
 bool showing_=false;
 int duration_=0;
 Frame start_, end_;
 QRectF envelope_;
 QVector<QVector<QRectF>> tumble_;
};
