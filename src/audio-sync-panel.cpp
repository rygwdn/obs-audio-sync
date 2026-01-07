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
#include "recording-scanner-worker.h"
#include "audio-sync-modal.h"
#include "realtime-audio-monitor.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/base.h>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <qwidget.h>
#include <qboxlayout.h>
#include <qfont.h>
#include <qlistwidget.h>
#include <qabstractitemview.h>
#include <qpushbutton.h>
#include <qnamespace.h>
#include <qlist.h>
#include <qset.h>
#include <qprogressbar.h>
#include <qthread.h>
#include <qtimer.h>
#include <qlabel.h>
#include <qcombobox.h>
#include <qpalette.h>
#include <qcolor.h>

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QWidget(parent)
{
	// Initialize auto-sync state
	m_autoSyncState = AutoSyncState::Idle;
	m_audioMonitor = new RealTimeAudioMonitor(this);
	connect(m_audioMonitor, &RealTimeAudioMonitor::spikeDetected, this, &AudioSyncPanel::onSpikeDetected);
	connect(m_audioMonitor, &RealTimeAudioMonitor::recordingComplete, this, &AudioSyncPanel::onRecordingComplete);
	connect(m_audioMonitor, &RealTimeAudioMonitor::volumeLevelsUpdated, this,
		&AudioSyncPanel::onVolumeLevelsUpdated);
	connect(m_audioMonitor, &RealTimeAudioMonitor::monitoringError, this, &AudioSyncPanel::onMonitoringError);

	// Initialize auto-sync timers
	m_autoSyncStatusTimer = new QTimer(this);
	m_autoSyncStatusTimer->setSingleShot(true);
	m_autoSyncTimeoutTimer = new QTimer(this);
	m_autoSyncTimeoutTimer->setSingleShot(true);
	m_statsThrottleTimer = new QTimer(this);
	m_statsThrottleTimer->setSingleShot(true);
	m_statsThrottleTimer->setInterval(100); // Throttle to 10 updates per second

	setupUI();
	populateAudioSources();
	refreshRecordings();
}

AudioSyncPanel::~AudioSyncPanel()
{
	// Stop audio monitoring FIRST to prevent callbacks from firing during destruction.
	// This is critical because the audio monitor's volmeter callback runs on OBS's
	// audio thread and could race with object destruction.
	if (m_audioMonitor) {
		m_audioMonitor->stopMonitoring();
	}

	// Disconnect all signal/slot connections to prevent any objects from
	// emitting signals to destroyed slots. This is critical to prevent crashes
	// during shutdown. We disconnect in this order:
	// 1. Worker signals (most critical - cross-thread)
	// 2. UI widget signals (safety measure)
	// 3. Keep thread->worker deleteLater connections for proper cleanup

	// Disconnect worker signals (critical - these are cross-thread)
	if (m_scanWorker) {
		disconnect(m_scanWorker, nullptr, this, nullptr);
	}

	// Disconnect UI widget signals (safety measure - prevents signals during destruction)
	if (m_recordingList) {
		disconnect(m_recordingList, nullptr, this, nullptr);
	}
	if (m_autoSyncButton) {
		disconnect(m_autoSyncButton, nullptr, this, nullptr);
	}
	if (m_cancelAutoSyncButton) {
		disconnect(m_cancelAutoSyncButton, nullptr, this, nullptr);
	}
	if (m_endAutoSyncButton) {
		disconnect(m_endAutoSyncButton, nullptr, this, nullptr);
	}
	if (m_audioMonitor) {
		disconnect(m_audioMonitor, nullptr, this, nullptr);
	}

	// Stop refresh timer
	if (m_refreshTimer) {
		m_refreshTimer->stop();
		disconnect(m_refreshTimer, nullptr, this, nullptr);
	}

	// Stop auto-sync timers
	if (m_autoSyncStatusTimer) {
		m_autoSyncStatusTimer->stop();
		disconnect(m_autoSyncStatusTimer, nullptr, this, nullptr);
	}
	if (m_autoSyncTimeoutTimer) {
		m_autoSyncTimeoutTimer->stop();
		disconnect(m_autoSyncTimeoutTimer, nullptr, this, nullptr);
	}
	if (m_statsThrottleTimer) {
		m_statsThrottleTimer->stop();
		disconnect(m_statsThrottleTimer, nullptr, this, nullptr);
	}

	// Stop and cleanup worker threads
	// Workers will be automatically deleted via deleteLater when threads finish
	if (m_scanThread) {
		m_scanThread->quit();
		m_scanThread->wait();
		delete m_scanThread;
		m_scanThread = nullptr;
	}
}

void AudioSyncPanel::setupUI()
{
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(10, 10, 10, 10);
	m_layout->setSpacing(10);

	// Recording list (styled list items) - no header
	m_recordingList = new QListWidget(this);
	m_recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recordingList->setMaximumHeight(200);
	m_recordingList->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_recordingList->setItemDelegate(new HtmlListDelegate(m_recordingList));
	m_layout->addWidget(m_recordingList);

	// Auto Sync controls layout - source dropdown next to Auto Sync button
	auto *autoSyncLayout = new QHBoxLayout();
	m_audioSourceCombo = new QComboBox(this);
	m_audioSourceCombo->setToolTip("Select audio source to monitor for clap detection");
	autoSyncLayout->addWidget(m_audioSourceCombo);

	m_autoSyncButton = new QPushButton("Auto Sync", this);
	m_autoSyncButton->setToolTip("Start recording, detect clap, and automatically analyze");
	autoSyncLayout->addWidget(m_autoSyncButton);

	// Cancel and End buttons (initially hidden, shown during auto-sync)
	m_cancelAutoSyncButton = new QPushButton("Cancel", this);
	m_cancelAutoSyncButton->setToolTip("Stop recording without opening analysis modal");
	m_cancelAutoSyncButton->setVisible(false);
	autoSyncLayout->addWidget(m_cancelAutoSyncButton);

	m_endAutoSyncButton = new QPushButton("End", this);
	m_endAutoSyncButton->setToolTip("Stop recording and open analysis modal");
	m_endAutoSyncButton->setVisible(false);
	autoSyncLayout->addWidget(m_endAutoSyncButton);

	m_layout->addLayout(autoSyncLayout);

	// Status label
	m_statusLabel = new QLabel("Ready", this);
	// Use theme's dim text color
	QPalette statusPalette = m_statusLabel->palette();
	statusPalette.setColor(QPalette::WindowText, statusPalette.color(QPalette::Disabled, QPalette::WindowText));
	m_statusLabel->setPalette(statusPalette);
	m_layout->addWidget(m_statusLabel);

	// Volume levels label (initially hidden) - dimmed with smaller font
	m_volumeLevelsLabel = new QLabel("", this);
	// Use theme color with monospace font, dimmed
	QFont monoFont("monospace");
	monoFont.setStyleHint(QFont::Monospace);
	monoFont.setPointSizeF(monoFont.pointSizeF() * 0.9); // Slightly smaller
	m_volumeLevelsLabel->setFont(monoFont);
	QPalette volumePalette = m_volumeLevelsLabel->palette();
	volumePalette.setColor(QPalette::WindowText, volumePalette.color(QPalette::Disabled, QPalette::WindowText));
	m_volumeLevelsLabel->setPalette(volumePalette);
	m_volumeLevelsLabel->setVisible(false);
	m_layout->addWidget(m_volumeLevelsLabel);

	// Spinner (initially hidden)
	m_spinner = new QProgressBar(this);
	m_spinner->setRange(0, 0); // Indeterminate progress
	m_spinner->setTextVisible(false);
	m_spinner->setVisible(false);
	m_layout->addWidget(m_spinner);

	m_spinnerLabel = new QLabel("", this);
	// Use theme's dim text color
	QPalette spinnerPalette = m_spinnerLabel->palette();
	spinnerPalette.setColor(QPalette::WindowText, spinnerPalette.color(QPalette::Disabled, QPalette::WindowText));
	m_spinnerLabel->setPalette(spinnerPalette);
	m_spinnerLabel->setVisible(false);
	m_layout->addWidget(m_spinnerLabel);

	// Connect signals - only double-click to open recordings
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this, &AudioSyncPanel::onRecordingSelected);
	connect(m_autoSyncButton, &QPushButton::clicked, this, &AudioSyncPanel::onAutoSyncClicked);
	connect(m_cancelAutoSyncButton, &QPushButton::clicked, this,
		[this]() { stopAutoSyncRecording(false); }); // Cancel without modal
	connect(m_endAutoSyncButton, &QPushButton::clicked, this,
		[this]() { stopAutoSyncRecording(true); }); // End with modal

	// Setup refresh timer for delayed refresh after recording events
	// This ensures the file is ready after muxing completes
	m_refreshTimer = new QTimer(this);
	m_refreshTimer->setSingleShot(true);
	m_refreshTimer->setInterval(500); // 500ms delay to ensure file is ready
	connect(m_refreshTimer, &QTimer::timeout, this, &AudioSyncPanel::refreshRecordings);

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
}

// Helper structure for enumerating audio sources
struct AudioSourceEnumData {
	QStringList *sourceNames;
	QSet<QString> *addedSources; // Track sources we've already added to avoid duplicates
};

// Recursively enumerate sources, including those in groups
static void enumerateSourceRecursive(obs_source_t *source, AudioSourceEnumData *data)
{
	if (source == nullptr) {
		return;
	}

	const char *sourceName = obs_source_get_name(source);
	if (sourceName == nullptr) {
		return;
	}

	QString sourceNameStr = QString::fromUtf8(sourceName);

	// Check if source has audio
	uint32_t outputFlags = obs_source_get_output_flags(source);
	if ((outputFlags & OBS_SOURCE_AUDIO) != 0) {
		// Filter out sources that don't meet requirements:
		// 1. Must be active (currently rendering)
		// 2. Must not be hidden
		// 3. Must have audio data available
		// 4. Must output to track 0

		// Check if source is active
		if (!obs_source_active(source)) {
			return;
		}

		// Check if source is showing (not hidden)
		if (!obs_source_showing(source)) {
			return;
		}

		// Check audio mixers - must output to track 0 (bit 0 set)
		uint32_t mixers = obs_source_get_audio_mixers(source);
		if ((mixers & (1 << 0)) == 0) {
			return;
		}

		// Check if we've already added this source
		if (!data->addedSources->contains(sourceNameStr)) {
			data->sourceNames->append(sourceNameStr);
			data->addedSources->insert(sourceNameStr);
		}
	}

	// Check if this is a group and enumerate its children
	const char *sourceId = obs_source_get_id(source);
	if (sourceId != nullptr && strcmp(sourceId, "group") == 0) {
		obs_source_enum_active_sources(
			source,
			[](obs_source_t *parent, obs_source_t *child, void *param) {
				Q_UNUSED(parent);
				enumerateSourceRecursive(child, static_cast<AudioSourceEnumData *>(param));
			},
			data);
	}
}

// Callback for enumerating all sources, filtering by scene
static bool enumAllSourcesCallback(void *param, obs_source_t *source)
{
	if (source == nullptr) {
		return true;
	}

	// Get the scene this source belongs to (if any)
	// We'll enumerate recursively to find audio sources
	enumerateSourceRecursive(source, static_cast<AudioSourceEnumData *>(param));
	return true;
}

void AudioSyncPanel::populateAudioSources()
{
	// Preserve current selection
	QString currentSelection = m_audioSourceCombo->currentText();

	m_audioSourceCombo->clear();

	// Get current scene
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (scene == nullptr) {
		blog(LOG_WARNING, "[AudioSync] populateAudioSources: No current scene");
		m_audioSourceCombo->addItem("(No scene active)");
		m_audioSourceCombo->setEnabled(false);
		return;
	}

	QStringList sourceNames;
	QSet<QString> addedSources;
	AudioSourceEnumData enumData;
	enumData.sourceNames = &sourceNames;
	enumData.addedSources = &addedSources;

	// Enumerate all active sources in the scene (this includes sources in groups)
	obs_source_enum_active_sources(
		scene,
		[](obs_source_t *parent, obs_source_t *child, void *param) {
			Q_UNUSED(parent);
			enumerateSourceRecursive(child, static_cast<AudioSourceEnumData *>(param));
		},
		&enumData);

	obs_source_release(scene);

	if (sourceNames.isEmpty()) {
		m_audioSourceCombo->addItem("(No audio sources)");
		m_audioSourceCombo->setEnabled(false);
		blog(LOG_INFO, "[AudioSync] populateAudioSources: No audio sources found in scene");
	} else {
		m_audioSourceCombo->addItems(sourceNames);
		m_audioSourceCombo->setEnabled(true);

		// Restore previous selection if it still exists
		if (!currentSelection.isEmpty() && sourceNames.contains(currentSelection)) {
			int index = m_audioSourceCombo->findText(currentSelection);
			if (index >= 0) {
				m_audioSourceCombo->setCurrentIndex(index);
			}
		}

		blog(LOG_INFO, "[AudioSync] populateAudioSources: Found %lld audio sources",
		     static_cast<long long>(sourceNames.size()));
	}
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

	// Start scanning in background thread
	QMetaObject::invokeMethod(m_scanWorker, "scanRecordings", Qt::QueuedConnection, Q_ARG(double, 30.0));
}

void AudioSyncPanel::onRecordingsScanned(const QList<RecordingInfo> &recordings)
{
	m_recordingList->clear();

	for (const RecordingInfo &recording : recordings) {
		QFileInfo fileInfo(recording.filePath);

		// Create styled text with timestamp, length, and filename
		QString timestamp = recording.modifiedTime.toString("yyyy-MM-dd hh:mm:ss");
		QString length = QString("%1s").arg(recording.duration, 0, 'f', 2);
		QString filename = fileInfo.fileName();

		// Format with styling: timestamp (bold), length (gray), filename (normal)
		QString itemText =
			QString("<b>%1</b> <span style='color: gray;'>%2</span> %3").arg(timestamp, length, filename);

		auto *item = new QListWidgetItem(itemText);
		item->setData(Qt::UserRole, recording.filePath);
		m_recordingList->addItem(item);
	}

	hideSpinner();
	m_statusLabel->setText("Ready");
}

void AudioSyncPanel::onScanError(const QString &error)
{
	hideSpinner();
	m_statusLabel->setText("Scan failed");
	QMessageBox::warning(this, "Scan Error", error);
}

void AudioSyncPanel::onRecordingSelected(QListWidgetItem *item)
{
	if (item == nullptr) {
		return;
	}

	QString const FILE_PATH = item->data(Qt::UserRole).toString();
	auto *modal = new AudioSyncModal(FILE_PATH, this);
	modal->exec();
	delete modal;
}

void AudioSyncPanel::scheduleDelayedRefresh()
{
	// Restart the timer - if it's already running, this will reset it
	// This ensures we only refresh once after the last event
	if (m_refreshTimer) {
		m_refreshTimer->start();
	}

	// If we were doing an auto-sync recording, handle it
	if (m_autoSyncState == AutoSyncState::Stopping) {
		handleAutoSyncRecordingStopped();
	}
}

void AudioSyncPanel::onAutoSyncClicked()
{
	if (m_autoSyncState != AutoSyncState::Idle) {
		// If clicking Auto Sync button again during recording, treat it as Cancel
		stopAutoSyncRecording(false);
		return;
	}

	startAutoSyncRecording();
}

void AudioSyncPanel::startAutoSyncRecording()
{
	// Check if recording is already active
	if (obs_frontend_recording_active()) {
		QMessageBox::warning(this, "Recording Active",
				     "OBS is already recording. Please stop the current recording first.");
		return;
	}

	// Refresh audio sources list before starting
	populateAudioSources();

	// Get recording directory to predict file path
	const char *recordingPath = obs_frontend_get_current_record_output_path();
	if (!recordingPath || strlen(recordingPath) == 0) {
		QMessageBox::warning(this, "Recording Path Error",
				     "Could not determine recording path. Please check OBS recording settings.");
		return;
	}

	QString path = QString::fromUtf8(recordingPath);
	blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Initial recording path from OBS: %s",
	     path.toUtf8().constData());

	// Expand ~ to home directory
	if (path.startsWith("~/")) {
		path = QDir::homePath() + path.mid(1);
	} else if (path == "~") {
		path = QDir::homePath();
	}

	// Normalize the path
	path = QDir::cleanPath(path);
	blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Normalized path: %s", path.toUtf8().constData());

	QFileInfo pathInfo(path);
	QString recordingDir;
	if (pathInfo.isDir()) {
		recordingDir = pathInfo.absoluteFilePath();
		blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Path is a directory: %s",
		     recordingDir.toUtf8().constData());
		// When it's a directory, we'll need to find the actual file after recording starts
		m_autoSyncRecordingPath = recordingDir;
	} else {
		recordingDir = pathInfo.absolutePath();
		QString baseName = pathInfo.baseName();
		blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Path is a file, dir: %s, baseName: %s",
		     recordingDir.toUtf8().constData(), baseName.toUtf8().constData());
		// Store expected recording path pattern (OBS will create the file)
		m_autoSyncRecordingPath = recordingDir + "/" + baseName;
		if (m_autoSyncRecordingPath.isEmpty()) {
			m_autoSyncRecordingPath = recordingDir;
		}
	}
	blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Stored recording path: %s",
	     m_autoSyncRecordingPath.toUtf8().constData());

	// Start recording
	obs_frontend_recording_start();

	// Update state
	m_autoSyncState = AutoSyncState::Recording;
	m_spikeTimestamp = 0.0;

	// Update UI
	m_autoSyncButton->setVisible(false); // Hide Auto Sync button
	m_cancelAutoSyncButton->setVisible(true);
	m_cancelAutoSyncButton->setEnabled(true);
	m_endAutoSyncButton->setVisible(true);
	m_endAutoSyncButton->setEnabled(true);
	m_statusLabel->setText("Collecting baseline");
	// Use theme's warning/orange color
	QPalette palette = m_statusLabel->palette();
	palette.setColor(QPalette::WindowText, QColor(255, 140, 0)); // Orange
	m_statusLabel->setPalette(palette);
	m_volumeLevelsLabel->setVisible(true);
	m_volumeLevelsLabel->setText("Current: -- | Min: -- | Baseline: -- | Max: --");

	// Get selected audio source
	QString selectedSource = m_audioSourceCombo->currentText();
	if (selectedSource.isEmpty() || selectedSource.startsWith("(")) {
		QMessageBox::warning(this, "No Audio Source Selected",
				     "Please select an audio source from the dropdown.");
		stopAutoSyncRecording(false);
		return;
	}

	// Start monitoring OBS audio output from selected source
	if (!m_audioMonitor->startMonitoring(selectedSource)) {
		blog(LOG_ERROR, "[AudioSync] startAutoSyncRecording: Failed to start audio monitoring for source: %s",
		     selectedSource.toUtf8().constData());
		QMessageBox::warning(this, "Audio Monitoring Error",
				     QString("Failed to start audio monitoring for source: %1").arg(selectedSource));
		stopAutoSyncRecording(false);
		return;
	}

	blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Started monitoring OBS audio output");

	// Update UI after monitoring starts (baseline collection phase)
	disconnect(m_autoSyncStatusTimer, nullptr, this, nullptr);
	connect(m_autoSyncStatusTimer, &QTimer::timeout, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			m_statusLabel->setText("Waiting for clap");
			// Keep orange color
		}
	});
	m_autoSyncStatusTimer->start(2100);

	// Set timeout (30 seconds)
	disconnect(m_autoSyncTimeoutTimer, nullptr, this, nullptr);
	connect(m_autoSyncTimeoutTimer, &QTimer::timeout, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			// Timeout reached - stop without modal
			stopAutoSyncRecording(false);
			QMessageBox::information(this, "Auto Sync Timeout",
						 "No audio spike detected within 30 seconds. Recording stopped.");
		}
	});
	m_autoSyncTimeoutTimer->start(30000);
}

void AudioSyncPanel::stopAutoSyncRecording(bool openModal)
{
	if (m_autoSyncState == AutoSyncState::Idle) {
		return;
	}

	// Store whether we should open modal
	bool shouldOpenModal = openModal || (m_autoSyncState == AutoSyncState::PostSpike);

	// Stop auto-sync timers to prevent them from firing after we've stopped
	if (m_autoSyncStatusTimer) {
		m_autoSyncStatusTimer->stop();
	}
	if (m_autoSyncTimeoutTimer) {
		m_autoSyncTimeoutTimer->stop();
	}

	// Stop monitoring
	m_audioMonitor->stopMonitoring();

	// Stop recording if still active
	if (obs_frontend_recording_active()) {
		obs_frontend_recording_stop();
		m_autoSyncState = shouldOpenModal ? AutoSyncState::Stopping : AutoSyncState::Idle;
	} else {
		m_autoSyncState = AutoSyncState::Idle;
	}

	// Update UI
	m_autoSyncButton->setVisible(true);
	m_autoSyncButton->setEnabled(true);
	m_cancelAutoSyncButton->setVisible(false);
	m_endAutoSyncButton->setVisible(false);
	m_statusLabel->setText(shouldOpenModal ? "Stopping..." : "Cancelled");
	// Reset to theme's dim text color
	QPalette palette = m_statusLabel->palette();
	palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
	m_statusLabel->setPalette(palette);
	m_volumeLevelsLabel->setVisible(false);

	// If we're not opening modal and recording has stopped, just refresh
	if (!shouldOpenModal && m_autoSyncState == AutoSyncState::Idle) {
		scheduleDelayedRefresh();
	}
}

void AudioSyncPanel::onSpikeDetected(double timestamp)
{
	if (m_autoSyncState != AutoSyncState::Recording) {
		return;
	}

	m_spikeTimestamp = timestamp;
	m_autoSyncState = AutoSyncState::PostSpike;

	// Update UI
	m_statusLabel->setText("Clap detected");
	// Use theme's success/green color
	QPalette palette = m_statusLabel->palette();
	palette.setColor(QPalette::WindowText, QColor(0, 200, 0)); // Bright green
	m_statusLabel->setPalette(palette);
}

void AudioSyncPanel::onRecordingComplete(double spikeTimestamp)
{
	if (m_autoSyncState != AutoSyncState::PostSpike) {
		return;
	}

	m_spikeTimestamp = spikeTimestamp;

	// Stop recording and open modal automatically
	stopAutoSyncRecording(true);
}

void AudioSyncPanel::onVolumeLevelsUpdated(double baseline, double current, double threshold, double minVol,
					   double maxVol, double avgVol)
{
	Q_UNUSED(avgVol);
	if (m_autoSyncState != AutoSyncState::Recording && m_autoSyncState != AutoSyncState::PostSpike) {
		return;
	}

	// Throttle updates - only update UI every 100ms
	if (m_statsThrottleTimer->isActive()) {
		return;
	}

	// Check if baseline has been collected by seeing if threshold is set
	const double epsilon = 0.000001;
	QString text;
	if (baseline >= epsilon || threshold >= epsilon) {
		// Baseline collected - show compact inline stats with reduced precision
		text = QString("Current: %1 | Min: %2 | Baseline: %3 | Max: %4")
			       .arg(current, 0, 'f', 3)
			       .arg(minVol, 0, 'f', 3)
			       .arg(baseline, 0, 'f', 3)
			       .arg(maxVol, 0, 'f', 3);
	} else {
		// Still collecting baseline - show placeholder
		text = QString("Current: %1 | Min: %2 | Baseline: -- | Max: %3")
			       .arg(current, 0, 'f', 3)
			       .arg(minVol, 0, 'f', 3)
			       .arg(maxVol, 0, 'f', 3);
	}
	m_volumeLevelsLabel->setText(text);

	// Start throttle timer
	m_statsThrottleTimer->start();
}

void AudioSyncPanel::onMonitoringError(const QString &error)
{
	qWarning() << "RealTimeAudioMonitor error:" << error;
	// Don't stop recording on monitoring error, just log it
	// The recording will continue and user can manually stop
}

void AudioSyncPanel::handleAutoSyncRecordingStopped()
{
	if (m_autoSyncState != AutoSyncState::Stopping) {
		return;
	}

	m_autoSyncState = AutoSyncState::Idle;

	// Wait a bit for file to be ready, then find and load it
	QTimer::singleShot(1000, this, [this]() {
		// Try to get the last recording
		char *lastRecording = obs_frontend_get_last_recording();
		QString recordingPath;

		if (lastRecording && strlen(lastRecording) > 0) {
			recordingPath = QString::fromUtf8(lastRecording);
			bfree(lastRecording);
		} else {
			// Fallback: find newest file in recording directory
			QFileInfo pathInfo(m_autoSyncRecordingPath);
			QString dir = pathInfo.absolutePath();
			QDir directory(dir);
			QStringList filters = {"*.mkv", "*.mp4", "*.flv", "*.mov", "*.avi", "*.webm"};
			QFileInfoList files =
				directory.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);
			if (!files.isEmpty()) {
				recordingPath = files.first().absoluteFilePath();
			}
		}

		if (!recordingPath.isEmpty() && QFileInfo(recordingPath).exists()) {
			// Open the modal automatically
			auto *modal = new AudioSyncModal(recordingPath, this);
			modal->exec();
			delete modal;
		} else {
			QMessageBox::warning(this, "Auto Sync",
					     "Recording completed but file not found. Please select it manually.");
		}

		// Refresh recordings list
		refreshRecordings();

		// Reset UI
		m_statusLabel->setText("Ready");
		// Reset to theme's dim text color
		QPalette palette = m_statusLabel->palette();
		palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
		m_statusLabel->setPalette(palette);
	});
}
