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

#ifndef RECORDING_SCANNER_H
#define RECORDING_SCANNER_H

#include <QString>
#include <QDateTime>
#include <QList>

struct RecordingInfo {
	QString filePath{};
	double duration{}; // in seconds
	QDateTime modifiedTime;
};

class RecordingScanner {
public:
	RecordingScanner();
	~RecordingScanner() = default;

	// Delete copy and move constructors/assignments
	RecordingScanner(const RecordingScanner &) = delete;
	RecordingScanner &operator=(const RecordingScanner &) = delete;
	RecordingScanner(RecordingScanner &&) = delete;
	RecordingScanner &operator=(RecordingScanner &&) = delete;

	static QList<RecordingInfo> scanRecordings(double maxDurationSeconds = 15.0);
	static bool isValidVideoFile(const QString &filePath); // Public for testing

private:
	static QString getRecordingPath();
	static double getFileDuration(const QString &filePath);
};

#endif // RECORDING_SCANNER_H
