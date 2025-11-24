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

#include "timeline-widget.h"
#include "audio-analyzer.h"
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QDebug>
#include <algorithm>
#include <QtMath>
#include <qwidget.h>
#include <qpolygon.h>
#include <qpaintdevice.h>
#include <qnamespace.h>
#include <qtpreprocessorsupport.h>
#include <qtmetamacros.h>

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(120);
	setMouseTracking(true);
}

TimelineWidget::~TimelineWidget() = default;

void TimelineWidget::setAudioSamples(const QVector<AudioSample> &samples)
{
	m_samples = samples;
	if (!samples.isEmpty()) {
		m_startTime = samples.first().timestamp;
		m_endTime = samples.last().timestamp;
	}
	update();
}

void TimelineWidget::setSpikePosition(double timestamp)
{
	m_spikePosition = timestamp;
	update();
}

void TimelineWidget::setFPS(double fps)
{
	m_fps = fps;
	update();
}

double TimelineWidget::timestampFromX(int xPos) const
{
	int width = this->width() - 40; // Leave margins
	if (width <= 0) {
		return m_startTime;
	}

	double const RATIO = (double)(xPos - 20) / width;
	return m_startTime + (RATIO * (m_endTime - m_startTime));
}

int TimelineWidget::xFromTimestamp(double timestamp) const
{
	int width = this->width() - 40;
	if (width <= 0 || m_endTime <= m_startTime) {
		return 20;
	}

	double const RATIO = (timestamp - m_startTime) / (m_endTime - m_startTime);
	return 20 + (int)(RATIO * width);
}

void TimelineWidget::drawWaveform(QPainter &painter)
{
	if (m_samples.isEmpty()) {
		return;
	}

	int const HEIGHT = 60;
	int const START_X = 20;
	int const CENTER_Y = 40;

	painter.setPen(QPen(QColor(100, 150, 255), 1));
	painter.setBrush(QBrush(QColor(100, 150, 255, 100)));

	QPolygonF waveform;
	waveform << QPointF(START_X, CENTER_Y);

	// Draw waveform
	double maxAmplitude = 0.0;
	for (const AudioSample &sample : m_samples) {
		maxAmplitude = qMax(maxAmplitude, sample.amplitude);
	}

	if (maxAmplitude > 0.0) {
		for (const AudioSample &sample : m_samples) {
			int x = xFromTimestamp(sample.timestamp);
			double normalized = sample.amplitude / maxAmplitude;
			int y = CENTER_Y - (int)(normalized * HEIGHT / 2);
			waveform << QPointF(x, y);
		}
	}

	// Mirror for bottom half
	for (int i = m_samples.size() - 1; i >= 0; i--) { // NOLINT(cppcoreguidelines-init-variables)
		const AudioSample &sample = m_samples[i];
		int const X_POS = xFromTimestamp(sample.timestamp);
		double const NORMALIZED = sample.amplitude / maxAmplitude;
		int const Y_POS = CENTER_Y + (int)(NORMALIZED * HEIGHT / 2);
		waveform << QPointF(X_POS, Y_POS);
	}

	waveform << QPointF(START_X, CENTER_Y);
	painter.drawPolygon(waveform);
}

void TimelineWidget::drawFrameMarkers(QPainter &painter)
{
	if (m_fps <= 0.0) {
		return;
	}

	double const FRAME_DURATION = 1.0 / m_fps;

	painter.setPen(QPen(QColor(150, 150, 150), 1));

	double currentTime = m_startTime;
	int frameNumber = 0;

	while (currentTime <= m_endTime) {
		int const X_POS = xFromTimestamp(currentTime);
		painter.drawLine(X_POS, 100, X_POS, 105);

		// Draw frame number every 10 frames or at start/end
		if (frameNumber % 10 == 0 || currentTime == m_startTime || currentTime >= m_endTime - FRAME_DURATION) {
			QString const FRAME_TEXT = QString::number(frameNumber);
			QRect const TEXT_RECT(X_POS - 20, 107, 40, 15);
			painter.drawText(TEXT_RECT, Qt::AlignCenter, FRAME_TEXT);
		}

		currentTime += FRAME_DURATION;
		frameNumber++;
	}
}

void TimelineWidget::drawTimeMarkers(QPainter &painter)
{
	painter.setPen(QPen(QColor(100, 100, 100), 1));

	// Draw time markers every 0.5 seconds
	const double MARKER_INTERVAL = 0.5;
	const int NUM_MARKERS = static_cast<int>((m_endTime - m_startTime) / MARKER_INTERVAL) + 1;
	for (int markerIndex = 0; markerIndex < NUM_MARKERS; markerIndex++) {
		double const TIME_VALUE = m_startTime + (markerIndex * MARKER_INTERVAL);
		if (TIME_VALUE > m_endTime) {
			break;
		}
		int const X_POS = xFromTimestamp(TIME_VALUE);
		painter.drawLine(X_POS, 0, X_POS, 15);

		QString const TIME_TEXT = QString::number(TIME_VALUE, 'f', 1) + "s";
		QRect const TEXT_RECT(X_POS - 25, 0, 50, 15);
		painter.drawText(TEXT_RECT, Qt::AlignCenter, TIME_TEXT);
	}
}

void TimelineWidget::drawSpikeMarker(QPainter &painter)
{
	int const X_POS = xFromTimestamp(m_spikePosition);
	int height = this->height();

	// Draw vertical line
	painter.setPen(QPen(QColor(255, 0, 0), 2));
	painter.drawLine(X_POS, 0, X_POS, height);

	// Draw spike indicator
	painter.setBrush(QBrush(QColor(255, 0, 0)));
	QPolygonF triangle;
	triangle << QPointF(X_POS, 0) << QPointF(X_POS - 8, 15) << QPointF(X_POS + 8, 15);
	painter.drawPolygon(triangle);

	// Draw timestamp label
	QString const SPIKE_TEXT = QString::number(m_spikePosition, 'f', 3) + "s";
	QRect const TEXT_RECT(X_POS - 40, 18, 80, 15);
	painter.setPen(QPen(QColor(255, 255, 255)));
	painter.setBrush(QBrush(QColor(255, 0, 0)));
	painter.drawRect(TEXT_RECT);
	painter.drawText(TEXT_RECT, Qt::AlignCenter, SPIKE_TEXT);
}

void TimelineWidget::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Background
	painter.fillRect(rect(), QColor(40, 40, 40));

	// Draw components
	drawTimeMarkers(painter);
	drawWaveform(painter);
	drawFrameMarkers(painter);
	drawSpikeMarker(painter);
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		int const SPIKE_X = xFromTimestamp(m_spikePosition);
		int mouseX = static_cast<int>(event->position().x());
		// Check if clicking near spike marker (within 10 pixels)
		if (qAbs(mouseX - SPIKE_X) < 10) {
			m_draggingSpike = true;
			m_spikeDragStartX = mouseX;
		} else {
			// Click to set spike position
			double newTimestamp = timestampFromX(mouseX);
			newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
			setSpikePosition(newTimestamp);
			emit spikePositionChanged(newTimestamp);
		}
	}
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (m_draggingSpike && (event->buttons() & Qt::LeftButton)) {
		int mouseX = static_cast<int>(event->position().x());
		double newTimestamp = timestampFromX(mouseX);
		newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
		setSpikePosition(newTimestamp);
		emit spikePositionChanged(newTimestamp);
	} else {
		m_draggingSpike = false;
	}
}

void TimelineWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	update();
}
