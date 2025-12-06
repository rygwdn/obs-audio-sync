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

#include "realtime-audio-monitor.h"
#include "audio-analyzer.h"
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

RealTimeAudioMonitor::RealTimeAudioMonitor(QObject *parent) : QObject(parent)
{
	m_checkTimer = new QTimer(this);
	m_checkTimer->setInterval(200); // Check every 200ms
	connect(m_checkTimer, &QTimer::timeout, this, &RealTimeAudioMonitor::checkForSpike);
}

RealTimeAudioMonitor::~RealTimeAudioMonitor()
{
	stopMonitoring();
}

bool RealTimeAudioMonitor::startMonitoring(const QString &filePath)
{
	if (m_monitoring) {
		qWarning() << "RealTimeAudioMonitor: Already monitoring";
		return false;
	}

	QFileInfo fileInfo(filePath);
	if (!fileInfo.exists()) {
		// File might not exist yet - that's okay, we'll retry
		qInfo() << "RealTimeAudioMonitor: File does not exist yet, will retry:" << filePath;
	}

	m_filePath = filePath;
	m_recentSamples.clear();
	m_lastCheckPosition = 0.0;
	m_monitoring = true;
	m_recordingStartTime.start();

	// Start checking after a short delay to allow file to be created
	QTimer::singleShot(500, this, [this]() {
		if (m_monitoring) {
			m_checkTimer->start();
		}
	});

	return true;
}

void RealTimeAudioMonitor::stopMonitoring()
{
	if (!m_monitoring) {
		return;
	}

	m_monitoring = false;
	m_checkTimer->stop();
	m_recentSamples.clear();
	m_filePath.clear();
}

double RealTimeAudioMonitor::calculateBaselineAverage() const
{
	if (m_recentSamples.isEmpty()) {
		return 0.0;
	}

	// Calculate average of samples within baseline window
	double sum = 0.0;
	int count = 0;
	double currentTime = m_recentSamples.isEmpty() ? 0.0 : m_recentSamples.last().timestamp;

	for (const AudioSample &sample : m_recentSamples) {
		// Only include samples within the baseline window
		if (currentTime - sample.timestamp <= m_baselineWindowSeconds) {
			sum += sample.amplitude;
			count++;
		}
	}

	return count > 0 ? (sum / count) : 0.0;
}

bool RealTimeAudioMonitor::detectSpike(const QVector<AudioSample> &newSamples)
{
	if (newSamples.isEmpty()) {
		return false;
	}

	// Calculate baseline average from recent samples
	double baseline = calculateBaselineAverage();
	if (baseline <= 0.0) {
		// Not enough data yet, add samples to recent and return
		m_recentSamples.append(newSamples);
		return false;
	}

	double threshold = baseline * m_spikeThreshold;

	// Check new samples for spike
	for (const AudioSample &sample : newSamples) {
		if (sample.amplitude > threshold) {
			// Potential spike detected - check duration
			// For now, if amplitude exceeds threshold, consider it a spike
			// In a more sophisticated implementation, we'd track spike duration
			return true;
		}
	}

	// No spike, add samples to recent (maintaining window)
	m_recentSamples.append(newSamples);

	// Remove old samples outside baseline window
	double currentTime = newSamples.isEmpty() ? 0.0 : newSamples.last().timestamp;
	m_recentSamples.erase(std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
					     [currentTime, this](const AudioSample &s) {
						     return (currentTime - s.timestamp) > m_baselineWindowSeconds;
					     }),
			      m_recentSamples.end());

	return false;
}

void RealTimeAudioMonitor::checkForSpike()
{
	if (!m_monitoring || m_filePath.isEmpty()) {
		return;
	}

	// Check minimum recording duration
	double elapsedSeconds = m_recordingStartTime.elapsed() / 1000.0;
	if (elapsedSeconds < m_minRecordingDuration) {
		return; // Too early to check
	}

	QFileInfo fileInfo(m_filePath);
	if (!fileInfo.exists()) {
		// File doesn't exist yet, wait
		return;
	}

	// Check file size - if it hasn't changed, might be done recording
	static qint64 lastSize = 0;
	qint64 currentSize = fileInfo.size();
	if (currentSize == lastSize && currentSize > 0) {
		// File size hasn't changed - might be done or locked
		// Continue checking anyway
	}
	lastSize = currentSize;

	// Try to read audio samples from the file
	// Read from last check position to current end
	try {
		// Get all samples from the file (FFmpeg will handle partial files)
		QVector<AudioSample> allSamples = AudioAnalyzer::extractAudioSamples(m_filePath);

		if (allSamples.isEmpty()) {
			// No samples yet, file might still be initializing
			return;
		}

		// Get new samples since last check
		QVector<AudioSample> newSamples;
		for (const AudioSample &sample : allSamples) {
			if (sample.timestamp > m_lastCheckPosition) {
				newSamples.append(sample);
			}
		}

		if (!newSamples.isEmpty()) {
			// Update last check position
			m_lastCheckPosition = newSamples.last().timestamp;

			// Check for spike
			if (detectSpike(newSamples)) {
				// Spike detected - find the timestamp of the spike
				double spikeTimestamp = 0.0;
				double maxAmplitude = 0.0;
				double baseline = calculateBaselineAverage();
				double threshold = baseline * m_spikeThreshold;

				for (const AudioSample &sample : newSamples) {
					if (sample.amplitude > threshold && sample.amplitude > maxAmplitude) {
						maxAmplitude = sample.amplitude;
						spikeTimestamp = sample.timestamp;
					}
				}

				if (spikeTimestamp > 0.0) {
					emit spikeDetected(spikeTimestamp);
					stopMonitoring();
				}
			}
		}
	} catch (const std::exception &e) {
		emit monitoringError(QString("Error reading audio: %1").arg(e.what()));
		stopMonitoring();
	} catch (...) {
		emit monitoringError("Unknown error reading audio");
		stopMonitoring();
	}
}
