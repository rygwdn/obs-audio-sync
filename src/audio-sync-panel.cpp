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

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QDockWidget(parent), m_videoExtractor(new VideoExtractor())
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
	delete m_videoExtractor;
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
	auto const *navLayout = new QHBoxLayout();
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

	// Connect signals
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this, &AudioSyncPanel::onRecordingSelected);
	connect(m_refreshButton, &QPushButton::clicked, this, &AudioSyncPanel::onRefreshClicked);
	connect(m_timelineWidget, &TimelineWidget::spikePositionChanged, this, &AudioSyncPanel::onSpikePositionChanged);
	connect(m_prevFrameButton, &QPushButton::clicked, this, &AudioSyncPanel::onPrevFrameClicked);
	connect(m_nextFrameButton, &QPushButton::clicked, this, &AudioSyncPanel::onNextFrameClicked);
}

void AudioSyncPanel::refreshRecordings()
{
	m_statusLabel->setText("Scanning recordings...");
	m_recordingList->clear();

	scanRecordings();

	m_statusLabel->setText(QString("Found %1 recordings").arg(m_recordingList->count()));
}

void AudioSyncPanel::scanRecordings() // NOLINT(readability-convert-member-functions-to-static)
{
	RecordingScanner const SCANNER;
	QList<RecordingInfo> recordings = scanner.scanRecordings(15.0); // 15 second threshold

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
	m_statusLabel->setText(QString("Analyzing: %1...").arg(QFileInfo(filePath).fileName()));

	// Analyze audio and find spike
	if (!AudioAnalyzer::analyzeFile(filePath, m_currentSpike)) {
		m_statusLabel->setText("Failed to analyze audio");
		QMessageBox::warning(this, "Analysis Failed", "Could not analyze audio from recording.");
		return;
	}

	// Get audio samples for timeline
	QVector<AudioSample> samples =
		AudioAnalyzer::getAudioSamples(filePath, m_currentSpike.windowStart, m_currentSpike.windowEnd);

	// Open video file
	if (!m_videoExtractor->openFile(filePath)) {
		m_statusLabel->setText("Failed to open video");
		QMessageBox::warning(this, "Video Error", "Could not open video from recording.");
		return;
	}

	m_videoFPS = m_videoExtractor->getFPS();
	m_timelineWidget->setFPS(m_videoFPS);
	m_timelineWidget->setAudioSamples(samples);
	m_timelineWidget->setSpikePosition(m_currentSpike.timestamp);
	m_timelineWidget->setVisible(true);

	// Extract frames
	m_statusLabel->setText("Extracting frames...");
	m_frames = m_videoExtractor->extractFrames(m_currentSpike.windowStart, m_currentSpike.windowEnd);
	m_currentFrameIndex = 0;

	// Show UI components
	m_frameLabel->setVisible(true);
	m_prevFrameButton->setVisible(true);
	m_nextFrameButton->setVisible(true);
	m_frameInfoLabel->setVisible(true);
	m_syncOffsetLabel->setVisible(true);

	updateFrameDisplay();
	updateSyncDisplay();

	m_statusLabel->setText(QString("Spike found at %1s").arg(m_currentSpike.timestamp, 0, 'f', 3));
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
