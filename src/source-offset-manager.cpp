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

static void enumFilterCallback(obs_source_t *source, obs_source_t *filter, void *param)
{
	(void)source; // Unused parameter
	if (filter == nullptr) {
		return;
	}
	const char *sourceName = static_cast<const char *>(param);
	const char *filterName = obs_source_get_name(filter);
	const char *filterId = obs_source_get_id(filter);
	bool enabled = obs_source_enabled(filter);
	blog(LOG_INFO, "[AudioSync] enumFilterCallback: Source %s - Filter: %s (id: %s, enabled: %d)",
	     sourceName ? sourceName : "(null)", filterName ? filterName : "(null)", filterId ? filterId : "(null)",
	     enabled);
}

// Helper callback for enumerating sources within groups
static bool enumGroupSourceCallback(void *param, obs_source_t *source)
{
	// Reuse the same enumSourceCallback logic
	return enumSourceCallback(param, source);
}

static bool enumSourceCallback(void *param, obs_source_t *source)
{
	auto *data = static_cast<EnumData *>(param);
	if (source == nullptr) {
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: source is nullptr, skipping");
		return true; // Continue enumeration
	}

	const char *sourceName = obs_source_get_name(source);
	if (sourceName == nullptr) {
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: source name is nullptr, skipping");
		return true; // Continue enumeration
	}

	QString sourceNameStr = QString::fromUtf8(sourceName);
	const char *sourceId = obs_source_get_id(source);
	bool isGroup = (sourceId != nullptr && strcmp(sourceId, "group") == 0);

	blog(LOG_INFO, "[AudioSync] enumSourceCallback: Checking source: %s (id: %s, isGroup: %d)",
	     sourceNameStr.toUtf8().constData(), sourceId ? sourceId : "(null)", isGroup);

	// If this is a group, recursively enumerate its children
	if (isGroup) {
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: Source %s is a group, enumerating children",
		     sourceNameStr.toUtf8().constData());
		obs_source_enum_active_sources(source, enumGroupSourceCallback, data);
		// Continue processing the group itself (groups can have filters too, though less common)
	}

	// Get source type and output flags
	uint32_t outputFlags = obs_source_get_output_flags(source);
	bool isAudio = (outputFlags & OBS_SOURCE_AUDIO) != 0;
	bool isVideo = (outputFlags & OBS_SOURCE_VIDEO) != 0;

	blog(LOG_INFO, "[AudioSync] enumSourceCallback: Source %s - isAudio=%d, isVideo=%d, outputFlags=0x%x",
	     sourceNameStr.toUtf8().constData(), isAudio, isVideo, outputFlags);

	// Skip sources that don't have audio or video
	if (!isAudio && !isVideo) {
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: Source %s skipped - no audio or video",
		     sourceNameStr.toUtf8().constData());
		return true; // Continue enumeration
	}

	// For audio sources: they have built-in sync offset, always include them
	if (isAudio) {
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: Source %s is audio source, adding to list",
		     sourceNameStr.toUtf8().constData());

		// Get current sync offset (in nanoseconds, convert to milliseconds)
		int64_t syncOffsetNs = obs_source_get_sync_offset(source);
		int syncOffsetMs = static_cast<int>(syncOffsetNs / 1000000LL);

		// Create source info
		SourceInfo info;
		info.name = sourceNameStr;
		info.id = QString::fromUtf8(obs_source_get_id(source));
		info.isAudio = true;
		info.isVideo = false;
		info.currentOffsetMs = syncOffsetMs;
		info.hasAsyncDelayFilter = false; // Audio uses built-in sync offset

		data->sources->append(info);
		return true; // Continue enumeration
	}

	// For video sources: check if they have Video Delay (Async) filter (even if disabled)
	if (isVideo) {
		// Enumerate all filters to log them
		blog(LOG_INFO, "[AudioSync] enumSourceCallback: Video source found: %s",
		     sourceNameStr.toUtf8().constData());

		// List all filters on this source
		QByteArray sourceNameBytes = sourceNameStr.toUtf8();
		obs_source_enum_filters(source, enumFilterCallback, const_cast<char *>(sourceNameBytes.constData()));

		obs_source_t *filter = obs_source_get_filter_by_name(source, "Video Delay (Async)");
		if (filter == nullptr) {
			blog(LOG_INFO,
			     "[AudioSync] enumSourceCallback: Source %s is video but has no Video Delay (Async) filter, skipping",
			     sourceNameStr.toUtf8().constData());
			return true; // Continue enumeration - video source needs filter
		}

		// Get current offset from filter
		obs_data_t *settings = obs_source_get_settings(filter);
		int delayMs = obs_data_get_int(settings, "delay_ms");
		obs_data_release(settings);

		blog(LOG_INFO,
		     "[AudioSync] enumSourceCallback: Source %s is video with Video Delay (Async) filter (offset: %d ms), adding to list",
		     sourceNameStr.toUtf8().constData(), delayMs);

		// Create source info
		SourceInfo info;
		info.name = sourceNameStr;
		info.id = QString::fromUtf8(obs_source_get_id(source));
		info.isAudio = false;
		info.isVideo = true;
		info.currentOffsetMs = delayMs;
		info.hasAsyncDelayFilter = true;

		data->sources->append(info);
		return true; // Continue enumeration
	}

	return true; // Continue enumeration
}

QList<SourceInfo> SourceOffsetManager::enumerateSourcesWithAsyncDelay()
{
	QList<SourceInfo> sources;

	blog(LOG_INFO, "[AudioSync] enumerateSourcesWithAsyncDelay: Starting source enumeration");

	// Enumerate all sources (this will recursively handle groups via enumSourceCallback)
	EnumData data;
	data.sources = &sources;
	data.manager = this;

	obs_enum_sources(enumSourceCallback, &data);

	int const SOURCE_COUNT = static_cast<int>(sources.size());
	blog(LOG_INFO,
	     "[AudioSync] enumerateSourcesWithAsyncDelay: Found %d sources (audio with built-in sync or video with Video Delay (Async) filter)",
	     SOURCE_COUNT);

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

	// For audio sources: get built-in sync offset
	if (info.isAudio) {
		int64_t syncOffsetNs = obs_source_get_sync_offset(source);
		info.currentOffsetMs = static_cast<int>(syncOffsetNs / 1000000LL);
		info.hasAsyncDelayFilter = false;
	} else if (info.isVideo) {
		// For video sources: check if source has async delay filter
		obs_source_t *filter = obs_source_get_filter_by_name(source, "Video Delay (Async)");
		if (filter != nullptr) {
			info.hasAsyncDelayFilter = true;
			obs_data_t *settings = obs_source_get_settings(filter);
			info.currentOffsetMs = obs_data_get_int(settings, "delay_ms");
			obs_data_release(settings);
		}
	}

	obs_source_release(source);
	return info;
}

int SourceOffsetManager::getSourceOffset(const QString &sourceName)
{
	SourceInfo info = getSourceInfo(sourceName);
	return info.currentOffsetMs;
}

bool SourceOffsetManager::setSourceOffset(const QString &sourceName, int offsetMs, bool asDelta)
{
	// Find source by name
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (source == nullptr) {
		qWarning() << "SourceOffsetManager::setSourceOffset: Source not found:" << sourceName;
		return false;
	}

	// Get source type
	uint32_t outputFlags = obs_source_get_output_flags(source);
	bool isAudio = (outputFlags & OBS_SOURCE_AUDIO) != 0;
	bool isVideo = (outputFlags & OBS_SOURCE_VIDEO) != 0;

	if (isAudio) {
		// For audio sources: use built-in sync offset (in nanoseconds)
		int64_t currentOffsetNs = obs_source_get_sync_offset(source);
		int currentOffsetMs = static_cast<int>(currentOffsetNs / 1000000LL);

		// Calculate new offset
		int newOffsetMs = asDelta ? (currentOffsetMs + offsetMs) : offsetMs;

		// Clamp to valid range (-20000 to 20000 ms)
		const int MIN_OFFSET_MS = -20000;
		const int MAX_OFFSET_MS = 20000;
		if (newOffsetMs < MIN_OFFSET_MS) {
			newOffsetMs = MIN_OFFSET_MS;
			qWarning()
				<< "SourceOffsetManager::setSourceOffset: Clamped offset to minimum:" << MIN_OFFSET_MS;
		} else if (newOffsetMs > MAX_OFFSET_MS) {
			newOffsetMs = MAX_OFFSET_MS;
			qWarning()
				<< "SourceOffsetManager::setSourceOffset: Clamped offset to maximum:" << MAX_OFFSET_MS;
		}

		// Convert to nanoseconds and set
		int64_t newOffsetNs = static_cast<int64_t>(newOffsetMs) * 1000000LL;
		obs_source_set_sync_offset(source, newOffsetNs);

		obs_source_release(source);
		return true;
	} else if (isVideo) {
		// For video sources: use Video Delay (Async) filter
		obs_source_t *filter = obs_source_get_filter_by_name(source, "Video Delay (Async)");
		if (filter == nullptr) {
			obs_source_release(source);
			qWarning()
				<< "SourceOffsetManager::setSourceOffset: Video source does not have Video Delay (Async) filter:"
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
			qWarning()
				<< "SourceOffsetManager::setSourceOffset: Clamped offset to minimum:" << MIN_OFFSET_MS;
		} else if (newOffsetMs > MAX_OFFSET_MS) {
			newOffsetMs = MAX_OFFSET_MS;
			qWarning()
				<< "SourceOffsetManager::setSourceOffset: Clamped offset to maximum:" << MAX_OFFSET_MS;
		}

		// Update settings
		obs_data_set_int(settings, "delay_ms", newOffsetMs);
		obs_source_update(filter, settings);
		obs_data_release(settings);

		obs_source_release(source);
		return true;
	}

	obs_source_release(source);
	return false;
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

	int const AUDIO_COUNT = static_cast<int>(audioSources.size());
	blog(LOG_INFO, "[AudioSync] getAudioSources: Found %d audio sources", AUDIO_COUNT);

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

	int const VIDEO_COUNT = static_cast<int>(videoSources.size());
	blog(LOG_INFO, "[AudioSync] getVideoSources: Found %d video sources", VIDEO_COUNT);

	return videoSources;
}
