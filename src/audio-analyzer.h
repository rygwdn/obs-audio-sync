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

#include <QString>
#include <QVector>
#include <QPair>

struct AudioSpike {
	double timestamp;   // Time in seconds where spike occurs
	double amplitude;   // Peak amplitude value
	double windowStart; // Start of 4-second window (2s before spike)
	double windowEnd;   // End of 4-second window (2s after spike)
};

struct AudioSample {
	double timestamp; // Time in seconds
	double amplitude; // RMS or peak amplitude
};

class AudioAnalyzer {
public:
	AudioAnalyzer();
	~AudioAnalyzer();

	// Analyze audio file and find largest spike
	bool analyzeFile(const QString &filePath, AudioSpike &spike);

	// Get audio samples for visualization (4-second window)
	QVector<AudioSample> getAudioSamples(const QString &filePath, double startTime, double endTime);

	// Get file duration in seconds
	double getFileDuration(const QString &filePath);

private:
	bool initializeFFmpeg();
	void cleanupFFmpeg();

	// Extract audio samples from file
	QVector<AudioSample> extractAudioSamples(const QString &filePath);

	// Find largest spike in audio samples
	AudioSpike findLargestSpike(const QVector<AudioSample> &samples);
};
