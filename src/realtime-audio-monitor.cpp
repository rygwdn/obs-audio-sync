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
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QDebug>
#include <algorithm>
#include <cmath>

RealTimeAudioMonitor::RealTimeAudioMonitor(QObject *parent) : QObject(parent)
{
	m_processTimer = new QTimer(this);
	m_processTimer->setInterval(50); // Process volume data every 50ms
	connect(m_processTimer, &QTimer::timeout, this, &RealTimeAudioMonitor::processVolumeData);
	m_audioSource = nullptr;
	m_volmeter = nullptr;
}

RealTimeAudioMonitor::~RealTimeAudioMonitor()
{
	stopMonitoring();
}

bool RealTimeAudioMonitor::startMonitoring(const QString &sourceName)
{
	if (m_monitoring) {
		blog(LOG_WARNING, "[AudioSync] RealTimeAudioMonitor: Already monitoring");
		return false;
	}

	obs_source_t *source = nullptr;

	if (!sourceName.isEmpty()) {
		// Get source by name
		blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Starting monitoring of source: %s",
		     sourceName.toUtf8().constData());
		source = obs_get_source_by_name(sourceName.toUtf8().constData());
		if (source == nullptr) {
			blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Source not found: %s",
			     sourceName.toUtf8().constData());
			emit monitoringError(QString("Audio source not found: %1").arg(sourceName));
			return false;
		}
	} else {
		// Fallback: try to get current scene
		blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: No source name provided, trying current scene");
		obs_source_t *scene = obs_frontend_get_current_scene();
		if (scene != nullptr) {
			uint32_t outputFlags = obs_source_get_output_flags(scene);
			if ((outputFlags & OBS_SOURCE_AUDIO) != 0) {
				source = scene;
				// scene already has a reference, we'll release it in stopMonitoring
			} else {
				obs_source_release(scene);
			}
		}

		if (source == nullptr) {
			blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Failed to get audio source for monitoring");
			emit monitoringError("Failed to get OBS audio source for monitoring");
			return false;
		}
	}

	// Check if source has audio
	uint32_t outputFlags = obs_source_get_output_flags(source);
	if ((outputFlags & OBS_SOURCE_AUDIO) == 0) {
		obs_source_release(source);
		blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Source does not have audio");
		emit monitoringError("OBS audio source does not have audio output");
		return false;
	}

	m_audioSource = source; // Don't release, we'll use it
	m_recentSamples.clear();
	m_audioBuffer.clear();
	m_currentTime = 0.0;
	m_monitoring = true;
	m_baselineCollected = false;
	m_spikeDetected = false;
	m_spikeInProgress = false;
	m_spikeTimestamp = 0.0;
	m_spikeStartTime = 0.0;
	m_recordingStartTime.start();

	// Create and attach volmeter
	m_volmeter = obs_volmeter_create(OBS_FADER_LOG);
	if (m_volmeter == nullptr) {
		obs_source_release(m_audioSource);
		m_audioSource = nullptr;
		blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Failed to create volmeter");
		emit monitoringError("Failed to create OBS volume meter");
		return false;
	}

	// Attach volmeter to source
	obs_volmeter_attach_source(m_volmeter, m_audioSource);

	// Add volmeter callback
	obs_volmeter_add_callback(m_volmeter, volmeterCallback, this);

	// Start processing timer
	m_processTimer->start();

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Started monitoring OBS audio output with volmeter");
	return true;
}

void RealTimeAudioMonitor::stopMonitoring()
{
	if (!m_monitoring) {
		return;
	}

	m_monitoring = false;
	m_processTimer->stop();

	// Remove volmeter callback and detach
	if (m_volmeter != nullptr) {
		obs_volmeter_remove_callback(m_volmeter, volmeterCallback, this);
		obs_volmeter_detach_source(m_volmeter);
		obs_volmeter_destroy(m_volmeter);
		m_volmeter = nullptr;
	}

	// Release audio source
	if (m_audioSource != nullptr) {
		obs_source_release(m_audioSource);
		m_audioSource = nullptr;
	}

	// Clear buffers
	QMutexLocker locker(&m_audioMutex);
	m_audioBuffer.clear();
	m_recentSamples.clear();
	m_baselineCollected = false;
	m_spikeDetected = false;
	m_spikeInProgress = false;
	m_spikeTimestamp = 0.0;
	m_spikeStartTime = 0.0;
	m_currentTime = 0.0;

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Stopped monitoring");
}

void RealTimeAudioMonitor::volmeterCallback(void *param, const float magnitude[], const float peak[],
					    const float inputPeak[])
{
	Q_UNUSED(peak);
	Q_UNUSED(inputPeak);
	auto *monitor = static_cast<RealTimeAudioMonitor *>(param);
	if (monitor == nullptr || !monitor->m_monitoring) {
		return;
	}

	// Lock mutex to protect audio buffer
	QMutexLocker locker(&monitor->m_audioMutex);

	// magnitude[] contains magnitude values per channel (0.0 to 1.0)
	// We'll use the first channel
	float channelMagnitude = 0.0f;
	if (magnitude != nullptr) {
		channelMagnitude = magnitude[0];
	}

	// Calculate timestamp (volmeter updates at audio rate, typically ~20ms intervals)
	// Use elapsed time since recording started
	double timestamp = monitor->m_recordingStartTime.elapsed() / 1000.0; // Convert ms to seconds

	// Store in buffer for processing
	AudioSample sample{};
	sample.timestamp = timestamp;
	sample.amplitude = static_cast<double>(channelMagnitude);
	monitor->m_audioBuffer.append(sample);
}

void RealTimeAudioMonitor::processVolumeData()
{
	if (!m_monitoring) {
		return;
	}

	// Get buffered audio samples
	QVector<AudioSample> newSamples;
	{
		QMutexLocker locker(&m_audioMutex);
		if (m_audioBuffer.isEmpty()) {
			return;
		}
		newSamples = m_audioBuffer;
		m_audioBuffer.clear();
	}

	if (newSamples.isEmpty()) {
		return;
	}

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
				     "[AudioSync] RealTimeAudioMonitor: Collecting baseline: %.2fs / %.2fs, samples: %lld",
				     elapsed, m_baselineWindowSeconds, static_cast<long long>(m_recentSamples.size()));
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
