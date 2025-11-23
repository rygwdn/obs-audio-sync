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
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QDebug>
#include <algorithm>
#include <QtMath>

TimelineWidget::TimelineWidget(QWidget *parent)
	: QWidget(parent),
	  m_spikePosition(0.0),
	  m_startTime(0.0),
	  m_endTime(4.0),
	  m_fps(30.0),
	  m_spikeDragStartX(0),
	  m_draggingSpike(false)
{
	setMinimumHeight(120);
	setMouseTracking(true);
}

TimelineWidget::~TimelineWidget() {}

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

double TimelineWidget::timestampFromX(int x) const
{
	int width = this->width() - 40; // Leave margins
	if (width <= 0) {
		return m_startTime;
	}

	double ratio = (double)(x - 20) / width;
	return m_startTime + ratio * (m_endTime - m_startTime);
}

int TimelineWidget::xFromTimestamp(double timestamp) const
{
	int width = this->width() - 40;
	if (width <= 0 || m_endTime <= m_startTime) {
		return 20;
	}

	double ratio = (timestamp - m_startTime) / (m_endTime - m_startTime);
	return 20 + (int)(ratio * width);
}

void TimelineWidget::drawWaveform(QPainter &painter)
{
	if (m_samples.isEmpty()) {
		return;
	}

	int width = this->width() - 40;
	int height = 60;
	int startX = 20;
	int centerY = 40;

	painter.setPen(QPen(QColor(100, 150, 255), 1));
	painter.setBrush(QBrush(QColor(100, 150, 255, 100)));

	QPolygonF waveform;
	waveform << QPointF(startX, centerY);

	// Draw waveform
	double maxAmplitude = 0.0;
	for (const AudioSample &sample : m_samples) {
		maxAmplitude = qMax(maxAmplitude, sample.amplitude);
	}

	if (maxAmplitude > 0.0) {
		for (const AudioSample &sample : m_samples) {
			int x = xFromTimestamp(sample.timestamp);
			double normalized = sample.amplitude / maxAmplitude;
			int y = centerY - (int)(normalized * height / 2);
			waveform << QPointF(x, y);
		}
	}

	// Mirror for bottom half
	for (int i = m_samples.size() - 1; i >= 0; i--) {
		const AudioSample &sample = m_samples[i];
		int x = xFromTimestamp(sample.timestamp);
		double normalized = sample.amplitude / maxAmplitude;
		int y = centerY + (int)(normalized * height / 2);
		waveform << QPointF(x, y);
	}

	waveform << QPointF(startX, centerY);
	painter.drawPolygon(waveform);
}

void TimelineWidget::drawFrameMarkers(QPainter &painter)
{
	if (m_fps <= 0.0) {
		return;
	}

	double frameDuration = 1.0 / m_fps;
	int width = this->width() - 40;
	int startX = 20;

	painter.setPen(QPen(QColor(150, 150, 150), 1));

	double currentTime = m_startTime;
	int frameNumber = 0;

	while (currentTime <= m_endTime) {
		int x = xFromTimestamp(currentTime);
		painter.drawLine(x, 100, x, 105);

		// Draw frame number every 10 frames or at start/end
		if (frameNumber % 10 == 0 || currentTime == m_startTime || currentTime >= m_endTime - frameDuration) {
			QString frameText = QString::number(frameNumber);
			QRect textRect(x - 20, 107, 40, 15);
			painter.drawText(textRect, Qt::AlignCenter, frameText);
		}

		currentTime += frameDuration;
		frameNumber++;
	}
}

void TimelineWidget::drawTimeMarkers(QPainter &painter)
{
	int width = this->width() - 40;
	int startX = 20;

	painter.setPen(QPen(QColor(100, 100, 100), 1));

	// Draw time markers every 0.5 seconds
	for (double t = m_startTime; t <= m_endTime; t += 0.5) {
		int x = xFromTimestamp(t);
		painter.drawLine(x, 0, x, 15);

		QString timeText = QString::number(t, 'f', 1) + "s";
		QRect textRect(x - 25, 0, 50, 15);
		painter.drawText(textRect, Qt::AlignCenter, timeText);
	}
}

void TimelineWidget::drawSpikeMarker(QPainter &painter)
{
	int x = xFromTimestamp(m_spikePosition);
	int height = this->height();

	// Draw vertical line
	painter.setPen(QPen(QColor(255, 0, 0), 2));
	painter.drawLine(x, 0, x, height);

	// Draw spike indicator
	painter.setBrush(QBrush(QColor(255, 0, 0)));
	QPolygonF triangle;
	triangle << QPointF(x, 0) << QPointF(x - 8, 15) << QPointF(x + 8, 15);
	painter.drawPolygon(triangle);

	// Draw timestamp label
	QString spikeText = QString::number(m_spikePosition, 'f', 3) + "s";
	QRect textRect(x - 40, 18, 80, 15);
	painter.setPen(QPen(QColor(255, 255, 255)));
	painter.setBrush(QBrush(QColor(255, 0, 0)));
	painter.drawRect(textRect);
	painter.drawText(textRect, Qt::AlignCenter, spikeText);
}

void TimelineWidget::paintEvent(QPaintEvent *event)
{
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
		int spikeX = xFromTimestamp(m_spikePosition);
		// Check if clicking near spike marker (within 10 pixels)
		if (qAbs(event->x() - spikeX) < 10) {
			m_draggingSpike = true;
			m_spikeDragStartX = event->x();
		} else {
			// Click to set spike position
			double newTimestamp = timestampFromX(event->x());
			newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
			setSpikePosition(newTimestamp);
			emit spikePositionChanged(newTimestamp);
		}
	}
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (m_draggingSpike && (event->buttons() & Qt::LeftButton)) {
		double newTimestamp = timestampFromX(event->x());
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
