// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QIcon>
#include <array>
#include <deque>
#include <optional>

// Rates passed to setOutputs are bits per second, not bytes per second.
// The owning notification window paints the shared rounded card surface.
class BitrateCard final : public QWidget {
 friend class QuickPanelBridge;
public:
 struct Sample {qint64 timeMs;std::array<double,2> bps;std::array<quint64,2> generation;};
 explicit BitrateCard(QWidget *parent=nullptr);
 void setOutputs(bool recording,bool streaming,std::optional<double> recordingBps,std::optional<double> streamingBps,qint64 timeMs);
 void setIcons(QIcon recording,QIcon streaming);
 void setAppearance(int design);
 void setCompact(bool compact);
 void setGraphsEnabled(bool enabled){if(graphs_!=enabled){graphs_=enabled;updateGeometry();update();}}
 bool compact() const {return compact_;}
 void setWindowSeconds(int seconds);
 void clearHistory();
 QRectF cardRect() const {return QRectF(rect());}
 QSize sizeHint() const override {return {360,heightForWidth(360)};}
 int heightForWidth(int width) const override;
 bool hasActiveOutputs() const {return active_[0] || active_[1];}
 size_t sampleCount() const {return history_.size();}
 const std::deque<Sample> &samples() const {return history_;}
 double rangeMaximumBps() const;
protected:
 void paintEvent(QPaintEvent *) override;
private:
 double uiScale() const;
 QFont drawingFont() const;
 double logicalHeight(double width) const;
 static double niceMaximum(double value);
 std::deque<Sample> history_;
 std::array<bool,2> active_={false,false};
 std::array<double,2> current_;
 std::array<quint64,2> generation_={0,0};
 std::array<QIcon,2> icons_;
 int appearance_=1,seconds_=60;
 bool compact_=false;
 bool graphs_=true;
 qint64 now_=0;
};
