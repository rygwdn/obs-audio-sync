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

#ifndef AUDIO_ANALYSIS_WORKER_H
#define AUDIO_ANALYSIS_WORKER_H

#include <QObject>
#include <QString>
#include <QVector>
#include "audio-analyzer.h"

class AudioAnalysisWorker : public QObject {
	Q_OBJECT

public:
	explicit AudioAnalysisWorker(QObject *parent = nullptr);
	~AudioAnalysisWorker() override = default;

	// Delete copy and move constructors/assignments
	AudioAnalysisWorker(const AudioAnalysisWorker &) = delete;
	AudioAnalysisWorker &operator=(const AudioAnalysisWorker &) = delete;
	AudioAnalysisWorker(AudioAnalysisWorker &&) = delete;
	AudioAnalysisWorker &operator=(AudioAnalysisWorker &&) = delete;

public slots:
	void analyzeAudio(const QString &filePath);

signals:
	void audioAnalyzed(const AudioSpike &spike, const QVector<AudioSample> &samples);
	void analysisError(const QString &error);
};

#endif // AUDIO_ANALYSIS_WORKER_H
