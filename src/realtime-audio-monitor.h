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
#include "audio-analyzer.h"
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

	// Start monitoring a recording file
	bool startMonitoring(const QString &filePath);

	// Stop monitoring
	void stopMonitoring();

	// Check if monitoring is active
	bool isMonitoring() const { return m_monitoring; }

signals:
	void spikeDetected(double timestamp);          // Spike detected at timestamp
	void recordingComplete(double spikeTimestamp); // Recording complete (2s after spike)
	void volumeLevelsUpdated(double baseline, double current, double threshold); // Volume levels for UI display
	void monitoringError(const QString &error);

private slots:
	void checkForSpike(); // Called periodically

private:
	QTimer *m_checkTimer{nullptr};
	QString m_filePath;
	QVector<AudioSample> m_recentSamples; // Keep recent samples for threshold
	double m_spikeThreshold{4.0};         // Threshold multiplier (4x average)
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
	QElapsedTimer m_recordingStartTime;
	double m_lastCheckPosition{0.0}; // Last audio position checked

	// Calculate average amplitude of recent samples
	double calculateBaselineAverage() const;

	// Check if current samples indicate a spike
	bool detectSpike(const QVector<AudioSample> &newSamples);
};

#endif // REALTIME_AUDIO_MONITOR_H
