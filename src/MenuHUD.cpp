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

#include "Avatar.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "InputState.h"
#include "MenuHUD.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetLabel.h"

namespace {
Sprite* loadSprite(const std::string& filename) {
	if (filename.empty())
		return NULL;
	Image *graphics = render_device->loadImage(filename, RenderDevice::ERROR_NORMAL);
	if (!graphics)
		return NULL;
	Sprite *s = graphics->createSprite();
	graphics->unref();
	return s;
}
}

MenuHUD::MenuHUD()
	: clicked_portrait(false)
	, clicked_config(false)
	, portrait_size(112)
	, portrait_align(Utils::ALIGN_TOPLEFT)
	, portrait(NULL)
	, portrait_frame(NULL)
	, level_badge(NULL)
	, label_level(new WidgetLabel())
	, horde_align(Utils::ALIGN_TOP)
	, horde_box(NULL)
	, label_horde(new WidgetLabel())
	, horde_active(false)
	, horde_wave(0)
	, horde_seconds(0)
	, button_config(NULL) {

	visible = true;

	FileParser infile;
	// @CLASS MenuHUD|Description of menus/hud.txt (Random Dungeon)
	if (infile.open("menus/hud.txt", FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) {
		while (infile.next()) {
			// @ATTR portrait|int, int, int, alignment : X, Y, Size, Alignment|Area of the hero portrait (square). Clicking it opens the inventory.
			if (infile.key == "portrait") {
				portrait_base.x = Parse::popFirstInt(infile.val);
				portrait_base.y = Parse::popFirstInt(infile.val);
				portrait_size = Parse::popFirstInt(infile.val);
				portrait_align = Parse::toAlignment(Parse::popFirstString(infile.val));
			}
			// @ATTR portrait_frame|filename, int, int : Image, X, Y|Frame drawn over the portrait; X/Y is where the portrait sits inside it.
			else if (infile.key == "portrait_frame") {
				portrait_frame = loadSprite(Parse::popFirstString(infile.val));
				portrait_offset.x = Parse::popFirstInt(infile.val);
				portrait_offset.y = Parse::popFirstInt(infile.val);
			}
			// @ATTR level_badge|filename, int, int : Image, X, Y|Badge with the hero level, relative to the portrait frame.
			else if (infile.key == "level_badge") {
				level_badge = loadSprite(Parse::popFirstString(infile.val));
				level_pos.x = Parse::popFirstInt(infile.val);
				level_pos.y = Parse::popFirstInt(infile.val);
			}
			// @ATTR horde_box|filename, int, int, alignment : Image, X, Y, Alignment|Box with the horde wave and time. Only shown in horde mode.
			else if (infile.key == "horde_box") {
				horde_box = loadSprite(Parse::popFirstString(infile.val));
				horde_base.x = Parse::popFirstInt(infile.val);
				horde_base.y = Parse::popFirstInt(infile.val);
				horde_align = Parse::toAlignment(Parse::popFirstString(infile.val));
			}
			// @ATTR button_config|filename, int, int, alignment : Image, X, Y, Alignment|Button that opens the pause/configuration menu.
			else if (infile.key == "button_config") {
				if (!button_config)
					button_config = new WidgetButton(Parse::popFirstString(infile.val));
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				button_config->setBasePos(x, y, Parse::toAlignment(Parse::popFirstString(infile.val)));
				button_config->tooltip = msg->get("Configuration");
			}
			else {
				infile.error("MenuHUD: '%s' is not a valid key.", infile.key.c_str());
			}
		}
		infile.close();
	}

	if (horde_box) {
		horde_base.w = horde_box->getGraphicsWidth();
		horde_base.h = horde_box->getGraphicsHeight();
	}

	label_level->setJustify(FontEngine::JUSTIFY_CENTER);
	label_level->setVAlign(LabelInfo::VALIGN_CENTER);
	label_level->setFont("font_bold");
	label_level->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_horde->setJustify(FontEngine::JUSTIFY_CENTER);
	label_horde->setVAlign(LabelInfo::VALIGN_CENTER);
	label_horde->setFont("font_bold");
	label_horde->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	align();
}

void MenuHUD::align() {
	Menu::align();

	int frame_w = portrait_frame ? portrait_frame->getGraphicsWidth() : portrait_size;
	int frame_h = portrait_frame ? portrait_frame->getGraphicsHeight() : portrait_size;
	portrait_rect.x = portrait_base.x;
	portrait_rect.y = portrait_base.y;
	portrait_rect.w = frame_w;
	portrait_rect.h = frame_h;
	Utils::alignToScreenEdge(portrait_align, &portrait_rect);

	horde_rect = horde_base;
	Utils::alignToScreenEdge(horde_align, &horde_rect);

	if (button_config)
		button_config->setPos(0, 0);
}

void MenuHUD::loadPortrait() {
	if (!pc || pc->stats.gfx_portrait == portrait_loaded)
		return;

	portrait_loaded = pc->stats.gfx_portrait;
	delete portrait;
	portrait = NULL;

	if (portrait_loaded.empty())
		return;

	Image *graphics = render_device->loadImage(portrait_loaded, RenderDevice::ERROR_NORMAL);
	if (!graphics)
		return;

	// resize() libera a imagem original, entao segura uma referencia extra pro cache
	graphics->ref();
	Image *resized = graphics->resize(portrait_size, portrait_size);
	if (resized) {
		portrait = resized->createSprite();
		resized->unref();
	}
	graphics->unref();
}

bool MenuHUD::isWithin(const Point& mouse) {
	if (!visible || !settings->show_hud)
		return false;
	if (Utils::isWithinRect(portrait_rect, mouse))
		return true;
	if (button_config && Utils::isWithinRect(button_config->pos, mouse))
		return true;
	return false;
}

void MenuHUD::setHorde(bool active, int wave, float seconds) {
	horde_active = active;
	horde_wave = wave;
	horde_seconds = seconds;
}

void MenuHUD::logic() {
	if (!visible || !settings->show_hud)
		return;

	loadPortrait();

	if (inpt->usingMouse() && inpt->pressing[Input::MAIN1] && !inpt->lock[Input::MAIN1] && Utils::isWithinRect(portrait_rect, inpt->mouse)) {
		inpt->lock[Input::MAIN1] = true;
		clicked_portrait = true;
	}

	if (button_config) {
		button_config->enabled = !pc->stats.corpse;
		if (button_config->checkClick())
			clicked_config = true;
	}
}

void MenuHUD::render() {
	if (!visible || !settings->show_hud)
		return;

	if (portrait) {
		portrait->setDest(portrait_rect.x + portrait_offset.x, portrait_rect.y + portrait_offset.y);
		render_device->render(portrait);
	}
	if (portrait_frame) {
		portrait_frame->setDest(portrait_rect.x, portrait_rect.y);
		render_device->render(portrait_frame);
	}

	Rect badge;
	badge.x = portrait_rect.x + level_pos.x;
	badge.y = portrait_rect.y + level_pos.y;
	badge.w = level_badge ? level_badge->getGraphicsWidth() : 40;
	badge.h = level_badge ? level_badge->getGraphicsHeight() : 30;
	if (level_badge) {
		level_badge->setDest(badge.x, badge.y);
		render_device->render(level_badge);
	}
	label_level->setPos(badge.x + badge.w / 2, badge.y + badge.h / 2);
	std::stringstream level_ss;
	level_ss << pc->stats.level;
	label_level->setText(level_ss.str());
	label_level->render();

	if (horde_active) {
		if (horde_box) {
			horde_box->setDest(horde_rect.x, horde_rect.y);
			render_device->render(horde_box);
		}
		int total = static_cast<int>(horde_seconds);
		std::stringstream ss;
		ss << msg->getv("Wave %d", horde_wave + 1) << "   " << total / 60 << ":" << (total % 60 < 10 ? "0" : "") << total % 60;
		label_horde->setPos(horde_rect.x + horde_rect.w / 2, horde_rect.y + horde_rect.h / 2);
		label_horde->setText(ss.str());
		label_horde->render();
	}

	if (button_config)
		button_config->render();
}

MenuHUD::~MenuHUD() {
	delete portrait;
	delete portrait_frame;
	delete level_badge;
	delete horde_box;
	delete label_level;
	delete label_horde;
	delete button_config;
}
