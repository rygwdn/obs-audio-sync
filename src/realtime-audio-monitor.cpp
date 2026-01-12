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

	// Log source information
	const char *sourceId = obs_source_get_id(source);
	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Source has audio - ID: %s, Name: %s, Flags: 0x%x",
	     sourceId ? sourceId : "unknown", sourceName.toUtf8().constData(), outputFlags);

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
	m_postSpikeCount = 0;
	m_recordingStartTime.start();
	// Reset statistics
	m_minVolume = 1.0;
	m_maxVolume = 0.0;
	m_volumeSum = 0.0;
	m_volumeCount = 0;

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Monitoring initialized, waiting for audio callbacks...");

	// Create and attach volmeter
	// Use CUBIC fader type for better amplitude representation
	m_volmeter = obs_volmeter_create(OBS_FADER_CUBIC);
	if (m_volmeter == nullptr) {
		obs_source_release(m_audioSource);
		m_audioSource = nullptr;
		blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Failed to create volmeter");
		emit monitoringError("Failed to create OBS volume meter");
		return false;
	}

	// Attach volmeter to source
	if (!obs_volmeter_attach_source(m_volmeter, m_audioSource)) {
		obs_volmeter_destroy(m_volmeter);
		m_volmeter = nullptr;
		obs_source_release(m_audioSource);
		m_audioSource = nullptr;
		blog(LOG_ERROR, "[AudioSync] RealTimeAudioMonitor: Failed to attach volmeter to source");
		emit monitoringError("Failed to attach volume meter to audio source");
		return false;
	}

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Volmeter attached to source");

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
	m_postSpikeCount = 0;
	m_currentTime = 0.0;

	blog(LOG_INFO, "[AudioSync] RealTimeAudioMonitor: Stopped monitoring");
}

void RealTimeAudioMonitor::volmeterCallback(void *param, const float magnitude[], const float peak[],
					    const float inputPeak[])
{
	Q_UNUSED(magnitude);
	Q_UNUSED(inputPeak);
	auto *monitor = static_cast<RealTimeAudioMonitor *>(param);
	if (monitor == nullptr || !monitor->m_monitoring) {
		return;
	}

	// Lock mutex to protect audio buffer
	QMutexLocker locker(&monitor->m_audioMutex);

	// OBS volmeter returns peak values in decibels (dB), not linear amplitude
	// peak[] contains peak dB values per channel
	// Need to convert from dB to linear amplitude (0.0 to 1.0)
	float channelPeakDB = -INFINITY;
	if (peak != nullptr) {
		channelPeakDB = peak[0]; // Use first channel (in dB)
	}

	// Convert from dB to linear amplitude
	// Formula: amplitude = 10^(dB / 20)
	// Handle -inf dB (silence) as 0.0 amplitude
	float channelAmplitude = 0.0f;
	if (std::isfinite(channelPeakDB)) {
		channelAmplitude = std::pow(10.0f, channelPeakDB / 20.0f);
	}

	// Calculate timestamp (volmeter updates at audio rate, typically ~20ms intervals)
	// Use elapsed time since recording started
	double timestamp = monitor->m_recordingStartTime.elapsed() / 1000.0; // Convert ms to seconds

	// Log first few samples for debugging
	static int sampleCount = 0;
	if (sampleCount < 10) {
		blog(LOG_INFO,
		     "[AudioSync] RealTimeAudioMonitor: Sample #%d - timestamp: %.3fs, peak_dB: %.6f, amplitude: %.6f",
		     sampleCount, timestamp, channelPeakDB, channelAmplitude);
		sampleCount++;
	}

	// Store in buffer for processing
	AudioSample sample{};
	sample.timestamp = timestamp;
	sample.amplitude = static_cast<double>(channelAmplitude);
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
			// Log if we're not getting samples
			static double lastWarningTime = 0.0;
			double currentTime = m_recordingStartTime.elapsed() / 1000.0;
			if (currentTime - lastWarningTime >= 2.0) {
				blog(LOG_WARNING,
				     "[AudioSync] RealTimeAudioMonitor: No audio samples received in last 2 seconds");
				lastWarningTime = currentTime;
			}
			return;
		}
		newSamples = m_audioBuffer;
		m_audioBuffer.clear();
	}

	if (newSamples.isEmpty()) {
		return;
	}

	blog(LOG_DEBUG, "[AudioSync] RealTimeAudioMonitor: Processing %lld samples",
	     static_cast<long long>(newSamples.size()));

	// Update last check position
	double currentTime = newSamples.last().timestamp;

	// Update statistics
	for (const AudioSample &sample : newSamples) {
		double amp = sample.amplitude;
		if (amp < m_minVolume) {
			m_minVolume = amp;
		}
		if (amp > m_maxVolume) {
			m_maxVolume = amp;
		}
		m_volumeSum += amp;
		m_volumeCount++;
	}

	// Calculate and emit volume levels for UI display
	double baseline = 0.0;
	double current = 0.0;
	double threshold = 0.0;
	double avgVol = m_volumeCount > 0 ? (m_volumeSum / m_volumeCount) : 0.0;

	if (m_baselineCollected) {
		// Use stepped threshold values based on baseline noise floor:
		// - Very quiet (< -30dB): threshold = -20dB
		// - Quiet (-30dB to -20dB): threshold = -15dB
		// - Moderate/Noisy (>= -20dB): threshold = -5dB
		baseline = m_cachedBaseline;
		threshold = m_cachedThreshold;

		// Get current level using p99 of recent samples
		if (!newSamples.isEmpty()) {
			current = calculateP99(newSamples);
		} else if (!m_recentSamples.isEmpty()) {
			current = calculateP99(m_recentSamples);
		}
	} else {
		// During baseline collection, show current level using p99
		if (!newSamples.isEmpty()) {
			current = calculateP99(newSamples);
		} else if (!m_recentSamples.isEmpty()) {
			current = calculateP99(m_recentSamples);
		}
	}

	emit volumeLevelsUpdated(baseline, current, threshold, m_minVolume, m_maxVolume, avgVol);

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

		// Check if additional spikes are happening during post-spike period
		// If current level exceeds threshold, increment spike counter
		if (current > threshold) {
			m_postSpikeCount++;
			// If we've detected 3+ additional spikes, the environment is too noisy
			// Reset and recalculate baseline
			if (m_postSpikeCount >= 3) {
				blog(LOG_WARNING,
				     "[AudioSync] RealTimeAudioMonitor: Too many spikes during post-spike period (%d detected), resetting baseline",
				     m_postSpikeCount);

				// Reset spike detection state
				m_spikeDetected = false;
				m_spikeInProgress = false;
				m_spikeTimestamp = 0.0;
				m_spikeStartTime = 0.0;
				m_postSpikeCount = 0;
				m_baselineCollected = false;

				// Clear recent samples to start fresh baseline collection
				m_recentSamples.clear();

				blog(LOG_INFO,
				     "[AudioSync] RealTimeAudioMonitor: Restarting baseline collection due to noisy environment");
				return;
			}
		}
		// Continue monitoring, but don't check for new spikes via detectSpike
	} else {
		// Check for spike
		if (detectSpike(newSamples)) {
			// Valid spike (clap) detected
			if (m_spikeTimestamp > 0.0) {
				m_postSpikeCount = 0; // Reset counter when valid spike is detected
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

double RealTimeAudioMonitor::calculateP99(const QVector<AudioSample> &samples) const
{
	if (samples.isEmpty()) {
		return 0.0;
	}

	// Extract amplitudes from samples
	QVector<double> amplitudes;
	amplitudes.reserve(samples.size());
	for (const AudioSample &sample : samples) {
		amplitudes.append(sample.amplitude);
	}

	// Sort amplitudes to find percentile
	std::sort(amplitudes.begin(), amplitudes.end());

	// Calculate p99 index (99th percentile)
	// Use ceiling to be conservative (prefer higher values for spike detection)
	qsizetype p99Index = static_cast<qsizetype>(std::ceil(amplitudes.size() * 0.99)) - 1;
	p99Index = std::max(qsizetype(0), std::min(p99Index, amplitudes.size() - 1));

	return amplitudes[p99Index];
}

double RealTimeAudioMonitor::amplitudeToDb(double amplitude) const
{
	if (amplitude <= 0.0) {
		return -INFINITY;
	}
	// Convert linear amplitude to dB: dB = 20 * log10(amplitude)
	return 20.0 * std::log10(amplitude);
}

double RealTimeAudioMonitor::calculateAdaptiveThreshold(double baselineAmplitude) const
{
	// Convert baseline to dB for stepped threshold calculation
	double baselineDb = amplitudeToDb(baselineAmplitude);

	// Stepped threshold values based on baseline noise floor:
	// This provides a less dynamic threshold by using fixed dB values
	// rather than multiplying the baseline amplitude
	// - Very quiet environment (< -30dB): threshold = -20dB
	// - Quiet environment (-30dB to -20dB): threshold = -15dB
	// - Moderate/Noisy environment (>= -20dB): threshold = -5dB

	double thresholdDb;
	if (baselineDb < -30.0) {
		thresholdDb = -20.0;
	} else if (baselineDb < -20.0) {
		thresholdDb = -15.0;
	} else {
		thresholdDb = -5.0;
	}

	// Convert threshold from dB back to linear amplitude
	// Formula: amplitude = 10^(dB / 20)
	return std::pow(10.0, thresholdDb / 20.0);
}

bool RealTimeAudioMonitor::collectBaseline(const QVector<AudioSample> &newSamples)
{
	// Add new samples to recent samples buffer
	m_recentSamples.append(newSamples);

	if (m_recentSamples.isEmpty()) {
		return false;
	}

	double currentTime = m_recentSamples.last().timestamp;
	double firstTime = m_recentSamples.first().timestamp;
	double elapsed = currentTime - firstTime;

	if (elapsed >= m_baselineWindowSeconds) {
		// Baseline collection complete
		m_baselineCollected = true;

		// Calculate and cache baseline and threshold using stepped threshold values
		m_cachedBaseline = calculateBaselineAverage();
		double steppedThreshold = calculateAdaptiveThreshold(m_cachedBaseline);
		m_cachedThreshold = std::min(steppedThreshold, 0.9);

		double baselineDb = amplitudeToDb(m_cachedBaseline);
		double thresholdDb = amplitudeToDb(steppedThreshold);
		if (steppedThreshold > 0.9) {
			blog(LOG_WARNING,
			     "[AudioSync] RealTimeAudioMonitor: Noisy environment detected - baseline: %.6f (%.1f dB), threshold clamped from %.6f (%.1f dB) to %.6f",
			     m_cachedBaseline, baselineDb, steppedThreshold, thresholdDb, m_cachedThreshold);
		}

		blog(LOG_INFO,
		     "[AudioSync] RealTimeAudioMonitor: Baseline collected (%.2fs), average: %.6f (%.1f dB), threshold: %.6f (%.1f dB), samples: %lld",
		     elapsed, m_cachedBaseline, baselineDb, m_cachedThreshold, amplitudeToDb(m_cachedThreshold),
		     static_cast<long long>(m_recentSamples.size()));

		// Remove old samples outside baseline window
		m_recentSamples.erase(std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
						     [currentTime, this](const AudioSample &s) {
							     return (currentTime - s.timestamp) >
								    m_baselineWindowSeconds;
						     }),
				      m_recentSamples.end());

		return true;
	}

	// Still collecting baseline - log progress every 0.5 seconds
	static double lastLogTime = 0.0;
	if (currentTime - lastLogTime >= 0.5) {
		double currentAvg = calculateBaselineAverage();
		blog(LOG_INFO,
		     "[AudioSync] RealTimeAudioMonitor: Collecting baseline: %.2fs / %.2fs, samples: %lld, current avg: %.6f",
		     elapsed, m_baselineWindowSeconds, static_cast<long long>(m_recentSamples.size()), currentAvg);
		lastLogTime = currentTime;
	}

	// Remove old samples outside baseline window
	m_recentSamples.erase(std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
					     [currentTime, this](const AudioSample &s) {
						     return (currentTime - s.timestamp) > m_baselineWindowSeconds;
					     }),
			      m_recentSamples.end());

	return false;
}

bool RealTimeAudioMonitor::detectSpike(const QVector<AudioSample> &newSamples)
{
	if (newSamples.isEmpty()) {
		return false;
	}

	// Check if baseline has been collected (2 seconds of data)
	if (!m_baselineCollected) {
		// Collect baseline samples
		collectBaseline(newSamples);
		return false;
	}

	// Baseline collected, now detect spikes using cached thresholds
	if (m_cachedBaseline <= 0.0) {
		// Should not happen if baseline is collected, but handle gracefully
		m_recentSamples.append(newSamples);
		return false;
	}

	// Use cached threshold value instead of recalculating
	double threshold = m_cachedThreshold;

	// Log spike detection parameters once per detection cycle
	static bool loggedParams = false;
	if (!loggedParams) {
		blog(LOG_INFO,
		     "[AudioSync] RealTimeAudioMonitor: Spike detection active - baseline: %.6f, threshold: %.6f",
		     m_cachedBaseline, threshold);
		loggedParams = true;
	}

	// Check new samples for spike
	// Note: Each sample is already a peak value from the volmeter (~20ms windows),
	// so no additional averaging is needed. Using peak values directly allows us
	// to detect brief transients like claps.
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
			// Amplitude dropped below threshold - spike ended
			double spikeDuration = sample.timestamp - m_spikeStartTime;
			if (spikeDuration >= m_minSpikeDuration && spikeDuration <= m_maxSpikeDuration) {
				// Valid short spike (clap) detected
				m_spikeDetected = true;
				m_spikeTimestamp = m_spikeStartTime;
				m_spikeInProgress = false;

				// Find peak amplitude for logging
				double peakAmplitude = 0.0;
				for (const AudioSample &s : newSamples) {
					if (s.timestamp >= m_spikeStartTime && s.timestamp <= sample.timestamp) {
						if (s.amplitude > peakAmplitude) {
							peakAmplitude = s.amplitude;
						}
					}
				}

				blog(LOG_INFO,
				     "[AudioSync] RealTimeAudioMonitor: Valid clap detected! Time: %.3fs, Duration: %.3fs, Peak: %.6f (%.1fx baseline)",
				     m_spikeTimestamp, spikeDuration, peakAmplitude, peakAmplitude / m_cachedBaseline);

				// Add samples to recent for baseline maintenance
				m_recentSamples.append(newSamples);
				// Remove old samples outside baseline window
				double currentTime = newSamples.isEmpty() ? 0.0 : newSamples.last().timestamp;
				m_recentSamples.erase(std::remove_if(m_recentSamples.begin(), m_recentSamples.end(),
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
