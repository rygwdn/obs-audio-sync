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

#ifndef RECORDING_SCANNER_WORKER_H
#define RECORDING_SCANNER_WORKER_H

#include <QObject>
#include <QThread>
#include <QList>
#include "recording-scanner.h"

class RecordingScannerWorker : public QObject {
	Q_OBJECT

public:
	explicit RecordingScannerWorker(QObject *parent = nullptr);
	~RecordingScannerWorker() override = default;

	// Delete copy and move constructors/assignments
	RecordingScannerWorker(const RecordingScannerWorker &) = delete;
	RecordingScannerWorker &operator=(const RecordingScannerWorker &) = delete;
	RecordingScannerWorker(RecordingScannerWorker &&) = delete;
	RecordingScannerWorker &operator=(RecordingScannerWorker &&) = delete;

public slots:
	void scanRecordings(double maxDurationSeconds);

signals:
	void recordingsScanned(const QList<RecordingInfo> &recordings);
	void scanError(const QString &error);
};

#endif // RECORDING_SCANNER_WORKER_H
