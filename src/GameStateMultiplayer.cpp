/*
This file is part of FLARE.

FLARE is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

FLARE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
FLARE.  If not, see http://www.gnu.org/licenses/
*/

#include "CommonIncludes.h"
#include "EngineSettings.h"
#include "FontEngine.h"
#include "GameStateLoad.h"
#include "GameStateMultiplayer.h"
#include "GameStateNew.h"
#include "GameStateTitle.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "NetManager.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"
#include "UtilsFileSystem.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetInput.h"
#include "WidgetLabel.h"

GameStateMultiplayer::GameStateMultiplayer()
	: GameState()
	, button_host(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_join(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_back(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, input_ip(new WidgetInput(WidgetInput::DEFAULT_FILE))
	, label_ip(new WidgetLabel())
	, label_title(new WidgetLabel())
	, label_status(new WidgetLabel())
{
	button_host->setLabel(msg->get("Hospedar"));
	button_host->setBasePos(0, -110, Utils::ALIGN_CENTER);
	button_host->refresh();

	input_ip->max_length = 40;
	input_ip->setText("127.0.0.1:4650");
	input_ip->setBasePos(0, -20, Utils::ALIGN_CENTER);

	button_join->setLabel(msg->get("Conectar"));
	button_join->setBasePos(0, 40, Utils::ALIGN_CENTER);
	button_join->refresh();

	button_back->setLabel(msg->get("Voltar"));
	button_back->setBasePos(0, 140, Utils::ALIGN_CENTER);
	button_back->refresh();

	label_title->setText(msg->get("Multiplayer"));
	label_title->setJustify(FontEngine::JUSTIFY_CENTER);
	label_title->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_ip->setText(msg->get("IP[:porta] do host"));
	label_ip->setJustify(FontEngine::JUSTIFY_CENTER);
	label_ip->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_status->setText("");
	label_status->setJustify(FontEngine::JUSTIFY_CENTER);
	label_status->setColor(font->getColor(FontEngine::COLOR_WIDGET_DISABLED));

	tablist.add(button_host);
	tablist.add(input_ip);
	tablist.add(button_join);
	tablist.add(button_back);

	refreshWidgets();
	force_refresh_background = true;
}

void GameStateMultiplayer::refreshWidgets() {
	button_host->setPos(0, 0);
	input_ip->setPos(0, 0);
	button_join->setPos(0, 0);
	button_back->setPos(0, 0);

	label_title->setPos(settings->view_w / 2, settings->view_h / 2 - 180);
	label_ip->setPos(settings->view_w / 2, settings->view_h / 2 - 60);
	label_status->setPos(settings->view_w / 2, settings->view_h / 2 + 90);
}

void GameStateMultiplayer::proceedToNewGame() {
	showLoading();

	std::vector<std::string> save_dirs;
	Filesystem::getDirList(settings->path_user + "saves/" + eset->misc.save_prefix, save_dirs);
	if (save_dirs.empty()) {
		GameStateNew *newgame = new GameStateNew();
		newgame->game_slot = 1;
		setRequestedGameState(newgame);
	}
	else {
		setRequestedGameState(new GameStateLoad());
	}
}

void GameStateMultiplayer::startAsHost() {
	if (netmgr && netmgr->isActive())
		netmgr->shutdown();
	if (!netmgr)
		netmgr = new NetManager();

	if (netmgr->startServer(4650)) {
		proceedToNewGame();
	}
	else {
		label_status->setText(msg->get("Não foi possível hospedar (porta em uso?)"));
	}
}

void GameStateMultiplayer::startAsClient() {
	std::string target = input_ip->getText();
	if (target.empty()) {
		label_status->setText(msg->get("Digite o IP do host"));
		return;
	}

	std::string host_str = target;
	uint16_t port = 4650;
	size_t colon = target.find(':');
	if (colon != std::string::npos) {
		port = static_cast<uint16_t>(atoi(target.substr(colon + 1).c_str()));
		host_str = target.substr(0, colon);
	}

	if (netmgr && netmgr->isActive())
		netmgr->shutdown();
	if (!netmgr)
		netmgr = new NetManager();

	// Blocking (see header comment) -- the screen will visibly hang for up
	// to this timeout on a failed connection attempt.
	label_status->setText(msg->get("Conectando..."));
	render();
	render_device->commitFrame();

	if (netmgr->connectToServer(host_str, port, 5000)) {
		proceedToNewGame();
	}
	else {
		label_status->setText(msg->get("Não foi possível conectar"));
	}
}

void GameStateMultiplayer::logic() {
	if (inpt->window_resized)
		refreshWidgets();

	if (inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL]) {
		inpt->lock[Input::CANCEL] = true;
		setRequestedGameState(new GameStateTitle());
		return;
	}

	if (!input_ip->edit_mode)
		tablist.logic();

	input_ip->logic();

	if (!input_ip->edit_mode && !inpt->usingMouse() && tablist.getCurrent() == -1) {
		tablist.getNext(!TabList::GET_INNER, TabList::WIDGET_SELECT_AUTO);
	}

	if (button_host->checkClick()) {
		startAsHost();
	}
	else if (button_join->checkClick()) {
		startAsClient();
	}
	else if (button_back->checkClick()) {
		setRequestedGameState(new GameStateTitle());
	}
}

void GameStateMultiplayer::render() {
	label_title->render();
	label_ip->render();
	label_status->render();

	button_host->render();
	input_ip->render();
	button_join->render();
	button_back->render();
}

GameStateMultiplayer::~GameStateMultiplayer() {
	delete button_host;
	delete button_join;
	delete button_back;
	delete input_ip;
	delete label_ip;
	delete label_title;
	delete label_status;
}
