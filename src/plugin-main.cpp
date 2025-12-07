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
AudioSyncPanel *panel = nullptr;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
bool eventCallbackRegistered = false; // Track if event callback was registered
bool dockRegistered = false;          // Track if dock was registered

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
	default:
		// Ignore other events
		break;
	}
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
	dockRegistered = true;

	// Register event callback for recording events
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	eventCallbackRegistered = true;

	obsLog(LOG_INFO, "Audio Sync panel registered");
	return true;
}

void obs_module_unload(void)
{
	// Unregister event callback only if it was registered
	if (eventCallbackRegistered) {
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
		eventCallbackRegistered = false;
	}

	if (panel != nullptr) {
		// Remove dock only if it was registered
		if (dockRegistered) {
			obs_frontend_remove_dock("obs-audio-sync");
			dockRegistered = false;
		}
		delete panel;
		panel = nullptr;
	}
	// Restore default Qt message handler
	qInstallMessageHandler(nullptr);
	obsLog(LOG_INFO, "plugin unloaded");
}
