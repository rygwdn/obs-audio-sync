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

#pragma once

#include <QWidget>
#include <QVector>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include "audio-analyzer.h"

class TimelineWidget : public QWidget {
	Q_OBJECT

public:
	explicit TimelineWidget(QWidget *parent = nullptr);
	~TimelineWidget();

	void setAudioSamples(const QVector<AudioSample> &samples);
	void setSpikePosition(double timestamp);
	void setFPS(double fps);

signals:
	void spikePositionChanged(double timestamp);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	double timestampFromX(int x) const;
	int xFromTimestamp(double timestamp) const;
	void drawWaveform(QPainter &painter);
	void drawFrameMarkers(QPainter &painter);
	void drawTimeMarkers(QPainter &painter);
	void drawSpikeMarker(QPainter &painter);

	QVector<AudioSample> m_samples;
	double m_spikePosition;
	double m_startTime;
	double m_endTime;
	double m_fps;
	int m_spikeDragStartX;
	bool m_draggingSpike;
};
