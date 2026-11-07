// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stat-labels.hpp"
#include <QWidget>
#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPointer>
#include <deque>
#include <array>
class HistoryGraphs final : public QWidget {
 friend class QuickPanelBridge;
public:
 static constexpr int Count=6;
 struct Metric {const char *id,*card,*label,*unit;};
 static constexpr Metric metrics[Count] = {
  {"storage","disk",LookoutStats::storage.compact,"GiB"}, {"encoder","encoder",LookoutStats::encoder.compact,"%"},
  {"network","network",LookoutStats::network.compact,"%"}, {"cpu","cpu",LookoutStats::cpu.compact,"%"},
  {"memory","memory",LookoutStats::memory.compact,"MiB"}, {"disk-time","disk-time",LookoutStats::diskTime.compact,"min"}
 };
 struct Sample {qint64 timeMs;std::array<double,Count> values;};
 explicit HistoryGraphs(QWidget *parent=nullptr);
 void append(Sample sample);
 void clearHistory();
 void configure(int seconds,const std::array<bool,Count> &selected,const std::array<double,Count> &thresholds);
 void setAppearance(int preset);
 void setCompactMetrics(const std::array<bool,Count> &compact);
 bool compact(int metric=0) const {return compact_[metric];}
 void setMetricIcons(const std::array<QIcon,Count> &icons);
 void setHistorySource(const HistoryGraphs *source);
 QVector<QRectF> cardRects() const;
 QRectF metricRect(int metric) const;
 int appearance() const {return appearance_;}
 static QPainterPath curve(const QVector<QPointF> &points,bool smooth);
 size_t sampleCount() const {return samples().size();}
 const std::deque<Sample> &samples() const {return source_?source_->samples():history_;}
 QSize sizeHint() const override {return {360,heightForWidth(360)};}
 int heightForWidth(int width) const override;
protected:
 void paintEvent(QPaintEvent *) override;
private:
 double uiScale() const;
 QFont drawingFont() const;
 int columns(int width) const;
 double cardHeight(double logicalWidth,bool compact) const;
 QVector<QRectF> rectangles(int width) const;
 QPointer<const HistoryGraphs> source_;
 QMetaObject::Connection sourceConnection_;
 std::deque<Sample> history_;
 int seconds_=60;
 int appearance_=1;
 std::array<bool,Count> compact_{};
 std::array<QIcon,Count> metricIcons_;
 std::array<bool,Count> selected_={true,true,true};
 std::array<double,Count> thresholds_={20,2,2,-1,-1,-1};
};
