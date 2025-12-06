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
#include <QWheelEvent>
#include <QDebug>
#include <algorithm>
#include <QtMath>
#include <qwidget.h>
#include <qpolygon.h>
#include <qpaintdevice.h>
#include <qnamespace.h>
#include <qtmetamacros.h>

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(120);
	setMouseTracking(true);
	m_viewStartTime = m_startTime;
	m_viewEndTime = m_endTime;
}

TimelineWidget::~TimelineWidget() = default;

void TimelineWidget::setAudioSamples(const QVector<AudioSample> &samples)
{
	m_samples = samples;
	if (!samples.isEmpty()) {
		m_startTime = samples.first().timestamp;
		m_endTime = samples.last().timestamp;
		m_viewStartTime = m_startTime;
		m_viewEndTime = m_endTime;
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

void TimelineWidget::setVideoFramePosition(double timestamp)
{
	m_videoFramePosition = timestamp;
	update();
}

void TimelineWidget::setVideoFrames(const QVector<double> &frameTimestamps)
{
	m_videoFrameTimestamps = frameTimestamps;
	update();
}

void TimelineWidget::setFrameDifferences(const QVector<double> &differences)
{
	m_frameDifferences = differences;
	update();
}

void TimelineWidget::zoomIn()
{
	if (m_viewEndTime <= m_viewStartTime) {
		return;
	}
	double const CENTER = (m_viewStartTime + m_viewEndTime) / 2.0;
	double const RANGE = m_viewEndTime - m_viewStartTime;
	double const NEW_RANGE = RANGE * 0.8; // Zoom in by 20%
	m_viewStartTime = CENTER - NEW_RANGE / 2.0;
	m_viewEndTime = CENTER + NEW_RANGE / 2.0;
	// Clamp to full range
	m_viewStartTime = qMax(m_startTime, m_viewStartTime);
	m_viewEndTime = qMin(m_endTime, m_viewEndTime);
	m_zoomLevel = (m_endTime - m_startTime) / (m_viewEndTime - m_viewStartTime);
	update();
}

void TimelineWidget::zoomOut()
{
	if (m_viewEndTime <= m_viewStartTime) {
		return;
	}
	double const CENTER = (m_viewStartTime + m_viewEndTime) / 2.0;
	double const RANGE = m_viewEndTime - m_viewStartTime;
	double const NEW_RANGE = RANGE * 1.25; // Zoom out by 25%
	m_viewStartTime = CENTER - NEW_RANGE / 2.0;
	m_viewEndTime = CENTER + NEW_RANGE / 2.0;
	// Clamp to full range
	m_viewStartTime = qMax(m_startTime, m_viewStartTime);
	m_viewEndTime = qMin(m_endTime, m_viewEndTime);
	m_zoomLevel = (m_endTime - m_startTime) / (m_viewEndTime - m_viewStartTime);
	update();
}

void TimelineWidget::resetZoom()
{
	m_viewStartTime = m_startTime;
	m_viewEndTime = m_endTime;
	m_zoomLevel = 1.0;
	update();
}

double TimelineWidget::timestampFromX(int xPos) const
{
	int width = this->width() - 40; // Leave margins
	if (width <= 0) {
		return m_viewStartTime;
	}

	double const RATIO = (double)(xPos - 20) / width;
	return m_viewStartTime + (RATIO * (m_viewEndTime - m_viewStartTime));
}

int TimelineWidget::xFromTimestamp(double timestamp) const
{
	int width = this->width() - 40;
	if (width <= 0 || m_viewEndTime <= m_viewStartTime) {
		return 20;
	}

	double const RATIO = (timestamp - m_viewStartTime) / (m_viewEndTime - m_viewStartTime);
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

	// Start from view start time, but calculate frame number from overall start
	double currentTime = m_viewStartTime;
	int frameNumber = static_cast<int>((m_viewStartTime - m_startTime) * m_fps);

	while (currentTime <= m_viewEndTime) {
		int const X_POS = xFromTimestamp(currentTime);
		if (X_POS < 20 || X_POS > width() - 20) {
			currentTime += FRAME_DURATION;
			frameNumber++;
			continue; // Skip markers outside visible area
		}
		painter.drawLine(X_POS, 100, X_POS, 105);

		// Draw frame number every 10 frames or at start/end
		if (frameNumber % 10 == 0 || currentTime <= m_viewStartTime + FRAME_DURATION ||
		    currentTime >= m_viewEndTime - FRAME_DURATION) {
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

	// Draw time markers every 0.5 seconds (adjust based on zoom)
	double const VISIBLE_RANGE = m_viewEndTime - m_viewStartTime;
	double MARKER_INTERVAL = 0.5;
	if (VISIBLE_RANGE < 2.0) {
		MARKER_INTERVAL = 0.1; // More frequent markers when zoomed in
	} else if (VISIBLE_RANGE < 5.0) {
		MARKER_INTERVAL = 0.25;
	}

	const int NUM_MARKERS = static_cast<int>((m_viewEndTime - m_viewStartTime) / MARKER_INTERVAL) + 1;
	for (int markerIndex = 0; markerIndex < NUM_MARKERS; markerIndex++) {
		double const TIME_VALUE = m_viewStartTime + (markerIndex * MARKER_INTERVAL);
		if (TIME_VALUE > m_viewEndTime) {
			break;
		}
		int const X_POS = xFromTimestamp(TIME_VALUE);
		if (X_POS < 20 || X_POS > width() - 20) {
			continue; // Skip markers outside visible area
		}
		painter.drawLine(X_POS, 0, X_POS, 15);

		QString const TIME_TEXT = QString::number(TIME_VALUE, 'f', 2) + "s";
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

void TimelineWidget::drawVideoFrameMarkers(QPainter &painter)
{
	if (m_videoFrameTimestamps.isEmpty()) {
		return;
	}

	painter.setPen(QPen(QColor(100, 255, 100), 1));

	// Draw markers for all video frames in visible range
	for (double frameTime : m_videoFrameTimestamps) {
		if (frameTime < m_viewStartTime || frameTime > m_viewEndTime) {
			continue;
		}
		int const X_POS = xFromTimestamp(frameTime);
		if (X_POS < 20 || X_POS > width() - 20) {
			continue;
		}
		// Draw small vertical line
		painter.drawLine(X_POS, 60, X_POS, 65);
	}
}

void TimelineWidget::drawVideoFramePosition(QPainter &painter)
{
	if (m_videoFramePosition < 0.0) {
		return;
	}

	if (m_videoFramePosition < m_viewStartTime || m_videoFramePosition > m_viewEndTime) {
		return;
	}

	int const X_POS = xFromTimestamp(m_videoFramePosition);
	int height = this->height();

	// Draw vertical line for current video frame
	painter.setPen(QPen(QColor(0, 255, 0), 2));
	painter.drawLine(X_POS, 0, X_POS, height);

	// Draw frame indicator
	painter.setBrush(QBrush(QColor(0, 255, 0)));
	QPolygonF triangle;
	triangle << QPointF(X_POS, height) << QPointF(X_POS - 8, height - 15) << QPointF(X_POS + 8, height - 15);
	painter.drawPolygon(triangle);

	// Draw timestamp label
	QString const FRAME_TEXT = QString::number(m_videoFramePosition, 'f', 3) + "s";
	QRect const TEXT_RECT(X_POS - 40, height - 33, 80, 15);
	painter.setPen(QPen(QColor(255, 255, 255)));
	painter.setBrush(QBrush(QColor(0, 255, 0)));
	painter.drawRect(TEXT_RECT);
	painter.drawText(TEXT_RECT, Qt::AlignCenter, FRAME_TEXT);
}

void TimelineWidget::drawOffsetLine(QPainter &painter)
{
	if (m_videoFramePosition < 0.0 || m_spikePosition < 0.0) {
		return;
	}

	// Draw a line connecting audio spike to video frame position
	int const SPIKE_X = xFromTimestamp(m_spikePosition);
	int const FRAME_X = xFromTimestamp(m_videoFramePosition);

	// Only draw if both are visible
	if ((SPIKE_X < 20 || SPIKE_X > width() - 20) && (FRAME_X < 20 || FRAME_X > width() - 20)) {
		return;
	}

	double const OFFSET = m_videoFramePosition - m_spikePosition;
	QColor lineColor;
	if (qAbs(OFFSET) < 0.033) {            // Less than 1 frame at 30fps
		lineColor = QColor(0, 255, 0); // Green - in sync
	} else if (qAbs(OFFSET) < 0.1) {
		lineColor = QColor(255, 255, 0); // Yellow - close
	} else {
		lineColor = QColor(255, 0, 0); // Red - out of sync
	}

	painter.setPen(QPen(lineColor, 2, Qt::DashLine));
	painter.drawLine(SPIKE_X, 30, FRAME_X, height() - 30);

	// Draw offset text at midpoint
	int const MID_X = (SPIKE_X + FRAME_X) / 2;
	int const MID_Y = (30 + (height() - 30)) / 2;
	QString const OFFSET_TEXT = QString("%1ms").arg(OFFSET * 1000.0, 0, 'f', 1);
	QRect const TEXT_RECT(MID_X - 40, MID_Y - 10, 80, 20);
	painter.setPen(QPen(QColor(255, 255, 255)));
	painter.setBrush(QBrush(lineColor));
	painter.drawRect(TEXT_RECT);
	painter.drawText(TEXT_RECT, Qt::AlignCenter, OFFSET_TEXT);
}

void TimelineWidget::drawFrameDifferenceBars(QPainter &painter)
{
	if (m_frameDifferences.isEmpty() || m_videoFrameTimestamps.isEmpty()) {
		return;
	}

	// Ensure we have differences for all frames (or at least matching count)
	if (m_frameDifferences.size() != m_videoFrameTimestamps.size()) {
		return;
	}

	// Find max difference for normalization (only consider visible range)
	double maxDiff = 0.0;
	for (int i = 0; i < m_videoFrameTimestamps.size(); i++) {
		double frameTime = m_videoFrameTimestamps[i];
		if (frameTime >= m_viewStartTime && frameTime <= m_viewEndTime) {
			maxDiff = qMax(maxDiff, m_frameDifferences[i]);
		}
	}

	if (maxDiff <= 0.0) {
		return; // No differences to show
	}

	// Draw bars above frame markers (y=50-60, bars extend upward)
	const int BAR_BASE_Y = 50;
	const int MAX_BAR_HEIGHT = 20;
	const int BAR_WIDTH = 4;

	painter.setPen(Qt::NoPen);

	for (int i = 0; i < m_videoFrameTimestamps.size(); i++) {
		double frameTime = m_videoFrameTimestamps[i];
		if (frameTime < m_viewStartTime || frameTime > m_viewEndTime) {
			continue;
		}

		int xPos = xFromTimestamp(frameTime);
		if (xPos < 20 || xPos > width() - 20) {
			continue;
		}

		double diff = m_frameDifferences[i];
		double normalized = diff / maxDiff; // 0.0 to 1.0

		// Bar height (max 20 pixels)
		int barHeight = (int)(normalized * MAX_BAR_HEIGHT);

		if (barHeight <= 0) {
			continue; // Skip zero-height bars
		}

		// Color based on difference intensity
		QColor barColor;
		if (normalized < 0.3) {
			barColor = QColor(0, 255, 0, 200); // Green - low change
		} else if (normalized < 0.7) {
			barColor = QColor(255, 255, 0, 200); // Yellow - medium change
		} else {
			barColor = QColor(255, 0, 0, 200); // Red - high change
		}

		painter.setBrush(QBrush(barColor));
		painter.drawRect(xPos - BAR_WIDTH / 2, BAR_BASE_Y - barHeight, BAR_WIDTH, barHeight);
	}
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
	drawFrameDifferenceBars(painter);
	drawVideoFrameMarkers(painter);
	drawOffsetLine(painter);
	drawSpikeMarker(painter);
	drawVideoFramePosition(painter);
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		int mouseX = static_cast<int>(event->position().x());
		int const SPIKE_X = xFromTimestamp(m_spikePosition);
		int const FRAME_X = m_videoFramePosition >= 0.0 ? xFromTimestamp(m_videoFramePosition) : -1000;

		// Check if clicking near video frame marker (within 10 pixels) - prioritize this
		if (m_videoFramePosition >= 0.0 && qAbs(mouseX - FRAME_X) < 10) {
			m_draggingVideoFrame = true;
			m_spikeDragStartX = mouseX;
		} else if (qAbs(mouseX - SPIKE_X) < 10) {
			// Check if clicking near spike marker (within 10 pixels)
			m_draggingSpike = true;
			m_spikeDragStartX = mouseX;
		} else {
			// Click to set spike position (default behavior)
			double newTimestamp = timestampFromX(mouseX);
			newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
			setSpikePosition(newTimestamp);
			emit spikePositionChanged(newTimestamp);
		}
	}
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (m_draggingVideoFrame && (event->buttons() & Qt::LeftButton)) {
		int mouseX = static_cast<int>(event->position().x());
		double newTimestamp = timestampFromX(mouseX);
		newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
		setVideoFramePosition(newTimestamp);
		emit videoFramePositionChanged(newTimestamp);
	} else if (m_draggingSpike && (event->buttons() & Qt::LeftButton)) {
		int mouseX = static_cast<int>(event->position().x());
		double newTimestamp = timestampFromX(mouseX);
		newTimestamp = qMax(m_startTime, qMin(m_endTime, newTimestamp));
		setSpikePosition(newTimestamp);
		emit spikePositionChanged(newTimestamp);
	} else {
		m_draggingSpike = false;
		m_draggingVideoFrame = false;
	}
}

void TimelineWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	update();
}

void TimelineWidget::wheelEvent(QWheelEvent *event)
{
	// Zoom with mouse wheel
	if (event->angleDelta().y() > 0) {
		zoomIn();
	} else if (event->angleDelta().y() < 0) {
		zoomOut();
	}
	event->accept();
}
