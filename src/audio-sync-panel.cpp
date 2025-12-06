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
#include "source-offset-manager.h"
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
#include <qprogressbar.h>
#include <qthread.h>
#include <qtimer.h>
#include <qcombobox.h>
#include <qlabel.h>

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle("Audio Sync");

	// Create central widget for QDockWidget
	QWidget *centralWidget = new QWidget(this); // NOLINT(cppcoreguidelines-init-variables)
	setWidget(centralWidget);

	// Create source offset manager
	m_sourceOffsetManager = new SourceOffsetManager(this);

	setupUI();
	refreshRecordings();
	setupSourceSelection();
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
	if (m_audioSourceCombo) {
		disconnect(m_audioSourceCombo, nullptr, this, nullptr);
	}
	if (m_videoSourceCombo) {
		disconnect(m_videoSourceCombo, nullptr, this, nullptr);
	}
	if (m_refreshSourcesButton) {
		disconnect(m_refreshSourcesButton, nullptr, this, nullptr);
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
	m_layout->addWidget(m_recordingList);

	// Button layout
	auto *buttonLayout = new QHBoxLayout();
	m_refreshButton = new QPushButton("Refresh", centralWidget);
	buttonLayout->addWidget(m_refreshButton);
	m_startSyncButton = new QPushButton("Start Sync", centralWidget);
	m_startSyncButton->setEnabled(false);
	buttonLayout->addWidget(m_startSyncButton);
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

	// Setup refresh timer for delayed refresh after recording events
	// This ensures the file is ready after muxing completes
	m_refreshTimer = new QTimer(this);
	m_refreshTimer->setSingleShot(true);
	m_refreshTimer->setInterval(500); // 500ms delay to ensure file is ready
	connect(m_refreshTimer, &QTimer::timeout, this, &AudioSyncPanel::refreshRecordings);

	// Setup worker threads
	setupWorkerThreads();
}

void AudioSyncPanel::setupSourceSelection()
{
	QWidget *centralWidget = widget();

	// Audio source selection
	auto *audioSourceLayout = new QHBoxLayout();
	auto *audioLabel = new QLabel("Audio Source:", centralWidget);
	audioSourceLayout->addWidget(audioLabel);
	m_audioSourceCombo = new QComboBox(centralWidget);
	m_audioSourceCombo->setToolTip("Select an audio source with Async Delay filter");
	audioSourceLayout->addWidget(m_audioSourceCombo);
	m_audioOffsetLabel = new QLabel("", centralWidget);
	m_audioOffsetLabel->setStyleSheet("color: gray;");
	m_audioOffsetLabel->setMinimumWidth(120);
	audioSourceLayout->addWidget(m_audioOffsetLabel);
	m_layout->addLayout(audioSourceLayout);

	// Video source selection
	auto *videoSourceLayout = new QHBoxLayout();
	auto *videoLabel = new QLabel("Video Source:", centralWidget);
	videoSourceLayout->addWidget(videoLabel);
	m_videoSourceCombo = new QComboBox(centralWidget);
	m_videoSourceCombo->setToolTip("Select a video source with Async Delay filter");
	videoSourceLayout->addWidget(m_videoSourceCombo);
	m_videoOffsetLabel = new QLabel("", centralWidget);
	m_videoOffsetLabel->setStyleSheet("color: gray;");
	m_videoOffsetLabel->setMinimumWidth(120);
	videoSourceLayout->addWidget(m_videoOffsetLabel);
	m_layout->addLayout(videoSourceLayout);

	// Refresh sources button
	m_refreshSourcesButton = new QPushButton("Refresh Sources", centralWidget);
	m_refreshSourcesButton->setToolTip("Refresh the list of available sources");
	m_layout->addWidget(m_refreshSourcesButton);

	// Connect signals
	connect(m_audioSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&AudioSyncPanel::onAudioSourceChanged);
	connect(m_videoSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&AudioSyncPanel::onVideoSourceChanged);
	connect(m_refreshSourcesButton, &QPushButton::clicked, this, &AudioSyncPanel::onRefreshSourcesClicked);

	// Initial refresh
	refreshSourceList();
}

void AudioSyncPanel::refreshSourceList()
{
	if (!m_sourceOffsetManager) {
		return;
	}

	// Store current selections
	QString currentAudioSource = m_audioSourceCombo->currentText();
	QString currentVideoSource = m_videoSourceCombo->currentText();

	// Clear and repopulate audio sources
	m_audioSourceCombo->clear();
	m_audioSourceCombo->addItem("(None)", QVariant());
	QList<SourceInfo> audioSources = m_sourceOffsetManager->getAudioSources();
	for (const SourceInfo &info : audioSources) {
		m_audioSourceCombo->addItem(info.name, QVariant(info.name));
	}

	// Restore audio selection if still available
	int audioIndex = m_audioSourceCombo->findText(currentAudioSource);
	if (audioIndex >= 0) {
		m_audioSourceCombo->setCurrentIndex(audioIndex);
	}

	// Clear and repopulate video sources
	m_videoSourceCombo->clear();
	m_videoSourceCombo->addItem("(None)", QVariant());
	QList<SourceInfo> videoSources = m_sourceOffsetManager->getVideoSources();
	for (const SourceInfo &info : videoSources) {
		m_videoSourceCombo->addItem(info.name, QVariant(info.name));
	}

	// Restore video selection if still available
	int videoIndex = m_videoSourceCombo->findText(currentVideoSource);
	if (videoIndex >= 0) {
		m_videoSourceCombo->setCurrentIndex(videoIndex);
	}

	// Update offset displays
	updateOffsetDisplay();
}

void AudioSyncPanel::updateOffsetDisplay()
{
	if (!m_sourceOffsetManager) {
		return;
	}

	// Update audio offset
	QString audioSource = m_audioSourceCombo->currentData().toString();
	if (audioSource.isEmpty()) {
		m_audioOffsetLabel->setText("");
	} else {
		int offsetMs = m_sourceOffsetManager->getSourceOffset(audioSource);
		QString offsetText = QString("%1%2ms").arg(offsetMs >= 0 ? "+" : "").arg(offsetMs);
		m_audioOffsetLabel->setText(QString("Current: %1").arg(offsetText));
	}

	// Update video offset
	QString videoSource = m_videoSourceCombo->currentData().toString();
	if (videoSource.isEmpty()) {
		m_videoOffsetLabel->setText("");
	} else {
		int offsetMs = m_sourceOffsetManager->getSourceOffset(videoSource);
		QString offsetText = QString("%1%2ms").arg(offsetMs >= 0 ? "+" : "").arg(offsetMs);
		m_videoOffsetLabel->setText(QString("Current: %1").arg(offsetText));
	}
}

void AudioSyncPanel::onAudioSourceChanged(int index)
{
	Q_UNUSED(index);
	updateOffsetDisplay();
}

void AudioSyncPanel::onVideoSourceChanged(int index)
{
	Q_UNUSED(index);
	updateOffsetDisplay();
}

void AudioSyncPanel::onRefreshSourcesClicked()
{
	refreshSourceList();
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
}
