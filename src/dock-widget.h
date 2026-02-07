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

#pragma once

#include "recording-manager.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QWidget>

class NMDockWidget : public QWidget {
	Q_OBJECT

public:
	explicit NMDockWidget(RecordingManager *mgr, QWidget *parent = nullptr);
	~NMDockWidget() override = default;

private slots:
	void onGameNameChanged(const QString &text);
	void onEnabledToggled(bool checked);
	void refreshPreview();

private:
	RecordingManager *mgr_;

	QLineEdit *gameNameEdit_;
	QCheckBox *enabledCheck_;
	QLabel *keysLabel_;
	QLabel *mouseLabel_;
	QTimer *refreshTimer_;
};
