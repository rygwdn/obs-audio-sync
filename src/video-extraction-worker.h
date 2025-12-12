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

#ifndef VIDEO_EXTRACTION_WORKER_H
#define VIDEO_EXTRACTION_WORKER_H

#include <QObject>
#include <QString>
#include <QVector>
#include "video-extractor.h"

class VideoExtractionWorker : public QObject {
	Q_OBJECT

public:
	explicit VideoExtractionWorker(QObject *parent = nullptr);
	~VideoExtractionWorker() override;

	// Delete copy and move constructors/assignments
	VideoExtractionWorker(const VideoExtractionWorker &) = delete;
	VideoExtractionWorker &operator=(const VideoExtractionWorker &) = delete;
	VideoExtractionWorker(VideoExtractionWorker &&) = delete;
	VideoExtractionWorker &operator=(VideoExtractionWorker &&) = delete;

public slots:
	void extractFrames(const QString &filePath, double startTime, double endTime);
	void extractFramesIncremental(const QString &filePath, double startTime, double endTime, double priorityCenter);

signals:
	void framesExtracted(const QVector<VideoFrame> &frames, double fps);
	void extractionError(const QString &error);

private:
	VideoExtractor *m_videoExtractor{nullptr};
};

#endif // VIDEO_EXTRACTION_WORKER_H
