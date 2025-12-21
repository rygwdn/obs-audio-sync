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

	setupUI();
	populateAudioSources();
	refreshRecordings();
}

AudioSyncPanel::~AudioSyncPanel()
{
	// Disconnect all signal/slot connections first to prevent any objects from
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
	if (m_refreshButton) {
		disconnect(m_refreshButton, nullptr, this, nullptr);
	}
	if (m_startSyncButton) {
		disconnect(m_startSyncButton, nullptr, this, nullptr);
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

	// Title
	auto *titleLabel = new QLabel("Audio Sync", this);
	QFont titleFont = titleLabel->font();
	titleFont.setPointSize(14);
	titleFont.setBold(true);
	titleLabel->setFont(titleFont);
	m_layout->addWidget(titleLabel);

	// Recording list label
	auto *listLabel = new QLabel("Recordings (< 15s):", this);
	m_layout->addWidget(listLabel);

	// Recording list (styled list items)
	m_recordingList = new QListWidget(this);
	m_recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recordingList->setMaximumHeight(200);
	m_recordingList->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_recordingList->setItemDelegate(new HtmlListDelegate(m_recordingList));
	m_layout->addWidget(m_recordingList);

	// Button layout
	// Audio source selection dropdown
	auto *sourceLayout = new QHBoxLayout();
	auto *sourceLabel = new QLabel("Audio Source:", this);
	sourceLayout->addWidget(sourceLabel);
	m_audioSourceCombo = new QComboBox(this);
	m_audioSourceCombo->setToolTip("Select audio source to monitor for clap detection");
	sourceLayout->addWidget(m_audioSourceCombo);
	auto *refreshSourcesButton = new QPushButton("Refresh", this);
	refreshSourcesButton->setToolTip("Refresh list of audio sources from current scene");
	refreshSourcesButton->setMaximumWidth(80);
	connect(refreshSourcesButton, &QPushButton::clicked, this, &AudioSyncPanel::populateAudioSources);
	sourceLayout->addWidget(refreshSourcesButton);
	m_layout->addLayout(sourceLayout);

	auto *buttonLayout = new QHBoxLayout();
	m_refreshButton = new QPushButton("Refresh", this);
	buttonLayout->addWidget(m_refreshButton);
	m_startSyncButton = new QPushButton("Start Sync", this);
	m_startSyncButton->setEnabled(false);
	buttonLayout->addWidget(m_startSyncButton);
	m_autoSyncButton = new QPushButton("Auto Sync", this);
	m_autoSyncButton->setToolTip("Start recording, detect clap, and automatically analyze");
	buttonLayout->addWidget(m_autoSyncButton);

	// Cancel and End buttons (initially hidden, shown during auto-sync)
	m_cancelAutoSyncButton = new QPushButton("Cancel", this);
	m_cancelAutoSyncButton->setToolTip("Stop recording without opening analysis modal");
	m_cancelAutoSyncButton->setVisible(false);
	buttonLayout->addWidget(m_cancelAutoSyncButton);

	m_endAutoSyncButton = new QPushButton("End", this);
	m_endAutoSyncButton->setToolTip("Stop recording and open analysis modal");
	m_endAutoSyncButton->setVisible(false);
	buttonLayout->addWidget(m_endAutoSyncButton);

	m_layout->addLayout(buttonLayout);

	// Status label
	m_statusLabel = new QLabel("Ready", this);
	m_statusLabel->setStyleSheet("color: gray;");
	m_layout->addWidget(m_statusLabel);

	// Volume levels label (initially hidden)
	m_volumeLevelsLabel = new QLabel("", this);
	m_volumeLevelsLabel->setStyleSheet("color: blue; font-family: monospace;");
	m_volumeLevelsLabel->setVisible(false);
	m_layout->addWidget(m_volumeLevelsLabel);

	// Spinner (initially hidden)
	m_spinner = new QProgressBar(this);
	m_spinner->setRange(0, 0); // Indeterminate progress
	m_spinner->setTextVisible(false);
	m_spinner->setVisible(false);
	m_layout->addWidget(m_spinner);

	m_spinnerLabel = new QLabel("", this);
	m_spinnerLabel->setStyleSheet("color: gray;");
	m_spinnerLabel->setVisible(false);
	m_layout->addWidget(m_spinnerLabel);

	// Connect signals
	connect(m_recordingList, &QListWidget::itemSelectionChanged, this,
		[this]() { m_startSyncButton->setEnabled(m_recordingList->currentItem() != nullptr); });
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this, &AudioSyncPanel::onRecordingSelected);
	connect(m_refreshButton, &QPushButton::clicked, this, &AudioSyncPanel::onRefreshClicked);
	connect(m_startSyncButton, &QPushButton::clicked, this, &AudioSyncPanel::onStartSyncClicked);
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
	m_refreshButton->setEnabled(false);

	// Start scanning in background thread
	QMetaObject::invokeMethod(m_scanWorker, "scanRecordings", Qt::QueuedConnection, Q_ARG(double, 15.0));
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
	m_statusLabel->setText(QString("Found %1 recordings").arg(m_recordingList->count()));
	m_refreshButton->setEnabled(true);
	m_startSyncButton->setEnabled(m_recordingList->currentItem() != nullptr);
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
	onStartSyncClicked();
}

void AudioSyncPanel::onStartSyncClicked()
{
	QListWidgetItem *item = m_recordingList->currentItem();
	if (item == nullptr) {
		return;
	}

	QString const FILE_PATH = item->data(Qt::UserRole).toString();
	auto *modal = new AudioSyncModal(FILE_PATH, this);
	modal->exec();
	delete modal;
}

void AudioSyncPanel::onRefreshClicked()
{
	refreshRecordings();
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
	m_refreshButton->setEnabled(false);
	m_startSyncButton->setEnabled(false);
	m_statusLabel->setText("Recording... (Collecting baseline)");
	m_statusLabel->setStyleSheet("color: orange;");
	m_volumeLevelsLabel->setVisible(true);
	m_volumeLevelsLabel->setText(
		"Current: -- | Baseline: collecting... | Threshold: --\nMin: -- | Max: -- | Avg: --");

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
	QTimer::singleShot(2100, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			m_statusLabel->setText("Recording... (Waiting for clap)");
		}
	});

	// Set timeout (30 seconds)
	QTimer::singleShot(30000, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			// Timeout reached - stop without modal
			stopAutoSyncRecording(false);
			QMessageBox::information(this, "Auto Sync Timeout",
						 "No audio spike detected within 30 seconds. Recording stopped.");
		}
	});
}

void AudioSyncPanel::stopAutoSyncRecording(bool openModal)
{
	if (m_autoSyncState == AutoSyncState::Idle) {
		return;
	}

	// Store whether we should open modal
	bool shouldOpenModal = openModal || (m_autoSyncState == AutoSyncState::PostSpike);

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
	m_refreshButton->setEnabled(true);
	m_statusLabel->setText(shouldOpenModal ? "Stopping recording..." : "Recording cancelled");
	m_statusLabel->setStyleSheet("color: gray;");
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
	m_statusLabel->setText("Clap detected! Recording 2 more seconds...");
	m_statusLabel->setStyleSheet("color: green;");
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
	if (m_autoSyncState != AutoSyncState::Recording && m_autoSyncState != AutoSyncState::PostSpike) {
		return;
	}

	QString text;
	if (baseline > 0.0) {
		// Baseline collected, show all values
		double currentPercent = threshold > 0.0 ? (current / threshold * 100.0) : 0.0;
		text = QString("Current: %1 (%2%) | Baseline: %3 | Threshold: %4\nMin: %5 | Max: %6 | Avg: %7")
			       .arg(current, 0, 'f', 6)
			       .arg(currentPercent, 0, 'f', 1)
			       .arg(baseline, 0, 'f', 6)
			       .arg(threshold, 0, 'f', 6)
			       .arg(minVol, 0, 'f', 6)
			       .arg(maxVol, 0, 'f', 6)
			       .arg(avgVol, 0, 'f', 6);
	} else {
		// Still collecting baseline
		text = QString("Current: %1 | Baseline: collecting... | Threshold: --\nMin: %2 | Max: %3 | Avg: %4")
			       .arg(current, 0, 'f', 6)
			       .arg(minVol, 0, 'f', 6)
			       .arg(maxVol, 0, 'f', 6)
			       .arg(avgVol, 0, 'f', 6);
	}
	m_volumeLevelsLabel->setText(text);
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
		m_statusLabel->setStyleSheet("color: gray;");
	});
}
