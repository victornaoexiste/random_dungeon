/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2014 Henrik Andersson
Copyright © 2012-2016 Justin Jacobs

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
#include "FileParser.h"
#include "GameStateConfig.h"
#include "GameStateCutscene.h"
#include "GameStateLoad.h"
#include "GameStateMultiplayer.h"
#include "GameStateNew.h"
#include "GameStateTitle.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"
#include "WidgetButton.h"
#include "UtilsFileSystem.h"
#include "UtilsParsing.h"

#include <cstdlib>

GameStateTitle::GameStateTitle()
	: GameState()
	, logo(NULL)
	, button_play(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_run(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_test(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_multiplayer(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_cfg(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_credits(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, align_logo(Utils::ALIGN_CENTER)
{
	// Defaults, overridden by menus/gametitle.txt
	button_play->setBasePos(0, -110, Utils::ALIGN_CENTER);
	button_run->setBasePos(0, -36, Utils::ALIGN_CENTER);
	button_test->setBasePos(0, 38, Utils::ALIGN_CENTER);
	button_multiplayer->setBasePos(0, 112, Utils::ALIGN_CENTER);
	button_cfg->setBasePos(0, 186, Utils::ALIGN_CENTER);
	button_credits->setBasePos(0, 260, Utils::ALIGN_CENTER);

	FileParser infile;
	// @CLASS GameStateTitle|Description of menus/gametitle.txt
	if (infile.open("menus/gametitle.txt", FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) {
		while (infile.next()) {
			// @ATTR logo|filename, int, int, alignment : Image file, X, Y, Alignment|Filename and position of the main logo image.
			if (infile.key == "logo") {
				Image *graphics = render_device->loadImage(Parse::popFirstString(infile.val), RenderDevice::ERROR_NONE);
				if (graphics) {
					logo = graphics->createSprite();
					graphics->unref();

					pos_logo.x = Parse::popFirstInt(infile.val);
					pos_logo.y = Parse::popFirstInt(infile.val);
					align_logo = Parse::toAlignment(Parse::popFirstString(infile.val));
				}
			}
			// @ATTR play_pos|int, int, alignment : X, Y, Alignment|Position of the "Open World" game mode button.
			else if (infile.key == "play_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_play->setBasePos(x, y, a);
			}
			// @ATTR run_pos|int, int, alignment : X, Y, Alignment|Position of the "Infinite Run" game mode button.
			else if (infile.key == "run_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_run->setBasePos(x, y, a);
			}
			// @ATTR test_pos|int, int, alignment : X, Y, Alignment|Position of the "Test Room" game mode button.
			else if (infile.key == "test_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_test->setBasePos(x, y, a);
			}
			// @ATTR multiplayer_pos|int, int, alignment : X, Y, Alignment|Position of the Multiplayer button.
			else if (infile.key == "multiplayer_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_multiplayer->setBasePos(x, y, a);
			}
			// @ATTR config_pos|int, int, alignment : X, Y, Alignment|Position of the Configuration button.
			else if (infile.key == "config_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_cfg->setBasePos(x, y, a);
			}
			// @ATTR credits_pos|int, int, alignment : X, Y, Alignment|Position of the Credits button.
			else if (infile.key == "credits_pos") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_credits->setBasePos(x, y, a);
			}
			else {
				infile.error("GameStateTitle: '%s' is not a valid key.", infile.key.c_str());
			}
		}
		infile.close();
	}

	// run edition: no mode buttons, so Multiplayer/Configuration/Credits move
	// up into the Infinite Run / Test Room / Multiplayer slots of the layout
	if (runEdition()) {
		button_credits->setBasePos(button_multiplayer->pos_base.x, button_multiplayer->pos_base.y, button_multiplayer->alignment);
		Point mp = button_run->pos_base, cfg = button_test->pos_base;
		int mpa = button_run->alignment, cfga = button_test->alignment;
		button_multiplayer->setBasePos(mp.x, mp.y, mpa);
		button_cfg->setBasePos(cfg.x, cfg.y, cfga);
	}

	// the run edition only has the infinite run: one "Play" button
	button_play->setLabel(runEdition() ? msg->get("Play") : msg->get("Open World"));
	button_play->refresh();

	button_run->setLabel(msg->get("Infinite Run"));
	button_run->refresh();

	button_test->setLabel(msg->get("Test Room"));
	button_test->refresh();

	button_multiplayer->setLabel(msg->get("Multiplayer"));
	button_multiplayer->refresh();

	button_cfg->setLabel(msg->get("Configurações"));
	button_cfg->refresh();

	button_credits->setLabel(msg->get("Credits"));
	button_credits->refresh();

	tablist.add(button_play);
	if (!runEdition()) {
		tablist.add(button_run);
		tablist.add(button_test);
	}
	tablist.add(button_multiplayer);
	tablist.add(button_cfg);
	tablist.add(button_credits);

	refreshWidgets();
	force_refresh_background = true;

	// --load-slot=N skips this menu (used to launch test sessions directly)
	if (!settings->load_slot.empty()) {
		showLoading();
		setRequestedGameState(new GameStateLoad());
	}

	render_device->setBackgroundColor(Color(0,0,0,0));

	if (!eset->misc.mouse_move_enabled)
		settings->mouse_move = false;

}

void GameStateTitle::logic() {
	// automated test hook (RD_DEVKIT_SELFTEST=<dir>, see MenuDevKit.h): with
	// --title-shot, capture the title screen once and quit
	static int selftest_frames = 0;
	if (settings->title_screenshot && getenv("RD_DEVKIT_SELFTEST")) {
		++selftest_frames;
		if (selftest_frames == 90)
			render_device->screenshot_request = std::string(getenv("RD_DEVKIT_SELFTEST")) + "/title.png";
		else if (selftest_frames == 100)
			exitRequested = true;
	}
	if (settings->mp_screenshot && getenv("RD_DEVKIT_SELFTEST")) {
		setRequestedGameState(new GameStateMultiplayer());
		return;
	}

	if (inpt->window_resized)
		refreshWidgets();

	if (inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL]) {
		inpt->lock[Input::CANCEL] = true;
		exitRequested = true;
	}

	tablist.logic();

	bool play_clicked = button_play->checkClick();

	if (!inpt->usingMouse() && tablist.getCurrent() == -1) {
		tablist.getNext(!TabList::GET_INNER, TabList::WIDGET_SELECT_AUTO);
	}

	if (play_clicked) {
		startPlay(runEdition() ? "run" : "");
	}
	else if (!runEdition() && button_run->checkClick()) {
		startPlay("run");
	}
	else if (!runEdition() && button_test->checkClick()) {
		startPlay("test");
	}
	else if (button_multiplayer->checkClick()) {
		showLoading();
		setRequestedGameState(new GameStateMultiplayer());
	}
	else if (button_cfg->checkClick()) {
		showLoading();
		setRequestedGameState(new GameStateConfig());
	}
	else if (button_credits->checkClick()) {
		showLoading();
		GameStateTitle *title = new GameStateTitle();
		GameStateCutscene *credits = new GameStateCutscene(title);
		if (!credits->load("cutscenes/credits.txt")) {
			delete credits;
			delete title;
		}
		else {
			setRequestedGameState(credits);
		}
	}
}

/**
 * Every game mode goes through the normal character select / new character
 * screens; the mode only decides where the hero enters the game
 * (GameStatePlay::modeStartMap).
 */
void GameStateTitle::startPlay(const std::string& mode) {
	settings->game_mode = mode;
	showLoading();

	// no saves yet: go straight to character creation
	std::vector<std::string> save_dirs;
	Filesystem::getDirList(settings->path_user + "saves/" + eset->misc.save_prefix, save_dirs);
	if (save_dirs.empty()) {
		GameStateNew* newgame = new GameStateNew();
		newgame->game_slot = 1;
		setRequestedGameState(newgame);
	}
	else {
		setRequestedGameState(new GameStateLoad());
	}
}

void GameStateTitle::refreshWidgets() {
	if (logo) {
		Rect r;
		r.x = pos_logo.x;
		r.y = pos_logo.y;
		r.w = logo->getGraphicsWidth();
		r.h = logo->getGraphicsHeight();
		Utils::alignToScreenEdge(align_logo, &r);
		logo->setDestFromRect(r);
	}

	button_play->setPos(0, 0);
	button_run->setPos(0, 0);
	button_test->setPos(0, 0);
	button_multiplayer->setPos(0, 0);
	button_cfg->setPos(0, 0);
	button_credits->setPos(0, 0);
}

void GameStateTitle::render() {
	render_device->render(logo);

	button_play->render();
	if (!runEdition()) {
		button_run->render();
		button_test->render();
	}
	button_multiplayer->render();
	button_cfg->render();
	button_credits->render();
}

GameStateTitle::~GameStateTitle() {
	if (logo) delete logo;
	delete button_play;
	delete button_run;
	delete button_test;
	delete button_multiplayer;
	delete button_cfg;
	delete button_credits;
}
