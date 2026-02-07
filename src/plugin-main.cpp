/*
NeuroMiner OBS Plugin
Copyright (C) 2026 NeuroMiner <info@neurominer.ai>

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
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "dock-widget.h"
#include "recording-manager.h"

#include <QDockWidget>
#include <QMainWindow>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static std::unique_ptr<RecordingManager> g_recordingManager;
static NMDockWidget *g_dockWidget = nullptr; // owned by OBS (Qt parent)
static const char *kDockId = "neurominer-input-recorder";

static void configureDock()
{
	if (!g_dockWidget)
		return;

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return;

	QDockWidget *dock = mainWindow->findChild<QDockWidget *>(kDockId);
	if (!dock) {
		const auto docks = mainWindow->findChildren<QDockWidget *>();
		for (auto *candidate : docks) {
			if (candidate && candidate->widget() == g_dockWidget) {
				dock = candidate;
				break;
			}
		}
	}

	if (!dock)
		return;

	auto features = dock->features();
	features &= ~QDockWidget::DockWidgetClosable;
	dock->setFeatures(features);

	mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
	dock->setVisible(true);
	dock->raise();
}

// ---------------------------------------------------------------------------
// Frontend event callback
// ---------------------------------------------------------------------------
static void onFrontendEvent(enum obs_frontend_event event, void * /*data*/)
{
	if (!g_recordingManager)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		configureDock();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		obs_log(LOG_INFO, "OBS recording started — beginning input capture session.");
		g_recordingManager->startSession();
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		obs_log(LOG_INFO, "OBS recording stopped — finalizing input capture session.");
		g_recordingManager->stopSession();
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Module load / unload
// ---------------------------------------------------------------------------
bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	// Create the recording manager
	g_recordingManager = std::make_unique<RecordingManager>();

	// Register frontend event callback for recording start/stop
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	// Create and register the dock widget
	g_dockWidget = new NMDockWidget(g_recordingManager.get());
	obs_frontend_add_dock_by_id(kDockId, "NeuroMiner Input Recorder", g_dockWidget);
	configureDock();

	obs_log(LOG_INFO, "NeuroMiner Input Recorder dock registered.");
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloading...");

	// Remove frontend callback
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

	// The dock widget is owned by OBS/Qt, do not delete it manually.
	g_dockWidget = nullptr;

	// Destroy the recording manager (will stop any active session)
	g_recordingManager.reset();

	obs_log(LOG_INFO, "plugin unloaded");
}
