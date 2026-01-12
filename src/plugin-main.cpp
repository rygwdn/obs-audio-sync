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

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include <util/base.h>
#include "audio-sync-panel.h"
#include <QtCore/qlogging.h>
#include <QtCore/qdebug.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
AudioSyncPanel *panel = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Qt message handler to redirect to OBS logging
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	Q_UNUSED(context);
	int logLevel = LOG_INFO;
	switch (type) {
	case QtDebugMsg:
		// Use LOG_INFO for debug messages (LOG_DEBUG may not be available in all OBS versions)
		logLevel = LOG_INFO;
		break;
	case QtWarningMsg:
		logLevel = LOG_WARNING;
		break;
	case QtCriticalMsg:
	case QtFatalMsg:
		logLevel = LOG_ERROR;
		break;
	case QtInfoMsg:
		logLevel = LOG_INFO;
		break;
	}
	obsLog(logLevel, "%s", msg.toUtf8().constData());
}

// OBS frontend event callback to detect recording end and muxing end
void onFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	Q_UNUSED(private_data);

	// Only refresh if panel exists
	if (panel == nullptr) {
		return;
	}

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		obsLog(LOG_INFO, "Recording stopped event detected, scheduling delayed refresh");
		// Recording has stopped - schedule a delayed refresh to ensure file is ready after muxing
		// Use QMetaObject::invokeMethod to ensure we're on the correct thread
		QMetaObject::invokeMethod(panel, "scheduleDelayedRefresh", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		obsLog(LOG_INFO, "Scene change detected, refreshing audio sources");
		// Scene changed - refresh audio source list
		QMetaObject::invokeMethod(panel, "populateAudioSources", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_EXIT: {
		obsLog(LOG_INFO, "OBS exit event detected, performing early cleanup");
		// OBS is about to exit - this is our last chance to safely cleanup while
		// the OBS API is still fully functional. Do as much cleanup as possible here.
		// After this callback returns, no further frontend API calls are permitted.

		// Set panel to nullptr FIRST to prevent any callbacks from accessing it
		AudioSyncPanel *panelToDelete = panel;
		panel = nullptr;

		// Delete the panel now, which will:
		// - Stop audio monitoring (prevents audio thread callbacks)
		// - Disconnect all Qt signals/slots
		// - Stop and wait for worker threads
		// - Clean up all UI resources
		if (panelToDelete != nullptr) {
			delete panelToDelete;
			obsLog(LOG_INFO, "Panel cleanup completed during exit event");
		}
		break;
	}
	default:
		// Ignore other events
		break;
	}
}

// OBS signal handler for source events (show/hide/activate/deactivate/audio_mixers/destroy)
void onSourceEvent(void *data, calldata_t *cd)
{
	Q_UNUSED(data);
	Q_UNUSED(cd);

	// Only refresh if panel exists
	if (panel == nullptr) {
		return;
	}

	obsLog(LOG_INFO, "Source event detected, refreshing audio sources");
	// Source changed - refresh audio source list
	QMetaObject::invokeMethod(panel, "populateAudioSources", Qt::QueuedConnection);
}

// Callback to connect signal handlers for a source
bool connectSourceSignals(void *data, obs_source_t *source)
{
	Q_UNUSED(data);

	if (source == nullptr) {
		return true;
	}

	signal_handler_t *handler = obs_source_get_signal_handler(source);
	if (handler == nullptr) {
		return true;
	}

	// Connect to source signals that affect audio source visibility
	signal_handler_connect(handler, "show", onSourceEvent, nullptr);
	signal_handler_connect(handler, "hide", onSourceEvent, nullptr);
	signal_handler_connect(handler, "activate", onSourceEvent, nullptr);
	signal_handler_connect(handler, "deactivate", onSourceEvent, nullptr);
	signal_handler_connect(handler, "audio_mixers", onSourceEvent, nullptr);
	signal_handler_connect(handler, "destroy", onSourceEvent, nullptr);

	return true;
}
} // namespace

bool obs_module_load(void)
{
	// Install Qt message handler to redirect to OBS logging
	qInstallMessageHandler(qtMessageHandler);

	obsLog(LOG_INFO, "plugin loaded successfully (version %s)", pluginVersion);

	// Create and register the panel
	panel = new AudioSyncPanel();
	obs_frontend_add_dock_by_id("obs-audio-sync", "Audio Sync", panel);

	// Register event callback for recording and scene events
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	// Connect signal handlers for all existing sources
	obs_enum_sources(connectSourceSignals, nullptr);

	obsLog(LOG_INFO, "Audio Sync panel registered");
	return true;
}

void obs_module_unload(void)
{
	// Most cleanup is now handled in OBS_FRONTEND_EVENT_EXIT, which fires
	// before this function is called. At this point:
	// - Panel has already been deleted during EXIT event
	// - panel global is already nullptr
	// - Audio monitoring stopped
	// - Worker threads cleaned up
	//
	// We let OBS automatically handle:
	// - obs_frontend_remove_event_callback (cleaned up during OBS shutdown)
	// - obs_frontend_remove_dock (dock cleanup handled by OBS)
	// - Source signal handlers (cleaned up when sources are destroyed)
	//
	// This follows the pattern used by official OBS plugins like obs-browser,
	// which don't manually unregister callbacks in obs_module_unload.

	// Restore default Qt message handler
	qInstallMessageHandler(nullptr);
	obsLog(LOG_INFO, "plugin unloaded");
}
