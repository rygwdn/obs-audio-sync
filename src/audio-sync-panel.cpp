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
	connect(m_audioMonitor, &RealTimeAudioMonitor::recordingComplete, this, &AudioSyncPanel::onRecordingComplete);
	connect(m_audioMonitor, &RealTimeAudioMonitor::volumeLevelsUpdated, this,
		&AudioSyncPanel::onVolumeLevelsUpdated);
	connect(m_audioMonitor, &RealTimeAudioMonitor::monitoringError, this, &AudioSyncPanel::onMonitoringError);

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

	// Volume levels label (initially hidden)
	m_volumeLevelsLabel = new QLabel("", centralWidget);
	m_volumeLevelsLabel->setStyleSheet("color: blue; font-family: monospace;");
	m_volumeLevelsLabel->setVisible(false);
	m_layout->addWidget(m_volumeLevelsLabel);

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
		if (m_autoSyncState == AutoSyncState::Recording || m_autoSyncState == AutoSyncState::PostSpike) {
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
	m_autoSyncButton->setText("Cancel Auto Sync");
	m_autoSyncButton->setEnabled(true);
	m_refreshButton->setEnabled(false);
	m_startSyncButton->setEnabled(false);
	m_statusLabel->setText("Recording... (Collecting baseline)");
	m_statusLabel->setStyleSheet("color: orange;");
	m_volumeLevelsLabel->setVisible(true);
	m_volumeLevelsLabel->setText("Volume: -- | Baseline: -- | Threshold: --");

	// Start monitoring after a short delay to allow file creation
	QTimer::singleShot(1000, this, [this]() {
		if (m_autoSyncState == AutoSyncState::Recording) {
			blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Attempting to find recording file...");

			// Try to get the actual recording file path
			const char *currentPath = obs_frontend_get_current_record_output_path();
			QString pathToCheck;

			if (currentPath && strlen(currentPath) > 0) {
				pathToCheck = QString::fromUtf8(currentPath);
				blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: OBS returned path: %s",
				     pathToCheck.toUtf8().constData());

				// Expand ~ to home directory
				if (pathToCheck.startsWith("~/")) {
					pathToCheck = QDir::homePath() + pathToCheck.mid(1);
				} else if (pathToCheck == "~") {
					pathToCheck = QDir::homePath();
				}

				// Normalize the path
				pathToCheck = QDir::cleanPath(pathToCheck);
			}

			QFileInfo pathInfo(pathToCheck.isEmpty() ? m_autoSyncRecordingPath : pathToCheck);
			QString dir = pathInfo.isDir() ? pathInfo.absoluteFilePath() : pathInfo.absolutePath();
			blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Checking directory: %s",
			     dir.toUtf8().constData());

			// Always try to find the newest file in the directory (most reliable)
			QDir directory(dir);
			if (!directory.exists()) {
				blog(LOG_WARNING, "[AudioSync] startAutoSyncRecording: Directory does not exist: %s",
				     dir.toUtf8().constData());
				return;
			}

			QStringList filters = {"*.mkv", "*.mp4", "*.flv", "*.mov", "*.avi", "*.webm"};
			QFileInfoList files =
				directory.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);

			blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Found %lld video files in directory",
			     static_cast<long long>(files.size()));

			if (!files.isEmpty()) {
				// Get the newest file (first in list when sorted by time reversed)
				QString newestFile = files.first().absoluteFilePath();
				blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Using newest file: %s",
				     newestFile.toUtf8().constData());

				m_autoSyncRecordingPath = newestFile;
				m_audioMonitor->startMonitoring(m_autoSyncRecordingPath);
				blog(LOG_INFO, "[AudioSync] startAutoSyncRecording: Started monitoring file");

				// Update UI after monitoring starts (baseline collection phase)
				QTimer::singleShot(2100, this, [this]() {
					if (m_autoSyncState == AutoSyncState::Recording) {
						m_statusLabel->setText("Recording... (Waiting for clap)");
					}
				});
			} else {
				blog(LOG_WARNING,
				     "[AudioSync] startAutoSyncRecording: No video files found in directory, will retry");
				// Retry after another second
				QTimer::singleShot(1000, this, [this]() {
					if (m_autoSyncState == AutoSyncState::Recording) {
						// Retry finding the file
						QFileInfo pathInfo(m_autoSyncRecordingPath);
						QString dir = pathInfo.isDir() ? pathInfo.absoluteFilePath()
									       : pathInfo.absolutePath();
						QDir directory(dir);
						QStringList filters = {"*.mkv", "*.mp4", "*.flv",
								       "*.mov", "*.avi", "*.webm"};
						QFileInfoList files = directory.entryInfoList(
							filters, QDir::Files, QDir::Time | QDir::Reversed);
						if (!files.isEmpty()) {
							m_autoSyncRecordingPath = files.first().absoluteFilePath();
							blog(LOG_INFO,
							     "[AudioSync] startAutoSyncRecording: Retry found file: %s",
							     m_autoSyncRecordingPath.toUtf8().constData());
							m_audioMonitor->startMonitoring(m_autoSyncRecordingPath);
						}
					}
				});
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
	m_volumeLevelsLabel->setVisible(false);
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

	// Stop recording immediately
	stopAutoSyncRecording();
}

void AudioSyncPanel::onVolumeLevelsUpdated(double baseline, double current, double threshold)
{
	if (m_autoSyncState != AutoSyncState::Recording && m_autoSyncState != AutoSyncState::PostSpike) {
		return;
	}

	QString text;
	if (baseline > 0.0) {
		// Baseline collected, show all values
		double currentPercent = threshold > 0.0 ? (current / threshold * 100.0) : 0.0;
		text = QString("Volume: %1 (%.1f%%) | Baseline: %2 | Threshold: %3")
			       .arg(current, 0, 'f', 6)
			       .arg(currentPercent)
			       .arg(baseline, 0, 'f', 6)
			       .arg(threshold, 0, 'f', 6);
	} else {
		// Still collecting baseline
		text = QString("Volume: %1 | Baseline: collecting... | Threshold: --").arg(current, 0, 'f', 6);
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
