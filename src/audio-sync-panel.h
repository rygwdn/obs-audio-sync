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

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

class AudioSyncPanel : public QWidget {
	Q_OBJECT

public:
	explicit AudioSyncPanel(QWidget *parent = nullptr);
	~AudioSyncPanel();

	void refreshRecordings();

private slots:
	void onRecordingSelected(QListWidgetItem *item);
	void onRefreshClicked();

private:
	void setupUI();
	void scanRecordings();

	QListWidget *m_recordingList;
	QLabel *m_statusLabel;
	QPushButton *m_refreshButton;
	QVBoxLayout *m_layout;
};
