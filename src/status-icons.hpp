// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QIcon>
#include <QColor>
#include <QSet>
#include <QStringList>
#include <QVector>

// A text-free status strip; the owner paints the card surface and hides it
// whenever iconCount() is zero. Output icons only represent active outputs.
class StatusIconsCard final : public QWidget {
 friend class QuickPanelBridge;
public:
 struct AlertIcon {QString id;QIcon icon;QColor color;QString label;};
 explicit StatusIconsCard(QWidget *parent=nullptr);
 void setState(bool recording,bool streaming,const QVector<AlertIcon> &alerts);
 void setPersistentIcons(const QVector<AlertIcon> &icons);
 void setIcons(QIcon recording,QIcon streaming);
 void setScale(double scale);
 void setPulse(double pulse);
 int iconCount() const {return roles().size();}
 bool hasAlerts() const {return !alerts_.isEmpty();}
 QStringList roles() const;
 QRectF cardRect() const {return QRectF(rect());}
 QSize sizeHint() const override;
 int heightForWidth(int width) const override;
protected:
 void paintEvent(QPaintEvent *event) override;
private:
 struct Glyph {QIcon icon;QColor color;QString alertId;};
 QVector<Glyph> glyphs() const;
 int columnsFor(int width) const;
 bool recording_=false,streaming_=false;
 QIcon recordingIcon_,streamingIcon_;
 QVector<AlertIcon> alerts_;
 QVector<AlertIcon> persistent_;
 QSet<QString> freshAlerts_;
 double scale_=1,pulse_=1;
};
