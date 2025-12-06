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

#include "audio-analysis-worker.h"
#include "audio-analyzer.h"
#include <QDebug>

AudioAnalysisWorker::AudioAnalysisWorker(QObject *parent) : QObject(parent)
{
}

void AudioAnalysisWorker::analyzeAudio(const QString &filePath)
{
	try {
		AudioSpike spike;
		if (!AudioAnalyzer::analyzeFile(filePath, spike)) {
			emit analysisError("Failed to analyze audio from recording.");
			return;
		}

		// Get audio samples for timeline
		QVector<AudioSample> samples =
			AudioAnalyzer::getAudioSamples(filePath, spike.windowStart, spike.windowEnd);

		emit audioAnalyzed(spike, samples);
	} catch (const std::exception &e) {
		emit analysisError(QString("Error analyzing audio: %1").arg(e.what()));
	} catch (...) {
		emit analysisError("Unknown error analyzing audio");
	}
}
