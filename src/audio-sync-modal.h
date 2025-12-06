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

#ifndef AUDIO_SYNC_MODAL_H
#define AUDIO_SYNC_MODAL_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <qtmetamacros.h>
#include <qobject.h>
#include "timeline-widget.h"
#include "audio-analyzer.h"
#include "recording-scanner.h"
#include "audio-analysis-worker.h"
#include "video-extractor.h"
#include "video-extraction-worker.h"

class AudioSyncModal : public QDialog {
	Q_OBJECT

public:
	explicit AudioSyncModal(const QString &filePath, QWidget *parent = nullptr);
	~AudioSyncModal() override;

	// Delete copy and move constructors/assignments
	AudioSyncModal(const AudioSyncModal &) = delete;
	AudioSyncModal &operator=(const AudioSyncModal &) = delete;
	AudioSyncModal(AudioSyncModal &&) = delete;
	AudioSyncModal &operator=(AudioSyncModal &&) = delete;

private slots:
	void onSpikePositionChanged(double timestamp);
	void onVideoFramePositionChanged(double timestamp);
	void onPrevFrameClicked();
	void onNextFrameClicked();
	void onZoomInClicked();
	void onZoomOutClicked();
	void onResetZoomClicked();
	// Worker slots
	void onAudioAnalyzed(const AudioSpike &spike, const QVector<AudioSample> &samples);
	void onAnalysisError(const QString &error);
	void onFramesExtracted(const QVector<VideoFrame> &frames, double fps);
	void onExtractionError(const QString &error);

private:
	void setupUI();
	void setupWorkerThreads();
	void loadRecording(const QString &filePath);
	void updateFrameDisplay();
	void updateSyncDisplay() const;
	void showSpinner(const QString &message);
	void hideSpinner();

	TimelineWidget *m_timelineWidget{nullptr};
	QPushButton *m_zoomInButton{nullptr};
	QPushButton *m_zoomOutButton{nullptr};
	QPushButton *m_resetZoomButton{nullptr};
	QLabel *m_frameLabel{nullptr};
	QPushButton *m_prevFrameButton{nullptr};
	QPushButton *m_nextFrameButton{nullptr};
	QLabel *m_frameInfoLabel{nullptr};
	QLabel *m_syncOffsetLabel{nullptr};
	QProgressBar *m_spinner{nullptr};
	QLabel *m_spinnerLabel{nullptr};

	// Data
	QString m_currentRecording{};
	AudioSpike m_currentSpike{};
	QVector<VideoFrame> m_frames{};
	int m_currentFrameIndex{-1};
	double m_videoFPS{30.0};

	// Worker threads
	QThread *m_audioThread{nullptr};
	AudioAnalysisWorker *m_audioWorker{nullptr};
	QThread *m_videoThread{nullptr};
	VideoExtractionWorker *m_videoWorker{nullptr};
};

#endif // AUDIO_SYNC_MODAL_H
