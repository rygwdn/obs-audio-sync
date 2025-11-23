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
#include <QPixmap>
#include <QVector>
#include <QPair>

struct VideoFrame {
	QPixmap pixmap;
	double timestamp;  // Time in seconds
	int frameNumber;   // Frame index
};

class VideoExtractor {
public:
	VideoExtractor();
	~VideoExtractor();

	// Open video file and prepare for extraction
	bool openFile(const QString &filePath);

	// Extract frame at specific timestamp
	VideoFrame extractFrameAt(double timestamp);

	// Extract all frames in time range
	QVector<VideoFrame> extractFrames(double startTime, double endTime);

	// Get video FPS
	double getFPS() const { return m_fps; }

	// Get video duration
	double getDuration() const { return m_duration; }

	// Close file and cleanup
	void close();

private:
	bool initializeFFmpeg();
	void cleanupFFmpeg();

	QString m_filePath;
	double m_fps;
	double m_duration;
	bool m_fileOpen;
};
