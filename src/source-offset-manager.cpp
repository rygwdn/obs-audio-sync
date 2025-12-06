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

#include "source-offset-manager.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QDebug>
#include <qlogging.h>

SourceOffsetManager::SourceOffsetManager(QObject *parent) : QObject(parent) {}

struct EnumData {
	QList<SourceInfo> *sources;
	SourceOffsetManager *manager;
};

static bool enumSourceCallback(void *param, obs_source_t *source)
{
	auto *data = static_cast<EnumData *>(param);
	if (source == nullptr) {
		return true; // Continue enumeration
	}

	// Check if source has async delay filter
	const char *sourceName = obs_source_get_name(source);
	if (sourceName == nullptr) {
		return true; // Continue enumeration
	}

	// Get source type and output flags
	uint32_t outputFlags = obs_source_get_output_flags(source);
	bool isAudio = (outputFlags & OBS_SOURCE_AUDIO) != 0;
	bool isVideo = (outputFlags & OBS_SOURCE_VIDEO) != 0;

	// Skip sources that don't have audio or video
	if (!isAudio && !isVideo) {
		return true; // Continue enumeration
	}

	// Check if source has async delay filter
	obs_source_t *filter = obs_source_get_filter_by_name(source, "Async Delay");
	if (filter == nullptr) {
		return true; // Continue enumeration - source doesn't have filter
	}

	// Get current offset from filter
	obs_data_t *settings = obs_source_get_settings(filter);
	int delayMs = obs_data_get_int(settings, "delay_ms");
	obs_data_release(settings);

	// Create source info
	SourceInfo info;
	info.name = QString::fromUtf8(sourceName);
	info.id = QString::fromUtf8(obs_source_get_id(source));
	info.isAudio = isAudio;
	info.isVideo = isVideo;
	info.currentOffsetMs = delayMs;
	info.hasAsyncDelayFilter = true;

	data->sources->append(info);

	return true; // Continue enumeration
}

QList<SourceInfo> SourceOffsetManager::enumerateSourcesWithAsyncDelay()
{
	QList<SourceInfo> sources;

	// Enumerate all sources
	EnumData data;
	data.sources = &sources;
	data.manager = this;

	obs_enum_sources(enumSourceCallback, &data);

	return sources;
}

SourceInfo SourceOffsetManager::getSourceInfo(const QString &sourceName)
{
	SourceInfo info;
	info.name = sourceName;
	info.hasAsyncDelayFilter = false;
	info.currentOffsetMs = 0;

	// Find source by name
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (source == nullptr) {
		return info;
	}

	// Get source type and output flags
	uint32_t outputFlags = obs_source_get_output_flags(source);
	info.isAudio = (outputFlags & OBS_SOURCE_AUDIO) != 0;
	info.isVideo = (outputFlags & OBS_SOURCE_VIDEO) != 0;
	info.id = QString::fromUtf8(obs_source_get_id(source));

	// Check if source has async delay filter
	obs_source_t *filter = obs_source_get_filter_by_name(source, "Async Delay");
	if (filter != nullptr) {
		info.hasAsyncDelayFilter = true;
		obs_data_t *settings = obs_source_get_settings(filter);
		info.currentOffsetMs = obs_data_get_int(settings, "delay_ms");
		obs_data_release(settings);
	}

	obs_source_release(source);
	return info;
}

int SourceOffsetManager::getSourceOffset(const QString &sourceName)
{
	SourceInfo info = getSourceInfo(sourceName);
	return info.hasAsyncDelayFilter ? info.currentOffsetMs : 0;
}

bool SourceOffsetManager::setSourceOffset(const QString &sourceName, int offsetMs, bool asDelta)
{
	// Find source by name
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (source == nullptr) {
		qWarning() << "SourceOffsetManager::setSourceOffset: Source not found:" << sourceName;
		return false;
	}

	// Get async delay filter
	obs_source_t *filter = obs_source_get_filter_by_name(source, "Async Delay");
	if (filter == nullptr) {
		obs_source_release(source);
		qWarning() << "SourceOffsetManager::setSourceOffset: Source does not have Async Delay filter:"
			   << sourceName;
		return false;
	}

	// Get current settings
	obs_data_t *settings = obs_source_get_settings(filter);
	int currentOffsetMs = obs_data_get_int(settings, "delay_ms");

	// Calculate new offset
	int newOffsetMs = asDelta ? (currentOffsetMs + offsetMs) : offsetMs;

	// Clamp to valid range (-20000 to 20000 ms)
	const int MIN_OFFSET_MS = -20000;
	const int MAX_OFFSET_MS = 20000;
	if (newOffsetMs < MIN_OFFSET_MS) {
		newOffsetMs = MIN_OFFSET_MS;
		qWarning() << "SourceOffsetManager::setSourceOffset: Clamped offset to minimum:" << MIN_OFFSET_MS;
	} else if (newOffsetMs > MAX_OFFSET_MS) {
		newOffsetMs = MAX_OFFSET_MS;
		qWarning() << "SourceOffsetManager::setSourceOffset: Clamped offset to maximum:" << MAX_OFFSET_MS;
	}

	// Update settings
	obs_data_set_int(settings, "delay_ms", newOffsetMs);
	obs_source_update(filter, settings);
	obs_data_release(settings);

	obs_source_release(source);
	return true;
}

QList<SourceInfo> SourceOffsetManager::getAudioSources()
{
	QList<SourceInfo> allSources = enumerateSourcesWithAsyncDelay();
	QList<SourceInfo> audioSources;

	for (const SourceInfo &info : allSources) {
		if (info.isAudio) {
			audioSources.append(info);
		}
	}

	return audioSources;
}

QList<SourceInfo> SourceOffsetManager::getVideoSources()
{
	QList<SourceInfo> allSources = enumerateSourcesWithAsyncDelay();
	QList<SourceInfo> videoSources;

	for (const SourceInfo &info : allSources) {
		if (info.isVideo) {
			videoSources.append(info);
		}
	}

	return videoSources;
}
