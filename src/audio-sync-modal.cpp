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
#include <qset.h>
#include <qcheckbox.h>
#include <QResizeEvent>

AudioSyncModal::AudioSyncModal(const QString &filePath, QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Audio Sync - " + QFileInfo(filePath).fileName());
	setModal(true);
	setMinimumSize(900, 700);
	resize(1200, 900); // Start larger than minimum

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
	if (m_sourcesList) {
		disconnect(m_sourcesList, nullptr, this, nullptr);
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

	// Timeline widget - fixed height
	m_timelineWidget = new TimelineWidget(this);
	m_timelineWidget->setVisible(false);
	m_timelineWidget->setFixedHeight(220); // Static height
	mainLayout->addWidget(m_timelineWidget);

	// Zoom and frame navigation controls
	auto *zoomLayout = new QHBoxLayout();
	m_zoomInButton = new QPushButton("Zoom In", this);
	m_zoomInButton->setVisible(false);
	m_zoomOutButton = new QPushButton("Zoom Out", this);
	m_zoomOutButton->setVisible(false);
	m_resetZoomButton = new QPushButton("Reset Zoom", this);
	m_resetZoomButton->setVisible(false);

	// Offset label between zoom and navigation buttons
	m_syncOffsetLabel = new QLabel("", this);
	QFont syncFont = m_syncOffsetLabel->font();
	syncFont.setPointSize(12);
	syncFont.setBold(true);
	m_syncOffsetLabel->setFont(syncFont);
	m_syncOffsetLabel->setVisible(false);

	// Snap to peaks toggle button
	m_snapToPeaksButton = new QPushButton("Snap to Peaks", this);
	m_snapToPeaksButton->setCheckable(true);
	m_snapToPeaksButton->setChecked(false);
	m_snapToPeaksButton->setVisible(false);
	m_snapToPeaksButton->setToolTip("Enable gentle snapping to waveform peaks when dragging audio cursor");

	m_prevFrameButton = new QPushButton("< Prev", this);
	m_prevFrameButton->setEnabled(false);
	m_prevFrameButton->setVisible(false);
	m_nextFrameButton = new QPushButton("Next >", this);
	m_nextFrameButton->setEnabled(false);
	m_nextFrameButton->setVisible(false);
	zoomLayout->addWidget(m_zoomInButton);
	zoomLayout->addWidget(m_zoomOutButton);
	zoomLayout->addWidget(m_resetZoomButton);
	zoomLayout->addWidget(m_snapToPeaksButton);
	zoomLayout->addStretch();
	zoomLayout->addWidget(m_syncOffsetLabel);
	zoomLayout->addStretch();
	zoomLayout->addWidget(m_prevFrameButton);
	zoomLayout->addWidget(m_nextFrameButton);
	mainLayout->addLayout(zoomLayout);

	// Video frame display - grows to fill available space
	m_frameLabel = new QLabel(this);
	m_frameLabel->setMinimumHeight(200);
	m_frameLabel->setAlignment(Qt::AlignCenter);
	m_frameLabel->setStyleSheet("background-color: black; border: 1px solid gray;");
	m_frameLabel->setText("No frame loaded");
	m_frameLabel->setScaledContents(false); // Don't stretch - we handle scaling manually with aspect ratio
	m_frameLabel->setVisible(false);
	mainLayout->addWidget(m_frameLabel, 1); // Stretch factor 1 - grows to fill

	// Source selection section with filter checkbox, list and apply button
	auto *sourceLayout = new QVBoxLayout();

	// Filter checkbox above apply button
	m_filterNonZeroCheckbox = new QCheckBox("Show only sources with non-zero offsets", this);
	m_filterNonZeroCheckbox->setToolTip("Filter to show only sources that have offsets configured");
	m_filterNonZeroCheckbox->setChecked(true); // Default to checked
	connect(m_filterNonZeroCheckbox, &QCheckBox::toggled, this, &AudioSyncModal::onFilterNonZeroToggled);

	auto *sourceRowLayout = new QHBoxLayout();
	m_sourcesList = new QListWidget(this);
	m_sourcesList->setSelectionMode(QAbstractItemView::NoSelection);
	m_sourcesList->setMinimumHeight(100);
	m_sourcesList->setToolTip("Select audio and video sources to sync to match the recording");
	sourceRowLayout->addWidget(m_sourcesList);

	// Apply offset button to the right
	auto *buttonLayout = new QVBoxLayout();
	buttonLayout->addWidget(m_filterNonZeroCheckbox);
	buttonLayout->addStretch();
	m_applyOffsetButton = new QPushButton("Apply Offset", this);
	m_applyOffsetButton->setToolTip("Apply the calculated sync offset as a delta to the selected source");
	m_applyOffsetButton->setEnabled(false);
	buttonLayout->addWidget(m_applyOffsetButton);
	sourceRowLayout->addLayout(buttonLayout);

	sourceLayout->addLayout(sourceRowLayout);
	mainLayout->addLayout(sourceLayout, 0); // No stretch - expands after video frame fills

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

	// Connect signals
	connect(m_timelineWidget, &TimelineWidget::spikePositionChanged, this, &AudioSyncModal::onSpikePositionChanged);
	connect(m_timelineWidget, &TimelineWidget::videoFramePositionChanged, this,
		&AudioSyncModal::onVideoFramePositionChanged);
	connect(m_prevFrameButton, &QPushButton::clicked, this, &AudioSyncModal::onPrevFrameClicked);
	connect(m_nextFrameButton, &QPushButton::clicked, this, &AudioSyncModal::onNextFrameClicked);
	connect(m_zoomInButton, &QPushButton::clicked, this, &AudioSyncModal::onZoomInClicked);
	connect(m_zoomOutButton, &QPushButton::clicked, this, &AudioSyncModal::onZoomOutClicked);
	connect(m_resetZoomButton, &QPushButton::clicked, this, &AudioSyncModal::onResetZoomClicked);
	connect(m_snapToPeaksButton, &QPushButton::toggled, this, &AudioSyncModal::onSnapToPeaksToggled);
	connect(m_sourcesList, &QListWidget::itemChanged, this, &AudioSyncModal::onSourceSelectionChanged);
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
	connect(m_videoWorker, &VideoExtractionWorker::framesExtractedIncremental, this,
		&AudioSyncModal::onFramesExtractedIncremental);
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

	// Start video extraction in background thread (incremental: priority zone first)
	showSpinner("Extracting frames (priority zone)...");
	m_frameExtractionStartTime = m_currentSpike.windowStart;
	m_frameExtractionEndTime = m_currentSpike.windowEnd;
	m_allFramesExtracted = false;
	QMetaObject::invokeMethod(m_videoWorker, "extractFramesIncremental", Qt::QueuedConnection,
				  Q_ARG(QString, m_currentRecording), Q_ARG(double, m_currentSpike.windowStart),
				  Q_ARG(double, m_currentSpike.windowEnd), Q_ARG(double, m_currentSpike.timestamp));
}

void AudioSyncModal::onAnalysisError(const QString &error)
{
	hideSpinner();
	QMessageBox::warning(this, "Analysis Failed", error);
}

void AudioSyncModal::onFramesExtractedIncremental(const QVector<VideoFrame> &frames, double fps, bool isPriorityPhase)
{
	if (m_frames.isEmpty()) {
		// First batch - initialize
		m_videoFPS = fps;
		m_frames = frames;
		// Show UI components after first batch
		m_frameLabel->setVisible(true);
		m_prevFrameButton->setVisible(true);
		m_nextFrameButton->setVisible(true);
		m_syncOffsetLabel->setVisible(true);
		m_snapToPeaksButton->setVisible(true);
	} else {
		// Merge new frames with existing ones
		mergeFrames(frames);
	}

	// Update timeline with FPS
	m_timelineWidget->setFPS(m_videoFPS);

	// Update timeline with video frame data
	QVector<double> frameTimestamps;
	QVector<double> frameDifferences;
	for (const VideoFrame &frame : m_frames) {
		frameTimestamps.append(frame.timestamp);
		frameDifferences.append(frame.differenceFromPrevious);
	}
	m_timelineWidget->setVideoFrames(frameTimestamps);
	m_timelineWidget->setFrameDifferences(frameDifferences);

	// If this is the priority phase, find the frame closest to the audio spike
	if (isPriorityPhase && m_currentFrameIndex < 0) {
		double minDiff = 1e10;
		int bestFrameIndex = 0;
		for (int i = 0; i < m_frames.size(); i++) {
			double diff = qAbs(m_frames[i].timestamp - m_currentSpike.timestamp);
			if (diff < minDiff) {
				minDiff = diff;
				bestFrameIndex = i;
			}
		}
		m_currentFrameIndex = bestFrameIndex;
		updateFrameDisplay();
		updateSyncDisplay();
	}

	// Update spinner message
	if (isPriorityPhase) {
		// After priority phase, show message for remaining frames
		// The worker will emit remaining frames if any exist, or framesExtracted when complete
		showSpinner("Extracting frames (remaining)...");
	} else {
		// Remaining frames extracted, extraction is complete
		m_allFramesExtracted = true;
		hideSpinner();
	}
}

void AudioSyncModal::onFramesExtracted(const QVector<VideoFrame> &frames, double fps)
{
	// This signal is emitted after all frames are extracted (for backward compatibility)
	// The incremental signal handles the UI updates, so we just mark as complete
	Q_UNUSED(frames);
	Q_UNUSED(fps);
	m_allFramesExtracted = true;
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
	// Scale to fit within label bounds while maintaining aspect ratio
	QSize labelSize = m_frameLabel->size();
	if (labelSize.width() <= 0 || labelSize.height() <= 0) {
		labelSize = QSize(800, 400); // Fallback
	}
	// Scale to fit (not fill) - this ensures the entire image is visible
	QPixmap scaledPixmap = frame.pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	m_frameLabel->setPixmap(scaledPixmap);

	// Update timeline with current video frame position
	m_timelineWidget->setVideoFramePosition(frame.timestamp);

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

	// Calculate distance in meters using speed of sound (~343 m/s at room temperature)
	const double SPEED_OF_SOUND = 343.0; // meters per second
	double distanceMeters = qAbs(TIME_DIFF) * SPEED_OF_SOUND;

	QString const SYNC_TEXT = QString("%1 ms (%2 frames, %3 m)")
					  .arg(m_calculatedOffsetMs, 0, 'f', 1)
					  .arg(FRAME_DIFF, 0, 'f', 2)
					  .arg(distanceMeters, 0, 'f', 2);

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
	bool canApply = qAbs(m_calculatedOffsetMs) > 0.01 &&                  // Non-zero offset
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
	// Check if timestamp is outside the extracted range
	if (m_frames.isEmpty() || timestamp < m_frameExtractionStartTime || timestamp > m_frameExtractionEndTime) {
		// Extract frame on demand
		extractFrameOnDemand(timestamp);
		return;
	}

	// Find the frame closest to the new timestamp
	double minDiff = 1e10;
	int bestFrameIndex = m_currentFrameIndex;
	for (int i = 0; i < m_frames.size(); i++) {
		double diff = qAbs(m_frames[i].timestamp - timestamp);
		if (diff < minDiff) {
			minDiff = diff;
			bestFrameIndex = i;
		}
	}

	// If no frame is close enough (within half a frame duration), extract on demand
	if (m_videoFPS > 0.0) {
		double frameDuration = 1.0 / m_videoFPS;
		if (minDiff > frameDuration * 0.5) {
			extractFrameOnDemand(timestamp);
			return;
		}
	}

	if (bestFrameIndex != m_currentFrameIndex && bestFrameIndex >= 0 && bestFrameIndex < m_frames.size()) {
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

void AudioSyncModal::onSnapToPeaksToggled()
{
	bool enabled = m_snapToPeaksButton->isChecked();
	m_timelineWidget->setSnapToPeaks(enabled);
}

void AudioSyncModal::setupSourceSelection()
{
	refreshSourceList();
}

void AudioSyncModal::refreshSourceList()
{
	if (!m_sourceOffsetManager) {
		qWarning() << "AudioSyncModal::refreshSourceList: m_sourceOffsetManager is nullptr";
		return;
	}

	qInfo() << "AudioSyncModal::refreshSourceList: Refreshing source list";

	// Store current selections (checked items) with their types
	// Use a key that combines name and type to handle cases where same name might exist in both lists
	QSet<QString> checkedSources; // Format: "name:audio" or "name:video"
	for (int i = 0; i < m_sourcesList->count(); ++i) {
		QListWidgetItem *item = m_sourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			QString sourceName = item->data(Qt::UserRole).toString();
			bool isAudio = item->data(Qt::UserRole + 1).toBool();
			QString key = QString("%1:%2").arg(sourceName, isAudio ? "audio" : "video");
			checkedSources.insert(key);
		}
	}

	// Check if filter is enabled
	bool filterNonZero = m_filterNonZeroCheckbox && m_filterNonZeroCheckbox->isChecked();

	// Clear and repopulate combined list
	m_sourcesList->clear();

	// Add audio sources
	QList<SourceInfo> audioSources = m_sourceOffsetManager->getAudioSources();
	qInfo() << "AudioSyncModal::refreshSourceList: Adding" << audioSources.size() << "audio sources";
	for (const SourceInfo &info : audioSources) {
		// Filter: if checkbox is checked, only show sources with non-zero offsets
		if (filterNonZero && info.currentOffsetMs == 0) {
			continue;
		}
		qInfo() << "AudioSyncModal::refreshSourceList: Adding audio source:" << info.name;
		QString offsetText = info.currentOffsetMs != 0 ? QString(" (offset: %1 ms)").arg(info.currentOffsetMs)
							       : "";
		QListWidgetItem *item = new QListWidgetItem(QString("[Audio] %1%2").arg(info.name, offsetText));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		QString key = QString("%1:audio").arg(info.name);
		bool wasChecked = checkedSources.contains(key);
		item->setCheckState(wasChecked ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole, info.name);
		item->setData(Qt::UserRole + 1, true); // Mark as audio source
		m_sourcesList->addItem(item);
	}

	// Add video sources
	QList<SourceInfo> videoSources = m_sourceOffsetManager->getVideoSources();
	qInfo() << "AudioSyncModal::refreshSourceList: Adding" << videoSources.size() << "video sources";
	for (const SourceInfo &info : videoSources) {
		// Filter: if checkbox is checked, only show sources with non-zero offsets
		if (filterNonZero && info.currentOffsetMs == 0) {
			continue;
		}
		qInfo() << "AudioSyncModal::refreshSourceList: Adding video source:" << info.name;
		QString offsetText = info.currentOffsetMs != 0 ? QString(" (offset: %1 ms)").arg(info.currentOffsetMs)
							       : "";
		QListWidgetItem *item = new QListWidgetItem(QString("[Video] %1%2").arg(info.name, offsetText));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		QString key = QString("%1:video").arg(info.name);
		bool wasChecked = checkedSources.contains(key);
		item->setCheckState(wasChecked ? Qt::Checked : Qt::Unchecked);
		item->setData(Qt::UserRole, info.name);
		item->setData(Qt::UserRole + 1, false); // Mark as video source
		m_sourcesList->addItem(item);
	}

	qInfo() << "AudioSyncModal::refreshSourceList: Total items in list:" << m_sourcesList->count();

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
	if (!m_sourcesList) {
		return selected;
	}
	for (int i = 0; i < m_sourcesList->count(); ++i) {
		QListWidgetItem *item = m_sourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			bool isAudio = item->data(Qt::UserRole + 1).toBool();
			if (isAudio) {
				selected.append(item->data(Qt::UserRole).toString());
			}
		}
	}
	return selected;
}

QStringList AudioSyncModal::getSelectedVideoSources() const
{
	QStringList selected;
	if (!m_sourcesList) {
		return selected;
	}
	for (int i = 0; i < m_sourcesList->count(); ++i) {
		QListWidgetItem *item = m_sourcesList->item(i);
		if (item && item->checkState() == Qt::Checked) {
			bool isAudio = item->data(Qt::UserRole + 1).toBool();
			if (!isAudio) {
				selected.append(item->data(Qt::UserRole).toString());
			}
		}
	}
	return selected;
}

void AudioSyncModal::onFilterNonZeroToggled()
{
	refreshSourceList();
}

void AudioSyncModal::onApplyOffsetClicked()
{
	if (!m_sourceOffsetManager || qAbs(m_calculatedOffsetMs) < 0.01) {
		return;
	}

	QStringList audioSources = getSelectedAudioSources();
	QStringList videoSources = getSelectedVideoSources();

	if (audioSources.isEmpty() && videoSources.isEmpty()) {
		QMessageBox::warning(this, "Apply Failed",
				     "No sources selected. Please select at least one source to sync.");
		return;
	}

	QStringList appliedSources;

	// Apply to all selected audio sources
	for (const QString &source : audioSources) {
		if (m_sourceOffsetManager->setSourceOffset(source, static_cast<int>(m_calculatedOffsetMs), true,
							   true)) {
			appliedSources.append(source);
		}
	}

	// Apply to all selected video sources
	for (const QString &source : videoSources) {
		if (m_sourceOffsetManager->setSourceOffset(source, static_cast<int>(m_calculatedOffsetMs), true,
							   false)) {
			appliedSources.append(source);
		}
	}

	if (appliedSources.isEmpty()) {
		QMessageBox::warning(
			this, "Apply Failed",
			"Failed to apply offset to any sources. Please check that audio sources are available or video sources have Async Delay filters.");
	} else {
		QString offsetText = QString::number(m_calculatedOffsetMs, 'f', 1);
		QChar newlineChar(0x0A); // Newline character
		QString sourcesText = appliedSources.join(newlineChar);
		QString header = QString("Applied offset of %1ms to:").arg(offsetText);
		QString message = header + newlineChar + sourcesText;
		QMessageBox::information(this, "Offset Applied", message);
		// Refresh offset displays and source list (to update offset values)
		refreshSourceList();
	}
}

void AudioSyncModal::mergeFrames(const QVector<VideoFrame> &newFrames)
{
	// Merge new frames with existing ones, maintaining sorted order by timestamp
	for (const VideoFrame &newFrame : newFrames) {
		// Check if frame already exists (by timestamp)
		bool exists = false;
		for (const VideoFrame &existingFrame : m_frames) {
			if (qAbs(existingFrame.timestamp - newFrame.timestamp) < 0.001) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			m_frames.append(newFrame);
		}
	}

	// Sort by timestamp
	std::sort(m_frames.begin(), m_frames.end(),
		  [](const VideoFrame &a, const VideoFrame &b) { return a.timestamp < b.timestamp; });

	// Recalculate frame differences for the complete set
	VideoExtractor::calculateFrameDifferences(m_frames);
}

void AudioSyncModal::extractFrameOnDemand(double timestamp)
{
	// Extract a single frame at the requested timestamp
	if (m_currentRecording.isEmpty()) {
		return;
	}

	// Check if we already have this frame
	for (int i = 0; i < m_frames.size(); i++) {
		if (qAbs(m_frames[i].timestamp - timestamp) < 0.001) {
			m_currentFrameIndex = i;
			updateFrameDisplay();
			return;
		}
	}

	// Extract frame using VideoExtractor directly (synchronous, but should be fast for single frame)
	VideoExtractor extractor;
	if (extractor.openFile(m_currentRecording)) {
		VideoFrame frame = extractor.extractFrameAt(timestamp);
		if (!frame.pixmap.isNull()) {
			// Calculate frame number based on position in sequence
			// Find insertion point to calculate frame number
			int insertIndex = 0;
			for (int i = 0; i < m_frames.size(); i++) {
				if (m_frames[i].timestamp < timestamp) {
					insertIndex = i + 1;
				} else {
					break;
				}
			}
			frame.frameNumber = insertIndex;

			// Insert frame in sorted order
			m_frames.insert(insertIndex, frame);

			// Update frame numbers for all frames after insertion
			for (int i = insertIndex + 1; i < m_frames.size(); i++) {
				m_frames[i].frameNumber = i;
			}

			// Recalculate differences
			VideoExtractor::calculateFrameDifferences(m_frames);

			// Find the newly inserted frame
			for (int i = 0; i < m_frames.size(); i++) {
				if (qAbs(m_frames[i].timestamp - timestamp) < 0.001) {
					m_currentFrameIndex = i;
					updateFrameDisplay();

					// Update timeline
					QVector<double> frameTimestamps;
					QVector<double> frameDifferences;
					for (const VideoFrame &f : m_frames) {
						frameTimestamps.append(f.timestamp);
						frameDifferences.append(f.differenceFromPrevious);
					}
					m_timelineWidget->setVideoFrames(frameTimestamps);
					m_timelineWidget->setFrameDifferences(frameDifferences);
					break;
				}
			}
		}
	}
}

void AudioSyncModal::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
	// Update frame display when modal is resized so video frame scales properly
	if (!m_frames.isEmpty() && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_frames.size()) {
		updateFrameDisplay();
	}
}
