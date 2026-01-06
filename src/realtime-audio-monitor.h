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

#ifndef REALTIME_AUDIO_MONITOR_H
#define REALTIME_AUDIO_MONITOR_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QElapsedTimer>
#include <QMutex>
#include "audio-analyzer.h"
#include <obs-module.h>
#include <qtmetamacros.h>

class RealTimeAudioMonitor : public QObject {
	Q_OBJECT

public:
	explicit RealTimeAudioMonitor(QObject *parent = nullptr);
	~RealTimeAudioMonitor() override;

	// Delete copy and move constructors/assignments
	RealTimeAudioMonitor(const RealTimeAudioMonitor &) = delete;
	RealTimeAudioMonitor &operator=(const RealTimeAudioMonitor &) = delete;
	RealTimeAudioMonitor(RealTimeAudioMonitor &&) = delete;
	RealTimeAudioMonitor &operator=(RealTimeAudioMonitor &&) = delete;

	// Start monitoring OBS audio output from the specified source
	// If sourceName is empty, will try to find a default source
	bool startMonitoring(const QString &sourceName = QString());

	// Stop monitoring
	void stopMonitoring();

	// Check if monitoring is active
	bool isMonitoring() const { return m_monitoring; }

signals:
	void spikeDetected(double timestamp);          // Spike detected at timestamp
	void recordingComplete(double spikeTimestamp); // Recording complete (2s after spike)
	void volumeLevelsUpdated(double baseline, double current, double threshold, double minVol, double maxVol,
				 double avgVol); // Volume levels for UI display
	void monitoringError(const QString &error);

private slots:
	void processVolumeData(); // Called periodically to process buffered volume samples

private:
	QTimer *m_processTimer{nullptr};
	obs_source_t *m_audioSource{nullptr}; // Master audio source
	obs_volmeter_t *m_volmeter{nullptr};  // Volume meter for audio monitoring
	QMutex m_audioMutex;                  // Protects audio buffer
	QVector<AudioSample> m_audioBuffer;   // Buffer for volume samples from volmeter
	double m_currentTime{0.0};            // Current time in seconds since monitoring started
	QVector<AudioSample> m_recentSamples; // Keep recent samples for threshold
	double m_spikeThreshold{2.0};         // Threshold multiplier (2x average)
	double m_baselineWindowSeconds{2.0};  // Window for baseline calculation (2 seconds)
	double m_minSpikeDuration{0.05};      // Minimum spike duration (50ms)
	double m_maxSpikeDuration{0.2};       // Maximum duration for valid clap (200ms)
	double m_spikeEndThreshold{1.5};      // Threshold multiplier for spike end detection
	double m_postSpikeDuration{2.0};      // Seconds to record after spike
	bool m_monitoring{false};
	bool m_baselineCollected{false}; // True after 2 seconds of baseline collected
	bool m_spikeDetected{false};     // True when spike has been detected
	bool m_spikeInProgress{false};   // True when currently tracking a spike
	double m_spikeTimestamp{0.0};    // Timestamp when spike was detected
	double m_spikeStartTime{0.0};    // Timestamp when current spike started
	int m_postSpikeCount{0};         // Count of additional spikes during post-spike period
	QElapsedTimer m_recordingStartTime;
	// Statistics tracking
	double m_minVolume{1.0}; // Minimum volume seen (initialized to max)
	double m_maxVolume{0.0}; // Maximum volume seen
	double m_volumeSum{0.0}; // Sum of all volume samples
	int m_volumeCount{0};    // Count of volume samples
	// Cached threshold values (recalculated when baseline changes)
	double m_cachedBaseline{0.0};        // Cached baseline average
	double m_cachedThreshold{0.0};       // Cached spike detection threshold
	double m_cachedSpikeEndThreshold{0.0}; // Cached spike end threshold

	// OBS volmeter callback (static, forwards to instance)
	static void volmeterCallback(void *param, const float magnitude[], const float peak[], const float inputPeak[]);

	// Calculate average amplitude of recent samples
	double calculateBaselineAverage() const;

	// Calculate p99 (99th percentile) of audio samples
	double calculateP99(const QVector<AudioSample> &samples) const;

	// Convert linear amplitude (0.0-1.0) to decibels
	double amplitudeToDb(double amplitude) const;

	// Calculate adaptive threshold multiplier based on baseline noise level
	double calculateAdaptiveThreshold(double baselineAmplitude) const;

	// Collect baseline samples and check if baseline collection is complete
	// Returns true if baseline collection just completed
	bool collectBaseline(const QVector<AudioSample> &newSamples);

	// Check if current samples indicate a spike
	bool detectSpike(const QVector<AudioSample> &newSamples);
};

#endif // REALTIME_AUDIO_MONITOR_H
