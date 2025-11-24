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

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
AudioSyncPanel *panel = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}

bool obs_module_load(void)
{
	obsLog(LOG_INFO, "plugin loaded successfully (version %s)", pluginVersion);

	// Create and register the panel
	panel = new AudioSyncPanel();
	obs_frontend_add_dock_by_id("obs-audio-sync", "Audio Sync", panel);

	obsLog(LOG_INFO, "Audio Sync panel registered");
	return true;
}

void obs_module_unload(void)
{
	if (panel != nullptr) {
		obs_frontend_remove_dock("obs-audio-sync");
		delete panel;
		panel = nullptr;
	}
	obsLog(LOG_INFO, "plugin unloaded");
}
