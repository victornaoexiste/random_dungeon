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

/**
 * class GameStateMultiplayer
 *
 * Multiplayer screen reached from the main menu: one panel with two
 * sections -- host a game on this machine (fixed port), or join one by
 * IP[:port], with a PvP toggle for the host (see NetManager.h Step 6).
 * Replaces having to know the --net-host/--net-join command line
 * flags (see NetManager.h).
 *
 * connectToServer() is a blocking call (up to a few seconds' timeout) -- the
 * engine has no async networking, so joining briefly freezes the screen while
 * it waits for the handshake or the timeout. Known limitation, not a bug.
 */

#ifndef GAMESTATEMULTIPLAYER_H
#define GAMESTATEMULTIPLAYER_H

#include "GameState.h"
#include "Widget.h"

class WidgetButton;
class WidgetInput;
class WidgetLabel;

class GameStateMultiplayer : public GameState {
private:
	static const int HOST_PORT = 4650;

	Sprite *panel;
	WidgetLabel *label_title;
	WidgetLabel *label_host;
	WidgetLabel *label_join;
	WidgetLabel *label_status;
	WidgetButton *button_host;
	WidgetButton *button_pvp;
	WidgetInput *input_ip;
	WidgetButton *button_search;
	WidgetButton *button_join;
	WidgetButton *button_back;

	TabList tablist;

	void refreshWidgets();
	void setStatus(const std::string& text, bool error);
	void refreshPvpLabel();
	bool parseTarget(const std::string& target, std::string& host_str, uint16_t& port);
	void startAsHost();
	void startAsClient();
	void searchLan();
	void proceedToNewGame();

public:
	GameStateMultiplayer();
	~GameStateMultiplayer();
	void logic();
	void render();
};

#endif
