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

#include "dock-widget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

NMDockWidget::NMDockWidget(RecordingManager *mgr, QWidget *parent) : QWidget(parent), mgr_(mgr)
{
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 8, 8, 8);
	mainLayout->setSpacing(6);

	// --- Enable/Disable toggle ---
	enabledCheck_ = new QCheckBox(tr("Enable Input Capture"), this);
	enabledCheck_->setChecked(mgr_->isEnabled());
	mainLayout->addWidget(enabledCheck_);

	// --- Game name ---
	{
		auto *group = new QGroupBox(tr("Game Name"), this);
		auto *layout = new QHBoxLayout(group);
		layout->setContentsMargins(6, 6, 6, 6);

		gameNameEdit_ = new QLineEdit(this);
		gameNameEdit_->setPlaceholderText(tr("Enter game name..."));
		gameNameEdit_->setText(QString::fromStdString(mgr_->gameName()));
		layout->addWidget(gameNameEdit_);

		mainLayout->addWidget(group);
	}

	// --- Input ---
	{
		auto *group = new QGroupBox(tr("Input"), this);
		auto *layout = new QVBoxLayout(group);
		layout->setContentsMargins(6, 6, 6, 6);
		layout->setSpacing(4);

		keysLabel_ = new QLabel(tr("Keys: (none)"), this);
		keysLabel_->setWordWrap(true);
		keysLabel_->setStyleSheet("font-family: monospace;");
		layout->addWidget(keysLabel_);

		mouseLabel_ = new QLabel(tr("Mouse: --"), this);
		mouseLabel_->setWordWrap(true);
		mouseLabel_->setStyleSheet("font-family: monospace;");
		layout->addWidget(mouseLabel_);

		mainLayout->addWidget(group);
	}

	mainLayout->addStretch();

	// --- Connections ---
	connect(gameNameEdit_, &QLineEdit::textChanged, this, &NMDockWidget::onGameNameChanged);
	connect(enabledCheck_, &QCheckBox::toggled, this, &NMDockWidget::onEnabledToggled);

	// Refresh timer for live preview (50ms = ~20 Hz)
	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(50);
	connect(refreshTimer_, &QTimer::timeout, this, &NMDockWidget::refreshPreview);
	refreshTimer_->start();

	// Initial refresh
	refreshPreview();
}

void NMDockWidget::onGameNameChanged(const QString &text)
{
	mgr_->setGameName(text.toStdString());
}

void NMDockWidget::onEnabledToggled(bool checked)
{
	mgr_->setEnabled(checked);
}

void NMDockWidget::refreshPreview()
{
	// Input (only when capturer is running)
	auto *capturer = mgr_->inputCapturer();
	if (capturer && capturer->isRunning()) {
		InputState state = capturer->peekState();

		// Keys
		if (state.keys.empty()) {
			keysLabel_->setText(tr("Keys: (none)"));
		} else {
			QString keyStr;
			for (size_t i = 0; i < state.keys.size(); i++) {
				if (i > 0)
					keyStr += " + ";
				keyStr += QString::fromStdString(state.keys[i]);
			}
			keysLabel_->setText(tr("Keys: ") + keyStr);
		}

		// Mouse
		QString mouseStr = QString("Mouse: (%1, %2)  dx=%3 dy=%4  scroll=%5")
					   .arg(state.mouse.x)
					   .arg(state.mouse.y)
					   .arg(state.mouse.dx)
					   .arg(state.mouse.dy)
					   .arg(state.mouse.scroll_dy);
		if (!state.mouse.buttons.empty()) {
			mouseStr += "  [";
			for (size_t i = 0; i < state.mouse.buttons.size(); i++) {
				if (i > 0)
					mouseStr += ", ";
				mouseStr += QString::fromStdString(state.mouse.buttons[i]);
			}
			mouseStr += "]";
		}
		mouseLabel_->setText(mouseStr);
	} else {
		keysLabel_->setText(tr("Keys: (not capturing)"));
		mouseLabel_->setText(tr("Mouse: (not capturing)"));
	}
}
