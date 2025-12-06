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

#include "audio-sync-panel.h"
#include "recording-scanner.h"
#include "audio-analyzer.h"
#include "video-extractor.h"
#include "timeline-widget.h"
#include "recording-scanner-worker.h"
#include "audio-analysis-worker.h"
#include "video-extraction-worker.h"
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <qwidget.h>
#include <qdockwidget.h>
#include <qboxlayout.h>
#include <qfont.h>
#include <qlistwidget.h>
#include <qabstractitemview.h>
#include <qpushbutton.h>
#include <qnamespace.h>
#include <qlist.h>
#include <qpixmap.h>
#include <qprogressbar.h>
#include <qthread.h>

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle("Audio Sync");

	// Create central widget for QDockWidget
	QWidget *centralWidget = new QWidget(this); // NOLINT(cppcoreguidelines-init-variables)
	setWidget(centralWidget);

	setupUI();
	refreshRecordings();
}

AudioSyncPanel::~AudioSyncPanel()
{
	// Stop and cleanup worker threads
	if (m_scanThread) {
		m_scanThread->quit();
		m_scanThread->wait();
		delete m_scanThread;
	}
	if (m_audioThread) {
		m_audioThread->quit();
		m_audioThread->wait();
		delete m_audioThread;
	}
	if (m_videoThread) {
		m_videoThread->quit();
		m_videoThread->wait();
		delete m_videoThread;
	}
}

void AudioSyncPanel::setupUI()
{
	QWidget *centralWidget = widget();
	m_layout = new QVBoxLayout(centralWidget);
	m_layout->setContentsMargins(10, 10, 10, 10);
	m_layout->setSpacing(10);

	// Title
	auto *titleLabel = new QLabel("Audio Sync", centralWidget);
	QFont titleFont = titleLabel->font();
	titleFont.setPointSize(14);
	titleFont.setBold(true);
	titleLabel->setFont(titleFont);
	m_layout->addWidget(titleLabel);

	// Recording list label
	auto *listLabel = new QLabel("Recordings (< 15s):", centralWidget);
	m_layout->addWidget(listLabel);

	// Recording list
	m_recordingList = new QListWidget(centralWidget);
	m_recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recordingList->setMaximumHeight(100);
	m_layout->addWidget(m_recordingList);

	// Refresh button
	m_refreshButton = new QPushButton("Refresh", centralWidget);
	m_layout->addWidget(m_refreshButton);

	// Timeline widget
	auto *timelineLabel = new QLabel("Timeline:", centralWidget);
	m_layout->addWidget(timelineLabel);
	m_timelineWidget = new TimelineWidget(centralWidget);
	m_timelineWidget->setVisible(false);
	m_layout->addWidget(m_timelineWidget);

	// Video frame display
	auto *frameLabel = new QLabel("Video Frame:", centralWidget);
	m_layout->addWidget(frameLabel);
	m_frameLabel = new QLabel(centralWidget);
	m_frameLabel->setMinimumHeight(200);
	m_frameLabel->setAlignment(Qt::AlignCenter);
	m_frameLabel->setStyleSheet("background-color: black; border: 1px solid gray;");
	m_frameLabel->setText("No frame loaded");
	m_frameLabel->setVisible(false);
	m_layout->addWidget(m_frameLabel);

	// Frame navigation
	auto *navLayout = new QHBoxLayout();
	m_prevFrameButton = new QPushButton("< Prev", centralWidget);
	m_prevFrameButton->setEnabled(false);
	m_prevFrameButton->setVisible(false);
	m_nextFrameButton = new QPushButton("Next >", centralWidget);
	m_nextFrameButton->setEnabled(false);
	m_nextFrameButton->setVisible(false);
	navLayout->addWidget(m_prevFrameButton);
	navLayout->addStretch();
	navLayout->addWidget(m_nextFrameButton);
	m_layout->addLayout(navLayout);

	// Frame info
	m_frameInfoLabel = new QLabel("", centralWidget);
	m_frameInfoLabel->setVisible(false);
	m_layout->addWidget(m_frameInfoLabel);

	// Sync offset display
	m_syncOffsetLabel = new QLabel("", centralWidget);
	QFont syncFont = m_syncOffsetLabel->font();
	syncFont.setPointSize(12);
	syncFont.setBold(true);
	m_syncOffsetLabel->setFont(syncFont);
	m_syncOffsetLabel->setVisible(false);
	m_layout->addWidget(m_syncOffsetLabel);

	// Status label
	m_statusLabel = new QLabel("Ready", centralWidget);
	m_statusLabel->setStyleSheet("color: gray;");
	m_layout->addWidget(m_statusLabel);

	// Spinner (initially hidden)
	m_spinner = new QProgressBar(centralWidget);
	m_spinner->setRange(0, 0); // Indeterminate progress
	m_spinner->setTextVisible(false);
	m_spinner->setVisible(false);
	m_layout->addWidget(m_spinner);

	m_spinnerLabel = new QLabel("", centralWidget);
	m_spinnerLabel->setStyleSheet("color: gray;");
	m_spinnerLabel->setVisible(false);
	m_layout->addWidget(m_spinnerLabel);

	// Connect signals
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this, &AudioSyncPanel::onRecordingSelected);
	connect(m_refreshButton, &QPushButton::clicked, this, &AudioSyncPanel::onRefreshClicked);
	connect(m_timelineWidget, &TimelineWidget::spikePositionChanged, this, &AudioSyncPanel::onSpikePositionChanged);
	connect(m_prevFrameButton, &QPushButton::clicked, this, &AudioSyncPanel::onPrevFrameClicked);
	connect(m_nextFrameButton, &QPushButton::clicked, this, &AudioSyncPanel::onNextFrameClicked);

	// Setup worker threads
	setupWorkerThreads();
}

void AudioSyncPanel::setupWorkerThreads()
{
	// Setup scanning worker thread
	m_scanThread = new QThread(this);
	m_scanWorker = new RecordingScannerWorker();
	m_scanWorker->moveToThread(m_scanThread);
	connect(m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);
	connect(m_scanWorker, &RecordingScannerWorker::recordingsScanned, this, &AudioSyncPanel::onRecordingsScanned);
	connect(m_scanWorker, &RecordingScannerWorker::scanError, this, &AudioSyncPanel::onScanError);
	m_scanThread->start();

	// Setup audio analysis worker thread
	m_audioThread = new QThread(this);
	m_audioWorker = new AudioAnalysisWorker();
	m_audioWorker->moveToThread(m_audioThread);
	connect(m_audioThread, &QThread::finished, m_audioWorker, &QObject::deleteLater);
	connect(m_audioWorker, &AudioAnalysisWorker::audioAnalyzed, this, &AudioSyncPanel::onAudioAnalyzed);
	connect(m_audioWorker, &AudioAnalysisWorker::analysisError, this, &AudioSyncPanel::onAnalysisError);
	m_audioThread->start();

	// Setup video extraction worker thread
	m_videoThread = new QThread(this);
	m_videoWorker = new VideoExtractionWorker();
	m_videoWorker->moveToThread(m_videoThread);
	connect(m_videoThread, &QThread::finished, m_videoWorker, &QObject::deleteLater);
	connect(m_videoWorker, &VideoExtractionWorker::framesExtracted, this, &AudioSyncPanel::onFramesExtracted);
	connect(m_videoWorker, &VideoExtractionWorker::extractionError, this, &AudioSyncPanel::onExtractionError);
	m_videoThread->start();
}

void AudioSyncPanel::showSpinner(const QString &message)
{
	m_spinner->setVisible(true);
	m_spinnerLabel->setText(message);
	m_spinnerLabel->setVisible(true);
	m_statusLabel->setText(message);
}

void AudioSyncPanel::hideSpinner()
{
	m_spinner->setVisible(false);
	m_spinnerLabel->setVisible(false);
}

void AudioSyncPanel::refreshRecordings()
{
	showSpinner("Scanning recordings...");
	m_recordingList->clear();
	m_refreshButton->setEnabled(false);

	// Start scanning in background thread
	QMetaObject::invokeMethod(m_scanWorker, "scanRecordings", Qt::QueuedConnection, Q_ARG(double, 15.0));
}

void AudioSyncPanel::onRecordingsScanned(const QList<RecordingInfo> &recordings)
{
	m_recordingList->clear();

	for (const RecordingInfo &recording : recordings) {
		QFileInfo fileInfo(recording.filePath);
		QString displayText = QString("%1 (%2s) - %3")
					      .arg(fileInfo.fileName())
					      .arg(recording.duration, 0, 'f', 2)
					      .arg(recording.modifiedTime.toString("yyyy-MM-dd hh:mm:ss"));

		QListWidgetItem *item = new QListWidgetItem(displayText, m_recordingList);
		item->setData(Qt::UserRole, recording.filePath);
		m_recordingList->addItem(item);
	}

	hideSpinner();
	m_statusLabel->setText(QString("Found %1 recordings").arg(m_recordingList->count()));
	m_refreshButton->setEnabled(true);
}

void AudioSyncPanel::onScanError(const QString &error)
{
	hideSpinner();
	m_statusLabel->setText("Scan failed");
	QMessageBox::warning(this, "Scan Error", error);
	m_refreshButton->setEnabled(true);
}

void AudioSyncPanel::onRecordingSelected(QListWidgetItem *item) // NOLINT(readability-convert-member-functions-to-static)
{
	if (item == nullptr) {
		return;
	}

	QString const FILE_PATH = item->data(Qt::UserRole).toString();
	loadRecording(FILE_PATH);
}

void AudioSyncPanel::loadRecording(const QString &filePath)
{
	m_currentRecording = filePath;
	showSpinner(QString("Analyzing: %1...").arg(QFileInfo(filePath).fileName()));

	// Disable recording list and buttons during analysis
	m_recordingList->setEnabled(false);

	// Start audio analysis in background thread
	QMetaObject::invokeMethod(m_audioWorker, "analyzeAudio", Qt::QueuedConnection, Q_ARG(QString, filePath));
}

void AudioSyncPanel::onAudioAnalyzed(const AudioSpike &spike, const QVector<AudioSample> &samples)
{
	m_currentSpike = spike;

	// Update timeline with audio data
	m_timelineWidget->setAudioSamples(samples);
	m_timelineWidget->setSpikePosition(m_currentSpike.timestamp);
	m_timelineWidget->setVisible(true);

	// Start video extraction in background thread
	showSpinner("Extracting frames...");
	QMetaObject::invokeMethod(m_videoWorker, "extractFrames", Qt::QueuedConnection,
				  Q_ARG(QString, m_currentRecording), Q_ARG(double, m_currentSpike.windowStart),
				  Q_ARG(double, m_currentSpike.windowEnd));
}

void AudioSyncPanel::onAnalysisError(const QString &error)
{
	hideSpinner();
	m_statusLabel->setText("Analysis failed");
	QMessageBox::warning(this, "Analysis Failed", error);
	m_recordingList->setEnabled(true);
}

void AudioSyncPanel::onFramesExtracted(const QVector<VideoFrame> &frames, double fps)
{
	m_frames = frames;
	m_videoFPS = fps;
	m_currentFrameIndex = 0;

	// Update timeline with FPS
	m_timelineWidget->setFPS(m_videoFPS);

	// Show UI components
	m_frameLabel->setVisible(true);
	m_prevFrameButton->setVisible(true);
	m_nextFrameButton->setVisible(true);
	m_frameInfoLabel->setVisible(true);
	m_syncOffsetLabel->setVisible(true);

	updateFrameDisplay();
	updateSyncDisplay();

	hideSpinner();
	m_statusLabel->setText(QString("Spike found at %1s").arg(m_currentSpike.timestamp, 0, 'f', 3));
	m_recordingList->setEnabled(true);
}

void AudioSyncPanel::onExtractionError(const QString &error)
{
	hideSpinner();
	m_statusLabel->setText("Extraction failed");
	QMessageBox::warning(this, "Video Error", error);
	m_recordingList->setEnabled(true);
}

void AudioSyncPanel::updateFrameDisplay()
{
	if (m_frames.isEmpty() || m_currentFrameIndex < 0 || m_currentFrameIndex >= m_frames.size()) {
		m_frameLabel->setText("No frame available");
		m_prevFrameButton->setEnabled(false);
		m_nextFrameButton->setEnabled(false);
		return;
	}

	const VideoFrame &frame = m_frames[m_currentFrameIndex];
	QPixmap scaledPixmap = frame.pixmap.scaled(m_frameLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	m_frameLabel->setPixmap(scaledPixmap);

	m_frameInfoLabel->setText(QString("Frame %1/%2 - Time: %3s")
					  .arg(m_currentFrameIndex + 1)
					  .arg(m_frames.size())
					  .arg(frame.timestamp, 0, 'f', 3));

	m_prevFrameButton->setEnabled(m_currentFrameIndex > 0);
	m_nextFrameButton->setEnabled(m_currentFrameIndex < m_frames.size() - 1);

	updateSyncDisplay();
}

void AudioSyncPanel::updateSyncDisplay() const
{
	if (m_frames.isEmpty() || m_currentFrameIndex < 0 || m_currentFrameIndex >= m_frames.size()) {
		return;
	}

	const VideoFrame &frame = m_frames[m_currentFrameIndex];
	double const TIME_DIFF = frame.timestamp - m_currentSpike.timestamp;
	double const FRAME_DIFF = TIME_DIFF * m_videoFPS;

	QString const SYNC_TEXT =
		QString("Sync Offset: %1ms (%2 frames)").arg(TIME_DIFF * 1000.0, 0, 'f', 1).arg(FRAME_DIFF, 0, 'f', 2);

	// Color coding
	QString color = "white";
	if (qAbs(FRAME_DIFF) < 1.0) {
		color = "green"; // In sync
	} else if (qAbs(FRAME_DIFF) < 3.0) {
		color = "yellow"; // Close
	} else {
		color = "red"; // Out of sync
	}

	m_syncOffsetLabel->setText(SYNC_TEXT);
	m_syncOffsetLabel->setStyleSheet(QString("color: %1;").arg(color));
}

void AudioSyncPanel::onSpikePositionChanged(double timestamp)
{
	m_currentSpike.timestamp = timestamp;
	updateSyncDisplay();
}

void AudioSyncPanel::onPrevFrameClicked()
{
	if (m_currentFrameIndex > 0) {
		m_currentFrameIndex--;
		updateFrameDisplay();
	}
}

void AudioSyncPanel::onNextFrameClicked()
{
	if (m_currentFrameIndex < m_frames.size() - 1) {
		m_currentFrameIndex++;
		updateFrameDisplay();
	}
}

void AudioSyncPanel::onRefreshClicked()
{
	refreshRecordings();
}
