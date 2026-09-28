/*
Copyright © 2020 Justin Jacobs

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

#ifndef MENU_GAMEOVER_H
#define MENU_GAMEOVER_H

#include "CommonIncludes.h"
#include "Menu.h"
#include "WidgetLabel.h"

class WidgetButton;

class MenuGameOver : public Menu {
protected:
	WidgetButton *button_continue;
	WidgetButton *button_exit;
	WidgetButton *button_sanctuary; // Random Dungeon: only after an Infinite Run (show_sanctuary)
	WidgetLabel label;
	WidgetLabel label_info[2]; // optional extra lines (Random Dungeon: run summary)

public:
	MenuGameOver();
	~MenuGameOver();

	void logic();
	void align();
	void close();
	void disableSave();
	// Two optional lines under the title; cleared when the menu closes.
	void setInfo(const std::string& line1, const std::string& line2);
	virtual void render();

	bool continue_clicked;
	bool exit_clicked;
	bool sanctuary_clicked;
	bool show_sanctuary;
	bool blocked; // another screen (the Sanctuary) is on top
};

#endif
