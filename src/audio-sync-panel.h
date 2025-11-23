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

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "timeline-widget.h"
#include "video-extractor.h"
#include "audio-analyzer.h"

class AudioSyncPanel : public QWidget {
	Q_OBJECT

public:
	explicit AudioSyncPanel(QWidget *parent = nullptr);
	~AudioSyncPanel();

	void refreshRecordings();

private slots:
	void onRecordingSelected(QListWidgetItem *item);
	void onRefreshClicked();
	void onSpikePositionChanged(double timestamp);
	void onPrevFrameClicked();
	void onNextFrameClicked();

private:
	void setupUI();
	void scanRecordings();
	void loadRecording(const QString &filePath);
	void updateFrameDisplay();
	void updateSyncDisplay();

	QListWidget *m_recordingList;
	QLabel *m_statusLabel;
	QPushButton *m_refreshButton;
	QVBoxLayout *m_layout;

	// Analysis components
	TimelineWidget *m_timelineWidget;
	QLabel *m_frameLabel;
	QPushButton *m_prevFrameButton;
	QPushButton *m_nextFrameButton;
	QLabel *m_frameInfoLabel;
	QLabel *m_syncOffsetLabel;

	// Data
	QString m_currentRecording;
	AudioSpike m_currentSpike;
	QVector<VideoFrame> m_frames;
	int m_currentFrameIndex;
	double m_videoFPS;
	VideoExtractor *m_videoExtractor;
};
