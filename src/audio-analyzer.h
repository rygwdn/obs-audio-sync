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

#ifndef AUDIO_ANALYZER_H
#define AUDIO_ANALYZER_H

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

	// Delete copy and move constructors/assignments
	AudioAnalyzer(const AudioAnalyzer &) = delete;
	AudioAnalyzer &operator=(const AudioAnalyzer &) = delete;
	AudioAnalyzer(AudioAnalyzer &&) = delete;
	AudioAnalyzer &operator=(AudioAnalyzer &&) = delete;

	// Analyze audio file and find largest spike
	static bool analyzeFile(const QString &filePath, AudioSpike &spike);

	// Get audio samples for visualization (4-second window)
	static QVector<AudioSample> getAudioSamples(const QString &filePath, double startTime, double endTime);

	// Get file duration in seconds
	static double getFileDuration(const QString &filePath);

private:
	static bool initializeFFmpeg();
	void cleanupFFmpeg();

	// Extract audio samples from file
	static QVector<AudioSample> extractAudioSamples(const QString &filePath);

	// Find largest spike in audio samples
	static AudioSpike findLargestSpike(const QVector<AudioSample> &samples);
};

#endif // AUDIO_ANALYZER_H
