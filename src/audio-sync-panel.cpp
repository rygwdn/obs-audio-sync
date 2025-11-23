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
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>

AudioSyncPanel::AudioSyncPanel(QWidget *parent) : QWidget(parent)
{
	setupUI();
	refreshRecordings();
}

AudioSyncPanel::~AudioSyncPanel() {}

void AudioSyncPanel::setupUI()
{
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(10, 10, 10, 10);
	m_layout->setSpacing(10);

	// Title
	QLabel *titleLabel = new QLabel("Audio Sync", this);
	QFont titleFont = titleLabel->font();
	titleFont.setPointSize(14);
	titleFont.setBold(true);
	titleLabel->setFont(titleFont);
	m_layout->addWidget(titleLabel);

	// Recording list label
	QLabel *listLabel = new QLabel("Recordings (< 15s):", this);
	m_layout->addWidget(listLabel);

	// Recording list
	m_recordingList = new QListWidget(this);
	m_recordingList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_layout->addWidget(m_recordingList);

	// Refresh button
	m_refreshButton = new QPushButton("Refresh", this);
	m_layout->addWidget(m_refreshButton);

	// Status label
	m_statusLabel = new QLabel("Ready", this);
	m_statusLabel->setStyleSheet("color: gray;");
	m_layout->addWidget(m_statusLabel);

	// Connect signals
	connect(m_recordingList, &QListWidget::itemDoubleClicked, this,
		&AudioSyncPanel::onRecordingSelected);
	connect(m_refreshButton, &QPushButton::clicked, this,
		&AudioSyncPanel::onRefreshClicked);
}

void AudioSyncPanel::refreshRecordings()
{
	m_statusLabel->setText("Scanning recordings...");
	m_recordingList->clear();

	scanRecordings();

	m_statusLabel->setText(QString("Found %1 recordings").arg(m_recordingList->count()));
}

void AudioSyncPanel::scanRecordings()
{
	RecordingScanner scanner;
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

void AudioSyncPanel::onRecordingSelected(QListWidgetItem *item)
{
	if (!item) {
		return;
	}

	QString filePath = item->data(Qt::UserRole).toString();
	m_statusLabel->setText(QString("Analyzing: %1...").arg(QFileInfo(filePath).fileName()));

	// Analyze audio and find spike
	AudioAnalyzer analyzer;
	AudioSpike spike;
	if (analyzer.analyzeFile(filePath, spike)) {
		m_statusLabel->setText(QString("Spike found at %1s (amplitude: %2)")
					  .arg(spike.timestamp, 0, 'f', 3)
					  .arg(spike.amplitude, 0, 'f', 3));
		// TODO: Phase 3 - Display timeline and video frames
	} else {
		m_statusLabel->setText("Failed to analyze audio");
		QMessageBox::warning(this, "Analysis Failed",
				      "Could not analyze audio from recording.");
	}
}

void AudioSyncPanel::onRefreshClicked()
{
	refreshRecordings();
}
