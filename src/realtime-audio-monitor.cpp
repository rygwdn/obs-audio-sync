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
#include <obs-module.h>
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
		blog(LOG_WARNING, "[AudioSync] RealTimeAudioMonitor: Already monitoring");
		return false;
	}

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Starting monitoring for file: %s",
	     filePath.toUtf8().constData());

	QFileInfo fileInfo(filePath);
	if (!fileInfo.exists()) {
		// File might not exist yet - that's okay, we'll retry
		blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: File does not exist yet, will retry: %s",
		     filePath.toUtf8().constData());
	} else {
		blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: File exists, size: %lld bytes", fileInfo.size());
	}

	m_filePath = filePath;
	m_recentSamples.clear();
	m_lastCheckPosition = 0.0;
	m_monitoring = true;
	m_baselineCollected = false;
	m_spikeDetected = false;
	m_spikeInProgress = false;
	m_spikeTimestamp = 0.0;
	m_spikeStartTime = 0.0;
	m_recordingStartTime.start();

	// Start checking after a short delay to allow file to be created
	QTimer::singleShot(500, this, [this]() {
		if (m_monitoring) {
			blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Starting check timer");
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
	m_baselineCollected = false;
	m_spikeDetected = false;
	m_spikeInProgress = false;
	m_spikeTimestamp = 0.0;
	m_spikeStartTime = 0.0;
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

	// Check if baseline has been collected (2 seconds of data)
	if (!m_baselineCollected) {
		// Still collecting baseline - add samples and check if we have 2 seconds
		m_recentSamples.append(newSamples);
		if (!m_recentSamples.isEmpty()) {
			double currentTime = m_recentSamples.last().timestamp;
			double firstTime = m_recentSamples.first().timestamp;
			double elapsed = currentTime - firstTime;
			if (elapsed >= m_baselineWindowSeconds) {
				m_baselineCollected = true;
				double baseline = calculateBaselineAverage();
				blog(LOG_INFO,
				     "[AudioSync] RealTimeAudioMonitor: Baseline collected (%.2fs), average: %.6f, threshold: %.6f",
				     elapsed, baseline, baseline * m_spikeThreshold);
			} else {
				blog(LOG_DEBUG,
				     "[AudioSync] RealTimeAudioMonitor: Collecting baseline: %.2fs / %.2fs, samples: %d",
				     elapsed, m_baselineWindowSeconds, m_recentSamples.size());
			}
		}
		// Remove old samples outside baseline window
		double currentTime = newSamples.isEmpty() ? 0.0 : newSamples.last().timestamp;
		m_recentSamples.erase(std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
						     [currentTime, this](const AudioSample &s) {
							     return (currentTime - s.timestamp) >
								    m_baselineWindowSeconds;
						     }),
				      m_recentSamples.end());
		return false;
	}

	// Baseline collected, now detect spikes
	double baseline = calculateBaselineAverage();
	if (baseline <= 0.0) {
		// Should not happen if baseline is collected, but handle gracefully
		m_recentSamples.append(newSamples);
		return false;
	}

	double threshold = baseline * m_spikeThreshold;
	double spikeEndThreshold = baseline * m_spikeEndThreshold;

	// Check new samples for spike
	for (const AudioSample &sample : newSamples) {
		if (m_spikeInProgress) {
			// Check if spike has exceeded maximum duration
			double spikeDuration = sample.timestamp - m_spikeStartTime;
			if (spikeDuration > m_maxSpikeDuration) {
				// Spike too long, not a valid clap - reject it
				m_spikeInProgress = false;
			}
		}

		if (sample.amplitude > threshold) {
			// Potential spike start
			if (!m_spikeInProgress) {
				m_spikeStartTime = sample.timestamp;
				m_spikeInProgress = true;
			}
		} else if (m_spikeInProgress) {
			// Amplitude dropped below threshold - spike may have ended
			// Check if amplitude is below end threshold (spike definitely ended)
			if (sample.amplitude < spikeEndThreshold) {
				// Spike ended - check if it's a valid short spike (clap)
				double spikeDuration = sample.timestamp - m_spikeStartTime;
				if (spikeDuration >= m_minSpikeDuration && spikeDuration <= m_maxSpikeDuration) {
					// Valid short spike (clap) detected
					m_spikeDetected = true;
					m_spikeTimestamp = m_spikeStartTime;
					m_spikeInProgress = false;

					// Find peak amplitude for logging
					double peakAmplitude = 0.0;
					for (const AudioSample &s : newSamples) {
						if (s.timestamp >= m_spikeStartTime &&
						    s.timestamp <= sample.timestamp) {
							if (s.amplitude > peakAmplitude) {
								peakAmplitude = s.amplitude;
							}
						}
					}

					blog(LOG_INFO,
					     "[AudioSync] RealTimeAudioMonitor: Valid clap detected! Time: %.3fs, Duration: %.3fs, Peak: %.6f (%.1fx baseline)",
					     m_spikeTimestamp, spikeDuration, peakAmplitude, peakAmplitude / baseline);

					// Add samples to recent for baseline maintenance
					m_recentSamples.append(newSamples);
					// Remove old samples outside baseline window
					double currentTime = newSamples.isEmpty() ? 0.0 : newSamples.last().timestamp;
					m_recentSamples.erase(
						std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
							       [currentTime, this](const AudioSample &s) {
								       return (currentTime - s.timestamp) >
									      m_baselineWindowSeconds;
							       }),
						m_recentSamples.end());
					return true;
				} else {
					// Spike too short or too long, not a valid clap
					m_spikeInProgress = false;
				}
			}
			// If amplitude is between threshold and endThreshold, spike might still be ongoing
		}
	}

	// No spike detected, add samples to recent (maintaining window)
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
			blog(LOG_DEBUG,
			     "[AudioSync] RealTimeAudioMonitor: No audio samples yet, file may still be initializing");
			return;
		}

		blog(LOG_DEBUG, "[AudioSync] RealTimeAudioMonitor: Extracted %d audio samples, last timestamp: %.3fs",
		     allSamples.size(), allSamples.isEmpty() ? 0.0 : allSamples.last().timestamp);

		// Get new samples since last check
		QVector<AudioSample> newSamples;
		for (const AudioSample &sample : allSamples) {
			if (sample.timestamp > m_lastCheckPosition) {
				newSamples.append(sample);
			}
		}

		if (!newSamples.isEmpty()) {
			// Update last check position
			double currentTime = newSamples.last().timestamp;

			// Calculate and emit volume levels for UI display
			double baseline = 0.0;
			double current = 0.0;
			double threshold = 0.0;

			if (m_baselineCollected) {
				baseline = calculateBaselineAverage();
				threshold = baseline * m_spikeThreshold;
				// Get current level from most recent sample
				if (!newSamples.isEmpty()) {
					current = newSamples.last().amplitude;
				} else if (!m_recentSamples.isEmpty()) {
					current = m_recentSamples.last().amplitude;
				}
			} else {
				// During baseline collection, show current level
				if (!newSamples.isEmpty()) {
					current = newSamples.last().amplitude;
				} else if (!m_recentSamples.isEmpty()) {
					current = m_recentSamples.last().amplitude;
				}
			}

			emit volumeLevelsUpdated(baseline, current, threshold);

			if (m_spikeDetected) {
				// Spike already detected, check if 2 seconds have passed
				double elapsedSinceSpike = currentTime - m_spikeTimestamp;
				if (elapsedSinceSpike >= m_postSpikeDuration) {
					// 2 seconds have passed, stop monitoring
					blog(LOG_INFO,
					     "[AudioSync] RealTimeAudioMonitor: Post-spike period complete (%.2fs), stopping monitoring",
					     elapsedSinceSpike);
					emit recordingComplete(m_spikeTimestamp);
					stopMonitoring();
					return;
				}
				// Continue monitoring, but don't check for new spikes
			} else {
				// Check for spike
				if (detectSpike(newSamples)) {
					// Valid spike (clap) detected
					if (m_spikeTimestamp > 0.0) {
						emit spikeDetected(m_spikeTimestamp);
						// Continue monitoring for 2 more seconds
					}
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
