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

#ifndef AUDIO_SYNC_PANEL_H
#define AUDIO_SYNC_PANEL_H

#include <QWidget>
#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <qtmetamacros.h>
#include <qobject.h>
#include "recording-scanner.h"
#include "recording-scanner-worker.h"
#include "audio-sync-modal.h"
#include "source-offset-manager.h"
#include "realtime-audio-monitor.h"
#include <QStyledItemDelegate>
#include <QPainter>
#include <QTextDocument>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QApplication>
#include <QStyle>

class HtmlListDelegate : public QStyledItemDelegate {
public:
	explicit HtmlListDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		// Draw selection/hover background using the style
		QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
		style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

		// Draw HTML text
		QTextDocument doc;
		doc.setHtml(opt.text);
		doc.setTextWidth(opt.rect.width());

		painter->save();
		painter->translate(opt.rect.topLeft());
		doc.drawContents(painter);
		painter->restore();
	}

	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		QTextDocument doc;
		doc.setHtml(opt.text);
		// Use a reasonable width for size calculation if rect width is invalid
		if (opt.rect.width() > 0) {
			doc.setTextWidth(opt.rect.width());
		} else {
			doc.setTextWidth(200); // Default width for calculation
		}

		// Calculate height based on font metrics for a single line, with minimal padding
		QFontMetrics fm(opt.font);
		int lineHeight = fm.height();
		// Add small padding (2px top + 2px bottom) instead of using document height
		int itemHeight = lineHeight + 4;

		return QSize(static_cast<int>(doc.idealWidth()), itemHeight);
	}
};

class AudioSyncPanel : public QDockWidget {
	Q_OBJECT

public:
	explicit AudioSyncPanel(QWidget *parent = nullptr);
	~AudioSyncPanel() override;

	// Delete copy and move constructors/assignments
	AudioSyncPanel(const AudioSyncPanel &) = delete;
	AudioSyncPanel &operator=(const AudioSyncPanel &) = delete;
	AudioSyncPanel(AudioSyncPanel &&) = delete;
	AudioSyncPanel &operator=(AudioSyncPanel &&) = delete;

public slots:
	void refreshRecordings();
	void scheduleDelayedRefresh();

private slots:
	void onRecordingSelected(QListWidgetItem *item);
	void onRefreshClicked();
	void onStartSyncClicked();
	void onAutoSyncClicked();
	// Worker slots
	void onRecordingsScanned(const QList<RecordingInfo> &recordings);
	void onScanError(const QString &error);
	// Real-time monitoring slots
	void onSpikeDetected(double timestamp);
	void onRecordingComplete(double spikeTimestamp);
	void onVolumeLevelsUpdated(double baseline, double current, double threshold);
	void onMonitoringError(const QString &error);

private:
	void setupUI();
	void setupWorkerThreads();
	void showSpinner(const QString &message);
	void hideSpinner();
	void startAutoSyncRecording();
	void stopAutoSyncRecording();
	void handleAutoSyncRecordingStopped();

	QListWidget *m_recordingList{nullptr};
	QLabel *m_statusLabel{nullptr};
	QLabel *m_volumeLevelsLabel{nullptr}; // Volume levels display during auto sync
	QPushButton *m_refreshButton{nullptr};
	QPushButton *m_startSyncButton{nullptr};
	QPushButton *m_autoSyncButton{nullptr};
	QVBoxLayout *m_layout{nullptr};
	QProgressBar *m_spinner{nullptr};
	QLabel *m_spinnerLabel{nullptr};

	// Worker threads
	QThread *m_scanThread{nullptr};
	RecordingScannerWorker *m_scanWorker{nullptr};

	// Timer for delayed refresh after recording events
	QTimer *m_refreshTimer{nullptr};

	// Auto-sync recording state
	enum class AutoSyncState {
		Idle,
		Recording,  // Baseline collection + spike detection
		PostSpike,  // After spike detected, waiting 2 seconds
		Stopping
	};
	AutoSyncState m_autoSyncState{AutoSyncState::Idle};
	QString m_autoSyncRecordingPath;
	RealTimeAudioMonitor *m_audioMonitor{nullptr};
	double m_spikeTimestamp{0.0};
};

#endif // AUDIO_SYNC_PANEL_H
