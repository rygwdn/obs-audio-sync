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

#include "recording-scanner.h"
#include <obs-frontend-api.h>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QDebug>
#include <algorithm>

RecordingScanner::RecordingScanner() {}

RecordingScanner::~RecordingScanner() {}

QString RecordingScanner::getRecordingPath()
{
	// Try to get recording path from OBS frontend API
	const char *recordingPath = obs_frontend_get_current_record_output_path();
	if (recordingPath && strlen(recordingPath) > 0) {
		QFileInfo pathInfo(recordingPath);
		return pathInfo.absolutePath();
	}

	// Fallback: try common OBS recording locations
	QStringList commonPaths;
#ifdef _WIN32
	commonPaths << QDir::homePath() + "/Videos";
	commonPaths << QDir::homePath() + "/Documents";
#elif __APPLE__
	commonPaths << QDir::homePath() + "/Movies";
	commonPaths << QDir::homePath() + "/Desktop";
#else
	commonPaths << QDir::homePath() + "/Videos";
	commonPaths << QDir::homePath() + "/Documents";
#endif

	for (const QString &path : commonPaths) {
		QDir dir(path);
		if (dir.exists()) {
			return path;
		}
	}

	return QDir::homePath();
}

bool RecordingScanner::isValidVideoFile(const QString &filePath)
{
	QString suffix = QFileInfo(filePath).suffix().toLower();
	QStringList videoExtensions = {"mp4", "mkv", "flv", "mov", "avi", "webm"};
	return videoExtensions.contains(suffix);
}

double RecordingScanner::getFileDuration(const QString &filePath)
{
	// For Phase 1, we'll use a simple file size-based estimate
	// In Phase 2, we'll use FFmpeg to get accurate duration
	QFileInfo fileInfo(filePath);
	if (!fileInfo.exists()) {
		return 0.0;
	}

	// Rough estimate: assume ~1MB per second for typical recording
	// This is a placeholder - will be replaced with FFmpeg in Phase 2
	qint64 fileSizeBytes = fileInfo.size();
	double estimatedDuration = fileSizeBytes / (1024.0 * 1024.0); // MB

	// Cap at reasonable maximum for our use case
	if (estimatedDuration > 60.0) {
		estimatedDuration = 60.0;
	}

	return estimatedDuration;
}

QList<RecordingInfo> RecordingScanner::scanRecordings(double maxDurationSeconds)
{
	QList<RecordingInfo> recordings;
	QString recordingPath = getRecordingPath();

	if (recordingPath.isEmpty()) {
		qWarning() << "No recording path found";
		return recordings;
	}

	QDir dir(recordingPath);
	if (!dir.exists()) {
		qWarning() << "Recording directory does not exist:" << recordingPath;
		return recordings;
	}

	// Scan for video files
	QDirIterator it(recordingPath, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString filePath = it.next();
		QFileInfo fileInfo(filePath);

		if (!fileInfo.isFile()) {
			continue;
		}

		if (!isValidVideoFile(filePath)) {
			continue;
		}

		double duration = getFileDuration(filePath);
		if (duration > 0.0 && duration <= maxDurationSeconds) {
			RecordingInfo info;
			info.filePath = filePath;
			info.duration = duration;
			info.modifiedTime = fileInfo.lastModified();
			recordings.append(info);
		}
	}

	// Sort by modification time (newest first)
	std::sort(recordings.begin(), recordings.end(),
		  [](const RecordingInfo &a, const RecordingInfo &b) {
			  return a.modifiedTime > b.modifiedTime;
		  });

	return recordings;
}
