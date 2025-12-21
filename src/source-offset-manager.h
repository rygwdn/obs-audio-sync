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

#include <QString>
#include <QList>
#include <qtmetamacros.h>
#include <qobject.h>

// Forward declaration
struct obs_source;
typedef struct obs_source obs_source_t;

struct SourceInfo {
	QString name;
	QString id;
	bool isAudio;
	bool isVideo;
	int currentOffsetMs;
	bool hasAsyncDelayFilter;
	QString videoDelayFilterType; // "async_delay_filter" or "gpu_delay" or empty if none
};

class SourceOffsetManager : public QObject {
	Q_OBJECT

public:
	explicit SourceOffsetManager(QObject *parent = nullptr);
	~SourceOffsetManager() override = default;

	// Delete copy and move constructors/assignments
	SourceOffsetManager(const SourceOffsetManager &) = delete;
	SourceOffsetManager &operator=(const SourceOffsetManager &) = delete;
	SourceOffsetManager(SourceOffsetManager &&) = delete;
	SourceOffsetManager &operator=(SourceOffsetManager &&) = delete;

	// Enumerate all sources with async delay filter
	QList<SourceInfo> enumerateSourcesWithAsyncDelay();

	// Get source info by name
	SourceInfo getSourceInfo(const QString &sourceName);

	// Get current offset for a source (in milliseconds)
	int getSourceOffset(const QString &sourceName);

	// Set source offset (in milliseconds)
	// If asDelta is true, adds offsetMs to current offset; otherwise sets absolute value
	// If applyToAudio is true, applies to audio; if false, applies to video
	// For sources with both audio and video, this parameter determines which to update
	bool setSourceOffset(const QString &sourceName, int offsetMs, bool asDelta = false, bool applyToAudio = true);

	// Get list of audio sources with async delay filter
	QList<SourceInfo> getAudioSources();

	// Get list of video sources with video delay filters (Video Delay (Async) or Render Delay)
	QList<SourceInfo> getVideoSources();
};
