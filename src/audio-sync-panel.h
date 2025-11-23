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
#include <qtmetamacros.h>
#include <qobject.h>
#include "timeline-widget.h"
#include "video-extractor.h"
#include "audio-analyzer.h"

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

	void refreshRecordings();

private slots:
	void onRecordingSelected(QListWidgetItem *item);
	void onRefreshClicked();
	void onSpikePositionChanged(double timestamp);
	void onPrevFrameClicked();
	void onNextFrameClicked();

	void setupUI();
	void scanRecordings();
	void loadRecording(const QString &filePath);
	void updateFrameDisplay();
	void updateSyncDisplay() const;

	QListWidget *m_recordingList{nullptr};
	QLabel *m_statusLabel{nullptr};
	QPushButton *m_refreshButton{nullptr};
	QVBoxLayout *m_layout{nullptr};

	// Analysis components
	TimelineWidget *m_timelineWidget{nullptr};
	QLabel *m_frameLabel{nullptr};
	QPushButton *m_prevFrameButton{nullptr};
	QPushButton *m_nextFrameButton{nullptr};
	QLabel *m_frameInfoLabel{nullptr};
	QLabel *m_syncOffsetLabel{nullptr};

	// Data
	QString m_currentRecording{};
	AudioSpike m_currentSpike{};
	QVector<VideoFrame> m_frames{};
	int m_currentFrameIndex{-1};
	double m_videoFPS{30.0};
	VideoExtractor *m_videoExtractor{nullptr};
};

#endif // AUDIO_SYNC_PANEL_H
