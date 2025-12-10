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

#include "video-extraction-worker.h"
#include "video-extractor.h"
#include <QDebug>

VideoExtractionWorker::VideoExtractionWorker(QObject *parent) : QObject(parent), m_videoExtractor(new VideoExtractor())
{
}

VideoExtractionWorker::~VideoExtractionWorker()
{
	delete m_videoExtractor;
}

void VideoExtractionWorker::extractFrames(const QString &filePath, double startTime, double endTime)
{
	try {
		if (!m_videoExtractor->openFile(filePath)) {
			emit extractionError("Could not open video from recording.");
			return;
		}

		double fps = m_videoExtractor->getFPS();
		QVector<VideoFrame> frames = m_videoExtractor->extractFrames(startTime, endTime);

		if (frames.isEmpty()) {
			qWarning() << "VideoExtractionWorker::extractFrames: No frames extracted";
			emit extractionError(QString("No frames extracted from video (time range: %1-%2s)")
						     .arg(startTime, 0, 'f', 3)
						     .arg(endTime, 0, 'f', 3));
			return;
		}

		emit framesExtracted(frames, fps);
	} catch (const std::exception &e) {
		qWarning() << "VideoExtractionWorker::extractFrames: Exception:" << e.what();
		emit extractionError(QString("Error extracting frames: %1").arg(e.what()));
	} catch (...) {
		qWarning() << "VideoExtractionWorker::extractFrames: Unknown exception";
		emit extractionError("Unknown error extracting frames");
	}
}

void VideoExtractionWorker::extractFramesIncremental(const QString &filePath, double startTime, double endTime,
						      double priorityCenter)
{
	try {
		if (!m_videoExtractor->openFile(filePath)) {
			emit extractionError("Could not open video from recording.");
			return;
		}

		double fps = m_videoExtractor->getFPS();

		// Use optimized extraction: single-pass decode, then parallel RGB conversion
		// Priority zone (1 second around cursor) is converted first, then rest in parallel
		QVector<VideoFrame> allFrames = m_videoExtractor->extractFramesOptimized(startTime, endTime,
											  priorityCenter);

		// Split frames into priority zone and remaining for incremental emission
		const double PRIORITY_WINDOW = 1.0;
		QVector<VideoFrame> priorityFrames;
		QVector<VideoFrame> remainingFrames;

		for (const VideoFrame &frame : allFrames) {
			double distance = qAbs(frame.timestamp - priorityCenter);
			if (distance <= PRIORITY_WINDOW) {
				priorityFrames.append(frame);
			} else {
				remainingFrames.append(frame);
			}
		}

		// Sort priority frames by timestamp
		std::sort(priorityFrames.begin(), priorityFrames.end(),
			  [](const VideoFrame &a, const VideoFrame &b) { return a.timestamp < b.timestamp; });

		// Emit priority frames first
		if (!priorityFrames.isEmpty()) {
			emit framesExtractedIncremental(priorityFrames, fps, true);
		}

		// Emit remaining frames
		if (!remainingFrames.isEmpty()) {
			emit framesExtractedIncremental(remainingFrames, fps, false);
		}

		if (allFrames.isEmpty()) {
			qWarning() << "VideoExtractionWorker::extractFramesIncremental: No frames extracted";
			emit extractionError(QString("No frames extracted from video (time range: %1-%2s)")
						     .arg(startTime, 0, 'f', 3)
						     .arg(endTime, 0, 'f', 3));
			return;
		}

		emit framesExtracted(allFrames, fps);
	} catch (const std::exception &e) {
		qWarning() << "VideoExtractionWorker::extractFramesIncremental: Exception:" << e.what();
		emit extractionError(QString("Error extracting frames: %1").arg(e.what()));
	} catch (...) {
		qWarning() << "VideoExtractionWorker::extractFramesIncremental: Unknown exception";
		emit extractionError("Unknown error extracting frames");
	}
}
