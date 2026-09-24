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
 * "Hospedar" (start a server on this machine) / "Conectar" (join one by
 * IP[:port]) screen, reached from the title screen. Replaces having to know
 * --net-host/--net-join command line flags to test multiplayer -- see
 * NetManager.h for what those flags used to be the only way to reach.
 *
 * connectToServer() is a blocking call (up to a few seconds' timeout) -- the
 * engine has no async networking, so clicking "Conectar" will briefly freeze
 * the game while it waits for the handshake or the timeout. Known limitation,
 * not a bug.
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
	WidgetButton *button_host;
	WidgetButton *button_join;
	WidgetButton *button_back;
	WidgetInput *input_ip;
	WidgetLabel *label_ip;
	WidgetLabel *label_title;
	WidgetLabel *label_status;

	TabList tablist;

	void refreshWidgets();
	void startAsHost();
	void startAsClient();
	void proceedToNewGame();

public:
	GameStateMultiplayer();
	~GameStateMultiplayer();
	void logic();
	void render();
};

#endif
