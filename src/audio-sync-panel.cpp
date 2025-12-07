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
#include <util/base.h>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <qwidget.h>
#include <qdockwidget.h>
#include <qboxlayout.h>
#include <qfont.h>
#include <qlistwidget.h>
#include <qabstractitemview.h>
#include <qpushbutton.h>
#include <qnamespace.h>
#include <qlist.h>
#include <qprogressbar.h>
#include <qthread.h>
#include <qtimer.h>
#include <qlabel.h>

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle("Audio Sync");

	// Create central widget for QDockWidget
	QWidget *centralWidget = new QWidget(this); // NOLINT(cppcoreguidelines-init-variables)
	setWidget(centralWidget);

	// Initialize auto-sync state
	m_autoSyncState = AutoSyncState::Idle;
	m_audioMonitor = new RealTimeAudioMonitor(this);
	connect(m_audioMonitor, &RealTimeAudioMonitor::spikeDetected, this, &AudioSyncPanel::onSpikeDetected);
	connect(m_audioMonitor, &RealTimeAudioMonitor::monitoringError, this, &AudioSyncPanel::onMonitoringError);

	// Setup countdown timer
	m_countdownTimer = new QTimer(this);
	m_countdownTimer->setInterval(1000); // 1 second
	connect(m_countdownTimer, &QTimer::timeout, this, &AudioSyncPanel::onCountdownTick);

	setupUI();
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
	if (m_audioMonitor) {
		disconnect(m_audioMonitor, nullptr, this, nullptr);
	}
	if (m_countdownTimer) {
		m_countdownTimer->stop();
		disconnect(m_countdownTimer, nullptr, this, nullptr);
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

	// Recording list (styled list items)
	m_recordingList = new QListWidget(centralWidget);
	m_recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recordingList->setMaximumHeight(200);
	m_recordingList->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_recordingList->setItemDelegate(new HtmlListDelegate(m_recordingList));
	m_layout->addWidget(m_recordingList);

	// Button layout
	auto *buttonLayout = new QHBoxLayout();
	m_refreshButton = new QPushButton("Refresh", centralWidget);
	buttonLayout->addWidget(m_refreshButton);
	m_startSyncButton = new QPushButton("Start Sync", centralWidget);
	m_startSyncButton->setEnabled(false);
	buttonLayout->addWidget(m_startSyncButton);
	m_autoSyncButton = new QPushButton("Auto Sync", centralWidget);
	m_autoSyncButton->setToolTip("Start recording, detect clap, and automatically analyze");
	buttonLayout->addWidget(m_autoSyncButton);
	m_layout->addLayout(buttonLayout);

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
	connect(m_recordingList, &QListWidget::itemSelectionChanged, this,
		[this]() { m_startSyncButton->setEnabled(m_recordingList->currentItem() != nullptr); });
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this, &AudioSyncPanel::onRecordingSelected);
	connect(m_refreshButton, &QPushButton::clicked, this, &AudioSyncPanel::onRefreshClicked);
	connect(m_startSyncButton, &QPushButton::clicked, this, &AudioSyncPanel::onStartSyncClicked);
	connect(m_autoSyncButton, &QPushButton::clicked, this, &AudioSyncPanel::onAutoSyncClicked);

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
		// Cancel if already recording
		if (m_autoSyncState == AutoSyncState::Recording || m_autoSyncState == AutoSyncState::Monitoring) {
			stopAutoSyncRecording();
		}
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

	// Get recording directory to predict file path
	const char *recordingPath = obs_frontend_get_current_record_output_path();
	if (!recordingPath || strlen(recordingPath) == 0) {
		QMessageBox::warning(this, "Recording Path Error",
				     "Could not determine recording path. Please check OBS recording settings.");
		return;
	}

	QString path = QString::fromUtf8(recordingPath);

	// Expand ~ to home directory
	if (path.startsWith("~/")) {
		path = QDir::homePath() + path.mid(1);
	} else if (path == "~") {
		path = QDir::homePath();
	}

	// Normalize the path
	path = QDir::cleanPath(path);

	QFileInfo pathInfo(path);
	QString recordingDir;
	if (pathInfo.isDir()) {
		recordingDir = pathInfo.absoluteFilePath();
	} else {
		recordingDir = pathInfo.absolutePath();
	}
	QString baseName = pathInfo.baseName();

	// Store expected recording path pattern (OBS will create the file)
	m_autoSyncRecordingPath = recordingDir + "/" + baseName;
	if (m_autoSyncRecordingPath.isEmpty()) {
		m_autoSyncRecordingPath = recordingDir;
	}

	// Start recording
	obs_frontend_recording_start();

	// Update state
	m_autoSyncState = AutoSyncState::Recording;
	m_spikeTimestamp = 0.0;
	m_countdownSeconds = 3;

	// Update UI
	m_autoSyncButton->setText("Cancel Auto Sync");
	m_autoSyncButton->setEnabled(true);
	m_refreshButton->setEnabled(false);
	m_startSyncButton->setEnabled(false);
	m_statusLabel->setText("Recording... (Waiting for clap)");
	m_statusLabel->setStyleSheet("color: orange;");

	// Start monitoring after a short delay to allow file creation
	QTimer::singleShot(1000, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			// Try to get the actual recording file path
			const char *currentPath = obs_frontend_get_current_record_output_path();
			if (currentPath && strlen(currentPath) > 0) {
				QString path = QString::fromUtf8(currentPath);

				// Expand ~ to home directory
				if (path.startsWith("~/")) {
					path = QDir::homePath() + path.mid(1);
				} else if (path == "~") {
					path = QDir::homePath();
				}

				// Normalize the path
				path = QDir::cleanPath(path);

				m_autoSyncRecordingPath = path;
				m_audioMonitor->startMonitoring(m_autoSyncRecordingPath);
			} else {
				// Fallback: try to find the newest file in the directory
				QFileInfo pathInfo(m_autoSyncRecordingPath);
				QString dir = pathInfo.absolutePath();
				QDir directory(dir);
				QStringList filters = {"*.mkv", "*.mp4", "*.flv", "*.mov", "*.avi", "*.webm"};
				QFileInfoList files =
					directory.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);
				if (!files.isEmpty()) {
					m_autoSyncRecordingPath = files.last().absoluteFilePath();
					m_audioMonitor->startMonitoring(m_autoSyncRecordingPath);
				}
			}
		}
	});

	// Set timeout (30 seconds)
	QTimer::singleShot(30000, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			// Timeout reached
			stopAutoSyncRecording();
			QMessageBox::information(this, "Auto Sync Timeout",
						 "No audio spike detected within 30 seconds. Recording stopped.");
		}
	});
}

void AudioSyncPanel::stopAutoSyncRecording()
{
	if (m_autoSyncState == AutoSyncState::Idle) {
		return;
	}

	// Stop monitoring
	m_audioMonitor->stopMonitoring();
	m_countdownTimer->stop();

	// Stop recording if still active
	if (obs_frontend_recording_active()) {
		obs_frontend_recording_stop();
		m_autoSyncState = AutoSyncState::Stopping;
	} else {
		m_autoSyncState = AutoSyncState::Idle;
	}

	// Update UI
	m_autoSyncButton->setText("Auto Sync");
	m_autoSyncButton->setEnabled(true);
	m_refreshButton->setEnabled(true);
	m_statusLabel->setText("Stopping recording...");
	m_statusLabel->setStyleSheet("color: gray;");
}

void AudioSyncPanel::onSpikeDetected(double timestamp)
{
	if (m_autoSyncState != AutoSyncState::Recording) {
		return;
	}

	m_spikeTimestamp = timestamp;
	m_autoSyncState = AutoSyncState::Monitoring;
	m_countdownSeconds = 3;

	// Update UI
	m_statusLabel->setText(QString("Spike detected! Stopping in %1s...").arg(m_countdownSeconds));
	m_statusLabel->setStyleSheet("color: green;");

	// Start countdown
	m_countdownTimer->start();
}

void AudioSyncPanel::onCountdownTick()
{
	m_countdownSeconds--;
	if (m_countdownSeconds > 0) {
		m_statusLabel->setText(QString("Spike detected! Stopping in %1s...").arg(m_countdownSeconds));
	} else {
		// Countdown finished, stop recording
		m_countdownTimer->stop();
		stopAutoSyncRecording();
	}
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
