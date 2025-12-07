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

#include "audio-sync-modal.h"
#include "audio-analyzer.h"
#include "video-extractor.h"
#include "timeline-widget.h"
#include "audio-analysis-worker.h"
#include "video-extraction-worker.h"
#include "source-offset-manager.h"
#include <QFileInfo>
#include <QMessageBox>
#include <qwidget.h>
#include <qdialog.h>
#include <qboxlayout.h>
#include <qfont.h>
#include <qpushbutton.h>
#include <qnamespace.h>
#include <qpixmap.h>
#include <qprogressbar.h>
#include <qthread.h>
#include <qlistwidget.h>
#include <qabstractitemview.h>
#include <qlabel.h>

AudioSyncModal::AudioSyncModal(const QString &filePath, QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Audio Sync - " + QFileInfo(filePath).fileName());
	setModal(true);
	setMinimumSize(900, 700);

	m_currentRecording = filePath;
	m_calculatedOffsetMs = 0.0;

	// Create source offset manager
	m_sourceOffsetManager = new SourceOffsetManager(this);

	setupUI();
	setupWorkerThreads();
	setupSourceSelection();
	loadRecording(filePath);
}

AudioSyncModal::~AudioSyncModal()
{
	// Disconnect all signal/slot connections first to prevent any objects from
	// emitting signals to destroyed slots. This is critical to prevent crashes
	// during shutdown. We disconnect in this order:
	// 1. Worker signals (most critical - cross-thread)
	// 2. UI widget signals (safety measure)
	// 3. Keep thread->worker deleteLater connections for proper cleanup

	// Disconnect worker signals (critical - these are cross-thread)
	if (m_audioWorker) {
		disconnect(m_audioWorker, nullptr, this, nullptr);
	}
	if (m_videoWorker) {
		disconnect(m_videoWorker, nullptr, this, nullptr);
	}

	// Disconnect UI widget signals (safety measure - prevents signals during destruction)
	if (m_timelineWidget) {
		disconnect(m_timelineWidget, nullptr, this, nullptr);
	}
	if (m_prevFrameButton) {
		disconnect(m_prevFrameButton, nullptr, this, nullptr);
	}
	if (m_nextFrameButton) {
		disconnect(m_nextFrameButton, nullptr, this, nullptr);
	}
	if (m_audioSourcesList) {
		disconnect(m_audioSourcesList, nullptr, this, nullptr);
	}
	if (m_videoSourcesList) {
		disconnect(m_videoSourcesList, nullptr, this, nullptr);
	}
	if (m_applyOffsetButton) {
		disconnect(m_applyOffsetButton, nullptr, this, nullptr);
	}

	// Stop and cleanup worker threads
	// Workers will be automatically deleted via deleteLater when threads finish
	if (m_audioThread) {
		m_audioThread->quit();
		m_audioThread->wait();
		delete m_audioThread;
		m_audioThread = nullptr;
	}
	if (m_videoThread) {
		m_videoThread->quit();
		m_videoThread->wait();
		delete m_videoThread;
		m_videoThread = nullptr;
	}
}

void AudioSyncModal::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(10, 10, 10, 10);
	mainLayout->setSpacing(10);

	// Title
	auto *titleLabel = new QLabel("Audio Sync", this);
	QFont titleFont = titleLabel->font();
	titleFont.setPointSize(14);
	titleFont.setBold(true);
	titleLabel->setFont(titleFont);
	mainLayout->addWidget(titleLabel);

	// Timeline widget
	auto *timelineLabel = new QLabel("Timeline:", this);
	mainLayout->addWidget(timelineLabel);
	m_timelineWidget = new TimelineWidget(this);
	m_timelineWidget->setVisible(false);
	mainLayout->addWidget(m_timelineWidget);

	// Zoom and frame navigation controls
	auto *zoomLayout = new QHBoxLayout();
	m_zoomInButton = new QPushButton("Zoom In", this);
	m_zoomInButton->setVisible(false);
	m_zoomOutButton = new QPushButton("Zoom Out", this);
	m_zoomOutButton->setVisible(false);
	m_resetZoomButton = new QPushButton("Reset Zoom", this);
	m_resetZoomButton->setVisible(false);
	m_prevFrameButton = new QPushButton("< Prev", this);
	m_prevFrameButton->setEnabled(false);
	m_prevFrameButton->setVisible(false);
	m_nextFrameButton = new QPushButton("Next >", this);
	m_nextFrameButton->setEnabled(false);
	m_nextFrameButton->setVisible(false);
	zoomLayout->addWidget(m_zoomInButton);
	zoomLayout->addWidget(m_zoomOutButton);
	zoomLayout->addWidget(m_resetZoomButton);
	zoomLayout->addStretch();
	zoomLayout->addWidget(m_prevFrameButton);
	zoomLayout->addWidget(m_nextFrameButton);
	mainLayout->addLayout(zoomLayout);

	// Video frame display
	auto *frameLabel = new QLabel("Video Frame:", this);
	mainLayout->addWidget(frameLabel);
	m_frameLabel = new QLabel(this);
	m_frameLabel->setMinimumHeight(200);
	m_frameLabel->setMaximumHeight(400); // Fixed maximum to prevent growth
	m_frameLabel->setMaximumWidth(800);  // Fixed maximum width
	m_frameLabel->setAlignment(Qt::AlignCenter);
	m_frameLabel->setStyleSheet("background-color: black; border: 1px solid gray;");
	m_frameLabel->setText("No frame loaded");
	m_frameLabel->setScaledContents(false); // Important: don't scale contents automatically
	m_frameLabel->setVisible(false);
	mainLayout->addWidget(m_frameLabel);

	// Frame info
	m_frameInfoLabel = new QLabel("", this);
	m_frameInfoLabel->setVisible(false);
	mainLayout->addWidget(m_frameInfoLabel);

	// Sync offset display
	m_syncOffsetLabel = new QLabel("", this);
	QFont syncFont = m_syncOffsetLabel->font();
	syncFont.setPointSize(12);
	syncFont.setBold(true);
	m_syncOffsetLabel->setFont(syncFont);
	m_syncOffsetLabel->setVisible(false);
	mainLayout->addWidget(m_syncOffsetLabel);

	// Source selection section
	auto *sourceSectionLabel = new QLabel("Source Selection:", this);
	QFont sectionFont = sourceSectionLabel->font();
	sectionFont.setBold(true);
	sourceSectionLabel->setFont(sectionFont);
	mainLayout->addWidget(sourceSectionLabel);

	// Audio sources to sync
	auto *audioLabel = new QLabel("Audio Sources to Sync:", this);
	mainLayout->addWidget(audioLabel);
	m_audioSourcesList = new QListWidget(this);
	m_audioSourcesList->setSelectionMode(QAbstractItemView::NoSelection);
	m_audioSourcesList->setMaximumHeight(150);
	m_audioSourcesList->setToolTip("Select audio sources to sync to match the recording");
	mainLayout->addWidget(m_audioSourcesList);

	// Video sources to sync
	auto *videoLabel = new QLabel("Video Sources to Sync:", this);
	mainLayout->addWidget(videoLabel);
	m_videoSourcesList = new QListWidget(this);
	m_videoSourcesList->setSelectionMode(QAbstractItemView::NoSelection);
	m_videoSourcesList->setMaximumHeight(150);
	m_videoSourcesList->setToolTip("Select video sources to sync to match the recording");
	mainLayout->addWidget(m_videoSourcesList);

	// Apply offset button
	m_applyOffsetButton = new QPushButton("Apply Offset", this);
	m_applyOffsetButton->setToolTip("Apply the calculated sync offset as a delta to the selected source");
	m_applyOffsetButton->setEnabled(false);
	mainLayout->addWidget(m_applyOffsetButton);

	// Spinner (initially hidden)
	m_spinner = new QProgressBar(this);
	m_spinner->setRange(0, 0); // Indeterminate progress
	m_spinner->setTextVisible(false);
	m_spinner->setVisible(false);
	mainLayout->addWidget(m_spinner);

	m_spinnerLabel = new QLabel("", this);
	m_spinnerLabel->setStyleSheet("color: gray;");
	m_spinnerLabel->setVisible(false);
	mainLayout->addWidget(m_spinnerLabel);

	// Close button
	auto *buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	auto *closeButton = new QPushButton("Close", this);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
	buttonLayout->addWidget(closeButton);
	mainLayout->addLayout(buttonLayout);

	// Connect signals
	connect(m_timelineWidget, &TimelineWidget::spikePositionChanged, this, &AudioSyncModal::onSpikePositionChanged);
	connect(m_timelineWidget, &TimelineWidget::videoFramePositionChanged, this,
		&AudioSyncModal::onVideoFramePositionChanged);
	connect(m_prevFrameButton, &QPushButton::clicked, this, &AudioSyncModal::onPrevFrameClicked);
	connect(m_nextFrameButton, &QPushButton::clicked, this, &AudioSyncModal::onNextFrameClicked);
	connect(m_zoomInButton, &QPushButton::clicked, this, &AudioSyncModal::onZoomInClicked);
	connect(m_zoomOutButton, &QPushButton::clicked, this, &AudioSyncModal::onZoomOutClicked);
	connect(m_resetZoomButton, &QPushButton::clicked, this, &AudioSyncModal::onResetZoomClicked);
	connect(m_audioSourcesList, &QListWidget::itemChanged, this, &AudioSyncModal::onSourceSelectionChanged);
	connect(m_videoSourcesList, &QListWidget::itemChanged, this, &AudioSyncModal::onSourceSelectionChanged);
	connect(m_applyOffsetButton, &QPushButton::clicked, this, &AudioSyncModal::onApplyOffsetClicked);
}

void AudioSyncModal::setupWorkerThreads()
{
	// Setup audio analysis worker thread
	m_audioThread = new QThread(this);
	m_audioWorker = new AudioAnalysisWorker();
	m_audioWorker->moveToThread(m_audioThread);
	connect(m_audioThread, &QThread::finished, m_audioWorker, &QObject::deleteLater);
	connect(m_audioWorker, &AudioAnalysisWorker::audioAnalyzed, this, &AudioSyncModal::onAudioAnalyzed);
	connect(m_audioWorker, &AudioAnalysisWorker::analysisError, this, &AudioSyncModal::onAnalysisError);
	m_audioThread->start();

	// Setup video extraction worker thread
	m_videoThread = new QThread(this);
	m_videoWorker = new VideoExtractionWorker();
	m_videoWorker->moveToThread(m_videoThread);
	connect(m_videoThread, &QThread::finished, m_videoWorker, &QObject::deleteLater);
	connect(m_videoWorker, &VideoExtractionWorker::framesExtracted, this, &AudioSyncModal::onFramesExtracted);
	connect(m_videoWorker, &VideoExtractionWorker::extractionError, this, &AudioSyncModal::onExtractionError);
	m_videoThread->start();
}

void AudioSyncModal::showSpinner(const QString &message)
{
	m_spinner->setVisible(true);
	m_spinnerLabel->setText(message);
	m_spinnerLabel->setVisible(true);
}

void AudioSyncModal::hideSpinner()
{
	m_spinner->setVisible(false);
	m_spinnerLabel->setVisible(false);
}

void AudioSyncModal::loadRecording(const QString &filePath)
{
	m_currentRecording = filePath;
	showSpinner(QString("Analyzing: %1...").arg(QFileInfo(filePath).fileName()));

	// Start audio analysis in background thread
	QMetaObject::invokeMethod(m_audioWorker, "analyzeAudio", Qt::QueuedConnection, Q_ARG(QString, filePath));
}

void AudioSyncModal::onAudioAnalyzed(const AudioSpike &spike, const QVector<AudioSample> &samples)
{
	m_currentSpike = spike;

	// Update timeline with audio data
	m_timelineWidget->setAudioSamples(samples);
	m_timelineWidget->setSpikePosition(m_currentSpike.timestamp);
	m_timelineWidget->setVisible(true);
	m_zoomInButton->setVisible(true);
	m_zoomOutButton->setVisible(true);
	m_resetZoomButton->setVisible(true);

	// Start video extraction in background thread
	showSpinner("Extracting frames...");
	QMetaObject::invokeMethod(m_videoWorker, "extractFrames", Qt::QueuedConnection,
				  Q_ARG(QString, m_currentRecording), Q_ARG(double, m_currentSpike.windowStart),
				  Q_ARG(double, m_currentSpike.windowEnd));
}

void AudioSyncModal::onAnalysisError(const QString &error)
{
	hideSpinner();
	QMessageBox::warning(this, "Analysis Failed", error);
}

void AudioSyncModal::onFramesExtracted(const QVector<VideoFrame> &frames, double fps)
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

	// Update timeline with video frame data
	QVector<double> frameTimestamps;
	QVector<double> frameDifferences;
	for (const VideoFrame &frame : frames) {
		frameTimestamps.append(frame.timestamp);
		frameDifferences.append(frame.differenceFromPrevious);
	}
	m_timelineWidget->setVideoFrames(frameTimestamps);
	m_timelineWidget->setFrameDifferences(frameDifferences);

	// Find the frame closest to the audio spike
	double minDiff = 1e10;
	int bestFrameIndex = 0;
	for (int i = 0; i < frames.size(); i++) {
		double diff = qAbs(frames[i].timestamp - m_currentSpike.timestamp);
		if (diff < minDiff) {
			minDiff = diff;
			bestFrameIndex = i;
		}
	}
	m_currentFrameIndex = bestFrameIndex;

	// Set initial video frame position to the closest frame (which will be close to the spike)
	updateFrameDisplay();
	updateSyncDisplay();

	hideSpinner();
}

void AudioSyncModal::onExtractionError(const QString &error)
{
	hideSpinner();
	QMessageBox::warning(this, "Video Error", error);
}

void AudioSyncModal::updateFrameDisplay()
{
	if (m_frames.isEmpty() || m_currentFrameIndex < 0 || m_currentFrameIndex >= m_frames.size()) {
		m_frameLabel->setText("No frame available");
		m_prevFrameButton->setEnabled(false);
		m_nextFrameButton->setEnabled(false);
		return;
	}

	const VideoFrame &frame = m_frames[m_currentFrameIndex];
	// Scale to fit within maximum label bounds while maintaining aspect ratio
	QSize maxSize = m_frameLabel->maximumSize();
	if (maxSize.width() <= 0 || maxSize.height() <= 0) {
		maxSize = QSize(800, 400); // Fallback
	}
	// Scale to fit (not fill) - this ensures the entire image is visible
	QPixmap scaledPixmap = frame.pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	m_frameLabel->setPixmap(scaledPixmap);

	// Update timeline with current video frame position
	m_timelineWidget->setVideoFramePosition(frame.timestamp);

	m_frameInfoLabel->setText(QString("Frame %1/%2 - Time: %3s")
					  .arg(m_currentFrameIndex + 1)
					  .arg(m_frames.size())
					  .arg(frame.timestamp, 0, 'f', 3));

	m_prevFrameButton->setEnabled(m_currentFrameIndex > 0);
	m_nextFrameButton->setEnabled(m_currentFrameIndex < m_frames.size() - 1);

	updateSyncDisplay();
}

void AudioSyncModal::updateSyncDisplay()
{
	if (m_frames.isEmpty() || m_currentFrameIndex < 0 || m_currentFrameIndex >= m_frames.size()) {
		return;
	}

	const VideoFrame &frame = m_frames[m_currentFrameIndex];
	double const TIME_DIFF = frame.timestamp - m_currentSpike.timestamp;
	double const FRAME_DIFF = TIME_DIFF * m_videoFPS;

	// Store calculated offset in milliseconds
	m_calculatedOffsetMs = TIME_DIFF * 1000.0;

	QString const SYNC_TEXT =
		QString("Sync Offset: %1ms (%2 frames)").arg(m_calculatedOffsetMs, 0, 'f', 1).arg(FRAME_DIFF, 0, 'f', 2);

	// Best practice color coding:
	// Green: < 1 frame difference (perfect sync)
	// Yellow: 1-2 frames difference (acceptable)
	// Red: > 2 frames difference (needs correction)
	QString color = "white";
	if (qAbs(FRAME_DIFF) < 1.0) {
		color = "green"; // In sync
	} else if (qAbs(FRAME_DIFF) < 2.0) {
		color = "yellow"; // Close
	} else {
		color = "red"; // Out of sync
	}

	m_syncOffsetLabel->setText(SYNC_TEXT);
	m_syncOffsetLabel->setStyleSheet(QString("color: %1;").arg(color));

	// Enable apply button if we have a calculated offset and at least one source selected
	QStringList audioSources = getSelectedAudioSources();
	QStringList videoSources = getSelectedVideoSources();
	bool canApply = qAbs(m_calculatedOffsetMs) > 0.01 && // Non-zero offset
			(!audioSources.isEmpty() || !videoSources.isEmpty()); // At least one source selected
	m_applyOffsetButton->setEnabled(canApply);
}

void AudioSyncModal::onSpikePositionChanged(double timestamp)
{
	m_currentSpike.timestamp = timestamp;
	updateSyncDisplay();
}

void AudioSyncModal::onVideoFramePositionChanged(double timestamp)
{
	// Find the frame closest to the new timestamp
	if (m_frames.isEmpty()) {
		return;
	}

	double minDiff = 1e10;
	int bestFrameIndex = m_currentFrameIndex;
	for (int i = 0; i < m_frames.size(); i++) {
		double diff = qAbs(m_frames[i].timestamp - timestamp);
		if (diff < minDiff) {
			minDiff = diff;
			bestFrameIndex = i;
		}
	}

	if (bestFrameIndex != m_currentFrameIndex) {
		m_currentFrameIndex = bestFrameIndex;
		updateFrameDisplay();
	}
}

void AudioSyncModal::onPrevFrameClicked()
{
	if (m_currentFrameIndex > 0) {
		m_currentFrameIndex--;
		updateFrameDisplay();
	}
}

void AudioSyncModal::onNextFrameClicked()
{
	if (m_currentFrameIndex < m_frames.size() - 1) {
		m_currentFrameIndex++;
		updateFrameDisplay();
	}
}

void AudioSyncModal::onZoomInClicked()
{
	m_timelineWidget->zoomIn();
}

void AudioSyncModal::onZoomOutClicked()
{
	m_timelineWidget->zoomOut();
}

void AudioSyncModal::onResetZoomClicked()
{
	m_timelineWidget->resetZoom();
}

void AudioSyncModal::setupSourceSelection()
{
	refreshSourceList();
}

void AudioSyncModal::refreshSourceList()
{
	if (!m_sourceOffsetManager) {
		return;
	}

	// Store current selections (checked items)
	QStringList checkedAudioSources;
	for (int i = 0; i < m_audioSourcesList->count(); ++i) {
		QListWidgetItem *item = m_audioSourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			checkedAudioSources.append(item->data(Qt::UserRole).toString());
		}
	}

	QStringList checkedVideoSources;
	for (int i = 0; i < m_videoSourcesList->count(); ++i) {
		QListWidgetItem *item = m_videoSourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			checkedVideoSources.append(item->data(Qt::UserRole).toString());
		}
	}

	// Clear and repopulate audio sources
	m_audioSourcesList->clear();
	QList<SourceInfo> audioSources = m_sourceOffsetManager->getAudioSources();
	for (const SourceInfo &info : audioSources) {
		QListWidgetItem *item = new QListWidgetItem(info.name);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(checkedAudioSources.contains(info.name) ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole, info.name);
		m_audioSourcesList->addItem(item);
	}

	// Clear and repopulate video sources
	m_videoSourcesList->clear();
	QList<SourceInfo> videoSources = m_sourceOffsetManager->getVideoSources();
	for (const SourceInfo &info : videoSources) {
		QListWidgetItem *item = new QListWidgetItem(info.name);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(checkedVideoSources.contains(info.name) ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole, info.name);
		m_videoSourcesList->addItem(item);
	}

	// Update offset displays
	updateOffsetDisplay();
}

void AudioSyncModal::updateOffsetDisplay()
{
	// Update apply button state
	updateSyncDisplay();
}

void AudioSyncModal::onSourceSelectionChanged()
{
	updateOffsetDisplay();
}

QStringList AudioSyncModal::getSelectedAudioSources() const
{
	QStringList selected;
	if (!m_audioSourcesList) {
		return selected;
	}
	for (int i = 0; i < m_audioSourcesList->count(); ++i) {
		QListWidgetItem *item = m_audioSourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			selected.append(item->data(Qt::UserRole).toString());
		}
	}
	return selected;
}

QStringList AudioSyncModal::getSelectedVideoSources() const
{
	QStringList selected;
	if (!m_videoSourcesList) {
		return selected;
	}
	for (int i = 0; i < m_videoSourcesList->count(); ++i) {
		QListWidgetItem *item = m_videoSourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			selected.append(item->data(Qt::UserRole).toString());
		}
	}
	return selected;
}

void AudioSyncModal::onApplyOffsetClicked()
{
	if (!m_sourceOffsetManager || qAbs(m_calculatedOffsetMs) < 0.01) {
		return;
	}

	QStringList audioSources = getSelectedAudioSources();
	QStringList videoSources = getSelectedVideoSources();

	if (audioSources.isEmpty() && videoSources.isEmpty()) {
		QMessageBox::warning(this, "Apply Failed", "No sources selected. Please select at least one source to sync.");
		return;
	}

	QStringList appliedSources;

	// Apply to all selected audio sources
	for (const QString &source : audioSources) {
		if (m_sourceOffsetManager->setSourceOffset(source, static_cast<int>(m_calculatedOffsetMs), true)) {
			appliedSources.append(source);
		}
	}

	// Apply to all selected video sources
	for (const QString &source : videoSources) {
		if (m_sourceOffsetManager->setSourceOffset(source, static_cast<int>(m_calculatedOffsetMs), true)) {
			appliedSources.append(source);
		}
	}

	if (appliedSources.isEmpty()) {
		QMessageBox::warning(this, "Apply Failed", "Failed to apply offset to any sources. Please check that sources have Async Delay filters.");
	} else {
		QMessageBox::information(this, "Offset Applied",
					 QString("Applied offset of %1ms to:\n%2")
						 .arg(m_calculatedOffsetMs, 0, 'f', 1)
						 .arg(appliedSources.join("\n")));
		// Refresh offset displays
		updateOffsetDisplay();
	}
}
