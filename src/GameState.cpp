/*
Copyright © 2011-2012 kitano
Copyright © 2014-2015 Justin Jacobs

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

#include "GameState.h"
#include "FileParser.h"
#include "Version.h"
#include "FontEngine.h"
#include "ModManager.h"
#include "WidgetLabel.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"

GameState::GameState()
	: hasMusic(false)
	, has_background(true)
	, has_frame_background(false)
	, reload_music(false)
	, reload_backgrounds(false)
	, force_refresh_background(false)
	, save_settings_on_exit(true)
	, load_counter(0)
	, requestedGameState(NULL)
	, exitRequested(false)
	, loading_tip(new WidgetTooltip())
{
	loading_tip_buf.addText(msg->get("Loading..."));
}

GameState::GameState(const GameState& other) {
	*this = other;
}

GameState& GameState::operator=(const GameState& other) {
	if (this == &other)
		return *this;

	hasMusic = other.hasMusic;
	has_background = other.has_background;
	reload_music = other.reload_music;
	reload_backgrounds = other.reload_backgrounds;
	force_refresh_background = other.force_refresh_background;
	save_settings_on_exit = other.save_settings_on_exit;
	load_counter = other.load_counter;
	requestedGameState = other.requestedGameState;
	exitRequested = other.exitRequested;
	loading_tip = new WidgetTooltip();
	loading_tip_buf.addText(msg->get("Loading..."));

	return *this;
}

GameState::~GameState() {
	render_device->cleanupQueuedImages();

	if (loading_tip)
		delete loading_tip;
}

GameState* GameState::getRequestedGameState() {
	return requestedGameState;
}

void GameState::setRequestedGameState(GameState *new_state) {
	delete requestedGameState;
	requestedGameState = new_state;
	requestedGameState->setLoadingFrame();
	requestedGameState->refreshWidgets();
}

void GameState::logic() {
}

void GameState::render() {
}

void GameState::refreshWidgets() {
}

void GameState::setLoadingFrame() {
	load_counter = 2;
	inpt->reset();
}

bool GameState::isPaused() {
	return false;
}

void GameState::showLoading() {
	if (!loading_tip)
		return;

	loading_tip->render(loading_tip_buf, Point(settings->view_w, settings->view_h), TooltipData::STYLE_FLOAT);

	render_device->commitFrame();
}

void GameState::renderSignature() {
	static WidgetLabel *label = NULL;
	if (!label) {
		std::string version;
		for (size_t i = 0; i < mods->mod_list.size(); ++i) {
			if (mods->mod_list[i].name == "random_dungeon" && mods->mod_list[i].version)
				version = mods->mod_list[i].version->getString();
		}
		label = new WidgetLabel();
		label->setFont("font_small");
		label->setJustify(FontEngine::JUSTIFY_RIGHT);
		label->setVAlign(LabelInfo::VALIGN_BOTTOM);
		label->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
		label->setText("Random Dungeon " + (version.empty() ? std::string("") : "v" + version + "  ") + "- RedByte");
	}
	label->setPos(settings->view_w - 12, settings->view_h - 8);
	label->render();
}

bool GameState::runEdition() {
	static int edition = -1;
	if (edition < 0) {
		edition = 0;
		FileParser infile;
		// @CLASS Edition|Description of engine/edition.txt
		if (infile.open("engine/edition.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE)) {
			while (infile.next()) {
				// @ATTR edition|["full", "run"]|Which game modes this build offers.
				if (infile.key == "edition")
					edition = (infile.val == "run") ? 1 : 0;
			}
			infile.close();
		}
	}
	return edition == 1;
}
