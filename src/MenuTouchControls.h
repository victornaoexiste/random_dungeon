/*
Copyright © 2018 Justin Jacobs

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
 * class MenuTouchControls
 *
 * Mobile controls (Random Dungeon): multi-touch, so you can move and fight
 * at the same time.
 *  - Floating joystick on the left: wherever a finger lands in the left part
 *    of the screen becomes its centre; it drives the 8 movement directions.
 *  - Button cluster on the right: a big attack button (action bar slot M1),
 *    three skill buttons (M2, 3, 4) and two potion buttons (1, 2). They show
 *    the icon of whatever is in that slot, so players customise them through
 *    the Powers menu like on desktop. Skills aim at the nearest enemy (see
 *    MenuActionBar::checkAction / isSlotHeld).
 *  - A pause button next to the minimap.
 * World taps (loot, NPCs, doors) still work, but never trigger attacks.
 * Art comes from images/menus/touch/ (see the UI theme generator).
 */

#ifndef MENU_TOUCHCONTROLS_H
#define MENU_TOUCHCONTROLS_H

#include "CommonIncludes.h"
#include "Menu.h"
#include "Utils.h"

class Sprite;

class MenuTouchControls : public Menu {
public:
	enum {
		BTN_ATTACK = 0,
		BTN_SKILL1,
		BTN_SKILL2,
		BTN_SKILL3,
		BTN_POTION1,
		BTN_POTION2,
		BTN_PAUSE,
		BTN_COUNT
	};

	MenuTouchControls();
	~MenuTouchControls();
	void logic();
	void align();
	void render();

	// false while touch controls are active: attacks only come from the buttons
	bool checkAllowMain1();
	// true while a touch button bound to this action bar slot is held
	bool isSlotHeld(unsigned slot) const;
	// true once after the pause button was tapped
	bool takePausePressed();
	// centre of the touch button bound to this action bar slot, if any
	bool getSlotCenter(unsigned slot, Point& center) const;

private:
	class Button {
	public:
		Button() : slot(-1), base_radius(0), radius(0), held(false), was_held(false) {}
		int slot;        // action bar slot index, -1 for none
		Point base;      // centre, offset from a screen corner (unscaled)
		int base_radius;
		Point center;
		int radius;
		bool held;
		bool was_held;
	};

	bool isOnControl(const Point& p) const;
	void loadGraphics();
	void renderCentered(Sprite *spr, const Point& c, int frame);

	Button buttons[BTN_COUNT];

	Point joy_default;   // resting centre
	Point joy_center;    // centre while dragging (floating)
	Point joy_knob;
	int joy_radius;
	int joy_deadzone;
	long int joy_finger;
	bool joy_active;

	bool pause_pressed;
	float prev_touch_scale;

	Sprite *spr_joy_base;
	Sprite *spr_joy_knob;
	Sprite *spr_btn_big;
	Sprite *spr_btn_small;
	Sprite *spr_btn_pause;
};

#endif
