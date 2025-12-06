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

#ifndef AUDIO_SYNC_PANEL_H
#define AUDIO_SYNC_PANEL_H

#include <QWidget>
#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <qtmetamacros.h>
#include <qobject.h>
#include "recording-scanner.h"
#include "recording-scanner-worker.h"
#include "audio-sync-modal.h"

class AudioSyncPanel : public QDockWidget {
	Q_OBJECT

public:
	explicit AudioSyncPanel(QWidget *parent = nullptr);
	~AudioSyncPanel() override;

	// Delete copy and move constructors/assignments
	AudioSyncPanel(const AudioSyncPanel &) = delete;
	AudioSyncPanel &operator=(const AudioSyncPanel &) = delete;
	AudioSyncPanel(AudioSyncPanel &&) = delete;
	AudioSyncPanel &operator=(AudioSyncPanel &&) = delete;

public slots:
	void refreshRecordings();
	void scheduleDelayedRefresh();

private slots:
	void onRecordingSelected(QListWidgetItem *item);
	void onRefreshClicked();
	void onStartSyncClicked();
	// Worker slots
	void onRecordingsScanned(const QList<RecordingInfo> &recordings);
	void onScanError(const QString &error);

private:
	void setupUI();
	void setupWorkerThreads();
	void showSpinner(const QString &message);
	void hideSpinner();

	QListWidget *m_recordingList{nullptr};
	QLabel *m_statusLabel{nullptr};
	QPushButton *m_refreshButton{nullptr};
	QPushButton *m_startSyncButton{nullptr};
	QVBoxLayout *m_layout{nullptr};
	QProgressBar *m_spinner{nullptr};
	QLabel *m_spinnerLabel{nullptr};

	// Worker threads
	QThread *m_scanThread{nullptr};
	RecordingScannerWorker *m_scanWorker{nullptr};

	// Timer for delayed refresh after recording events
	QTimer *m_refreshTimer{nullptr};
};

#endif // AUDIO_SYNC_PANEL_H
