/*
OBS Audio Sync Plugin
Copyright (C) 2025 Ryan Wooden

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#ifndef TIMELINE_WIDGET_H
#define TIMELINE_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <qtmetamacros.h>
#include <qicon.h>
#include "audio-analyzer.h" // For AudioSample definition

class TimelineWidget : public QWidget {
	Q_OBJECT

public:
	explicit TimelineWidget(QWidget *parent = nullptr);
	~TimelineWidget() override;

	// Delete copy and move constructors/assignments
	TimelineWidget(const TimelineWidget &) = delete;
	TimelineWidget &operator=(const TimelineWidget &) = delete;
	TimelineWidget(TimelineWidget &&) = delete;
	TimelineWidget &operator=(TimelineWidget &&) = delete;

	void setAudioSamples(const QVector<AudioSample> &samples);
	void setSpikePosition(double timestamp);
	void setFPS(double fps);
	void setVideoFramePosition(double timestamp);
	void setVideoFrames(const QVector<double> &frameTimestamps);
	void setFrameDifferences(const QVector<double> &differences);
	void zoomIn();
	void zoomOut();
	void resetZoom();

signals:
	void spikePositionChanged(double timestamp);
	void videoFramePositionChanged(double timestamp);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	[[nodiscard]] double timestampFromX(int xPos) const;
	[[nodiscard]] int xFromTimestamp(double timestamp) const;
	void drawWaveform(QPainter &painter);
	void drawFrameMarkers(QPainter &painter);
	void drawTimeMarkers(QPainter &painter);
	void drawSpikeMarker(QPainter &painter);
	void drawVideoFrameMarkers(QPainter &painter);
	void drawVideoFramePosition(QPainter &painter);
	void drawFrameDifferenceBars(QPainter &painter);
	[[nodiscard]] double snapToFrame(double timestamp) const;
	void wheelEvent(QWheelEvent *event) override;

	QVector<AudioSample> m_samples{};
	double m_spikePosition{};
	double m_startTime{};
	double m_endTime{};
	double m_viewStartTime{}; // Visible time range start (for zoom)
	double m_viewEndTime{};   // Visible time range end (for zoom)
	double m_fps{};
	double m_videoFramePosition{-1.0};
	QVector<double> m_videoFrameTimestamps{};
	QVector<double> m_frameDifferences{};
	int m_spikeDragStartX{};
	bool m_draggingSpike{};
	bool m_draggingVideoFrame{};
	double m_zoomLevel{1.0}; // 1.0 = no zoom, >1.0 = zoomed in
};

#endif // TIMELINE_WIDGET_H
