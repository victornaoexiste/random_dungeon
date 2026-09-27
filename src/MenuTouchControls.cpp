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

#include "EngineSettings.h"
#include "IconManager.h"
#include "InputState.h"
#include "MenuActionBar.h"
#include "MenuManager.h"
#include "MenuTouchControls.h"
#include "PowerManager.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"

#include <cmath>

namespace {
	// base layout, in screen px at the reference view height (960); scaled
	// by settings->touch_scale. Offsets are from the bottom-right corner,
	// except the pause button (top-right).
	struct ButtonDef { int slot; int x; int y; int r; };
	const ButtonDef BUTTON_DEFS[MenuTouchControls::BTN_COUNT] = {
		{ 10, -170, -175, 96 },  // attack: M1
		{ 11, -350, -110, 62 },  // M2
		{  2, -345, -285, 62 },  // slot 3
		{  3, -185, -370, 62 },  // slot 4
		{  0,  -60, -330, 44 },  // slot 1 (potion)
		{  1,  -60, -440, 44 },  // slot 2 (potion)
		{ -1, -320,   48, 40 },  // pause (from the top-right)
	};
	const int JOY_X = 210;
	const int JOY_Y = -210;
	const int JOY_R = 140;
}

MenuTouchControls::MenuTouchControls()
	: Menu()
	, joy_radius(JOY_R)
	, joy_deadzone(22)
	, joy_finger(-1)
	, joy_active(false)
	, pause_pressed(false)
	, prev_touch_scale(settings->touch_scale)
	, spr_joy_base(NULL)
	, spr_joy_knob(NULL)
	, spr_btn_big(NULL)
	, spr_btn_small(NULL)
	, spr_btn_pause(NULL)
{
	visible = true;
	for (int i = 0; i < BTN_COUNT; ++i) {
		buttons[i].slot = BUTTON_DEFS[i].slot;
		buttons[i].base = Point(BUTTON_DEFS[i].x, BUTTON_DEFS[i].y);
		buttons[i].base_radius = BUTTON_DEFS[i].r;
	}
	loadGraphics();
	align();
}

void MenuTouchControls::loadGraphics() {
	const char *files[] = { "images/menus/touch/joystick_base.png", "images/menus/touch/joystick_knob.png",
	                        "images/menus/touch/button_big.png", "images/menus/touch/button_small.png",
	                        "images/menus/touch/button_pause.png" };
	Sprite **targets[] = { &spr_joy_base, &spr_joy_knob, &spr_btn_big, &spr_btn_small, &spr_btn_pause };
	for (int i = 0; i < 5; ++i) {
		Image *graphics = render_device->loadImage(files[i], RenderDevice::ERROR_NONE);
		if (graphics) {
			*targets[i] = graphics->createSprite();
			graphics->unref();
		}
	}
}

void MenuTouchControls::align() {
	const float s = settings->touch_scale;
	for (int i = 0; i < BTN_COUNT; ++i) {
		Button& b = buttons[i];
		b.radius = static_cast<int>(static_cast<float>(b.base_radius) * s);
		b.center.x = settings->view_w + static_cast<int>(static_cast<float>(b.base.x) * s);
		if (i == BTN_PAUSE)
			b.center.y = static_cast<int>(static_cast<float>(b.base.y) * s);
		else
			b.center.y = settings->view_h + static_cast<int>(static_cast<float>(b.base.y) * s);
	}
	joy_radius = static_cast<int>(static_cast<float>(JOY_R) * s);
	joy_default.x = static_cast<int>(static_cast<float>(JOY_X) * s);
	joy_default.y = settings->view_h + static_cast<int>(static_cast<float>(JOY_Y) * s);
	if (!joy_active) {
		joy_center = joy_default;
		joy_knob = joy_center;
	}
}

bool MenuTouchControls::isOnControl(const Point& p) const {
	FPoint fp(static_cast<float>(p.x), static_cast<float>(p.y));
	for (int i = 0; i < BTN_COUNT; ++i) {
		FPoint c(static_cast<float>(buttons[i].center.x), static_cast<float>(buttons[i].center.y));
		if (Utils::isWithinRadius(c, static_cast<float>(buttons[i].radius) * 1.15f, fp))
			return true;
	}
	return false;
}

void MenuTouchControls::logic() {
	if (!visible || !settings->touchscreen) {
		joy_active = false;
		for (int i = 0; i < BTN_COUNT; ++i)
			buttons[i].held = buttons[i].was_held = false;
		return;
	}

	if (settings->touch_scale != prev_touch_scale) {
		prev_touch_scale = settings->touch_scale;
		align();
	}

	inpt->pressing[Input::LEFT] = inpt->pressing[Input::RIGHT] = inpt->pressing[Input::UP] = inpt->pressing[Input::DOWN] = false;

	const std::vector<InputState::FingerData>& fingers = inpt->getTouchFingers();

	// --- joystick: the finger that grabbed it keeps it until lifted
	Point joy_raw(-1, -1);
	if (joy_active) {
		bool still_down = false;
		for (size_t i = 0; i < fingers.size(); ++i) {
			if (fingers[i].id == joy_finger) {
				still_down = true;
				joy_knob = fingers[i].pos;
				joy_raw = fingers[i].pos;
			}
		}
		if (!still_down)
			joy_active = false;
	}
	if (!joy_active) {
		for (size_t i = 0; i < fingers.size(); ++i) {
			const Point& p = fingers[i].pos;
			if (p.x < settings->view_w * 45 / 100 && p.y > settings->view_h * 30 / 100 && !isOnControl(p)) {
				joy_active = true;
				joy_finger = fingers[i].id;
				// floating: centre where the thumb landed, kept fully on screen
				joy_center.x = std::max(joy_radius, p.x);
				joy_center.y = std::min(settings->view_h - joy_radius, p.y);
				joy_knob = p;
				joy_raw = p;
				break;
			}
		}
	}
	if (joy_active) {
		float dx = static_cast<float>(joy_knob.x - joy_center.x);
		float dy = static_cast<float>(joy_knob.y - joy_center.y);
		float len = std::sqrt(dx * dx + dy * dy);
		if (len > static_cast<float>(joy_radius)) {
			joy_knob.x = joy_center.x + static_cast<int>(dx / len * static_cast<float>(joy_radius));
			joy_knob.y = joy_center.y + static_cast<int>(dy / len * static_cast<float>(joy_radius));
		}
		if (len > static_cast<float>(joy_deadzone)) {
			// 8 directions: a component counts once it's past ~22.5 degrees
			float nx = dx / len, ny = dy / len;
			if (nx < -0.38f) inpt->pressing[Input::LEFT] = true;
			if (nx > 0.38f) inpt->pressing[Input::RIGHT] = true;
			if (ny < -0.38f) inpt->pressing[Input::UP] = true;
			if (ny > 0.38f) inpt->pressing[Input::DOWN] = true;
		}
	}
	else {
		joy_center = joy_default;
		joy_knob = joy_center;
	}

	// --- buttons: any finger (other than the joystick's) inside them
	for (int i = 0; i < BTN_COUNT; ++i) {
		Button& b = buttons[i];
		b.was_held = b.held;
		b.held = false;
		FPoint c(static_cast<float>(b.center.x), static_cast<float>(b.center.y));
		for (size_t f = 0; f < fingers.size(); ++f) {
			if (joy_active && fingers[f].id == joy_finger)
				continue;
			FPoint fp(static_cast<float>(fingers[f].pos.x), static_cast<float>(fingers[f].pos.y));
			if (Utils::isWithinRadius(c, static_cast<float>(b.radius) * 1.15f, fp))
				b.held = true;
		}
	}
	if (buttons[BTN_PAUSE].held && !buttons[BTN_PAUSE].was_held)
		pause_pressed = true;

	// a finger on the controls must not also click the world under it
	if (isOnControl(inpt->mouse) || (joy_active && inpt->mouse.x == joy_raw.x && inpt->mouse.y == joy_raw.y))
		inpt->pressing[Input::MAIN1] = false;
}

bool MenuTouchControls::checkAllowMain1() {
	return !(visible && settings->touchscreen);
}

bool MenuTouchControls::isSlotHeld(unsigned slot) const {
	if (!visible || !settings->touchscreen)
		return false;
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (buttons[i].held && buttons[i].slot == static_cast<int>(slot))
			return true;
	}
	return false;
}

bool MenuTouchControls::getSlotCenter(unsigned slot, Point& center) const {
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (buttons[i].slot == static_cast<int>(slot)) {
			center = buttons[i].center;
			return true;
		}
	}
	return false;
}

bool MenuTouchControls::takePausePressed() {
	bool p = pause_pressed;
	pause_pressed = false;
	return p;
}

// Sprites are sheets of equal square frames stacked vertically (normal, pressed).
void MenuTouchControls::renderCentered(Sprite *spr, const Point& c, int frame) {
	if (!spr)
		return;
	int w = spr->getGraphicsWidth();
	Rect src(0, frame * w, w, w);
	spr->setClipFromRect(src);
	spr->setDest(c.x - w / 2, c.y - w / 2);
	render_device->render(spr);
}

void MenuTouchControls::render() {
	if (!visible || !settings->touchscreen)
		return;

	renderCentered(spr_joy_base, joy_center, 0);
	renderCentered(spr_joy_knob, joy_knob, joy_active ? 1 : 0);

	for (int i = 0; i < BTN_COUNT; ++i) {
		const Button& b = buttons[i];
		Sprite *spr = (i == BTN_PAUSE) ? spr_btn_pause : (i == BTN_ATTACK ? spr_btn_big : spr_btn_small);
		renderCentered(spr, b.center, b.held ? 1 : 0);

		// the power currently in that action bar slot
		if (b.slot >= 0 && menu && menu->act && static_cast<size_t>(b.slot) < menu->act->hotkeys_mod.size()) {
			PowerID id = menu->act->hotkeys_mod[b.slot];
			if (id > 0 && powers->isValid(id) && powers->powers[id]->icon >= 0) {
				int isz = eset->resolutions.icon_size;
				icons->setIcon(powers->powers[id]->icon, Point(b.center.x - isz / 2, b.center.y - isz / 2 + (b.held ? 2 : 0)));
				icons->render();
			}
		}
	}
}

MenuTouchControls::~MenuTouchControls() {
	delete spr_joy_base;
	delete spr_joy_knob;
	delete spr_btn_big;
	delete spr_btn_small;
	delete spr_btn_pause;
}
