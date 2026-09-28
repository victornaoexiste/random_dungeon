/*
Random Dungeon: the Sanctuary (see MenuSanctuary.h).
*/

#include "MenuSanctuary.h"

#include "EngineSettings.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "IconManager.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "SaveLoad.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "SoundManager.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetLabel.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
	const int CARD_W = 280;
	const int CARD_H = 200;
	const int GAP = 16;
	const int COLS = 4;
}

MenuSanctuary::MenuSanctuary()
	: Menu()
	, souls(0)
	, loaded_slot(-1)
	, label_title(new WidgetLabel())
	, label_souls(new WidgetLabel())
	, button_close(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, card(NULL)
{
	visible = false;
	loadDefs();

	label_title->setJustify(FontEngine::JUSTIFY_CENTER);
	label_title->setFont("font_region_title");
	label_title->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
	label_title->setText(msg->get("Sanctuary"));
	label_souls->setJustify(FontEngine::JUSTIFY_CENTER);
	label_souls->setColor(font->getColor(FontEngine::COLOR_MENU_BONUS));

	for (size_t i = 0; i < defs.size(); ++i) {
		label_name.push_back(new WidgetLabel());
		label_desc.push_back(new WidgetLabel());
		label_rank.push_back(new WidgetLabel());
		button_buy.push_back(new WidgetButton("images/menus/run_button.png"));
		label_name[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_name[i]->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
		label_desc[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_desc[i]->setFont("font_small");
		label_desc[i]->setColor(font->getColor(FontEngine::COLOR_WIDGET_NORMAL));
		label_rank[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_rank[i]->setFont("font_small");
		label_rank[i]->setColor(font->getColor(FontEngine::COLOR_MENU_BONUS));
		label_name[i]->setText(defs[i].name);
		label_desc[i]->setText(defs[i].description);
		tablist.add(button_buy[i]);
	}
	button_close->setLabel(msg->get("Close"));
	button_close->refresh();
	tablist.add(button_close);

	Image *graphics = render_device->loadImage("images/menus/sanctuary_card.png", RenderDevice::ERROR_NONE);
	if (graphics) {
		card = graphics->createSprite();
		graphics->unref();
	}
	card_pos.resize(defs.size());
	align();
}

void MenuSanctuary::loadDefs() {
	FileParser infile;
	// @CLASS MenuSanctuary|Description of engine/sanctuary.txt
	if (!infile.open("engine/sanctuary.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE))
		return;
	while (infile.next()) {
		if (infile.new_section && infile.section == "blessing")
			defs.push_back(Blessing());
		if (defs.empty())
			continue;
		Blessing& b = defs.back();
		// @ATTR blessing.id|string|Identifier (saved with the character).
		if (infile.key == "id") b.id = infile.val;
		// @ATTR blessing.name|string|Card title (translatable).
		else if (infile.key == "name") b.name = msg->get(infile.val);
		// @ATTR blessing.description|string|One line under the title (translatable).
		else if (infile.key == "description") b.description = msg->get(infile.val);
		// @ATTR blessing.icon|int|Icon id.
		else if (infile.key == "icon") b.icon = Parse::toInt(infile.val);
		// @ATTR blessing.max_rank|int|Ranks that can be bought.
		else if (infile.key == "max_rank") b.max_rank = Parse::toInt(infile.val);
		// @ATTR blessing.cost|int, int : Base, Step|Souls for the next rank: base + step * current rank.
		else if (infile.key == "cost") {
			b.cost_base = Parse::popFirstInt(infile.val);
			b.cost_step = Parse::popFirstInt(infile.val);
		}
		// @ATTR blessing.effect|repeatable(string, float) : Kind, Value|Per rank. Kinds as in engine/run_upgrades.txt, plus gold, item_find and second_chance.
		else if (infile.key == "effect") {
			Effect e;
			e.kind = Parse::popFirstString(infile.val);
			e.value = Parse::toFloat(Parse::popFirstString(infile.val));
			b.effects.push_back(e);
		}
	}
	infile.close();
	rank.assign(defs.size(), 0);
}

/**
 * The save of the character being played (reloaded when the slot changes)
 */
void MenuSanctuary::loadSave() {
	int slot = save_load ? save_load->getGameSlot() : 0;
	if (slot == loaded_slot)
		return;
	loaded_slot = slot;
	souls = 0;
	rank.assign(defs.size(), 0);
	if (slot <= 0)
		return;

	std::stringstream path;
	path << settings->path_user << "saves/" << eset->misc.save_prefix << "/" << slot << "/sanctuary.txt";
	std::ifstream in(path.str().c_str());
	std::string line;
	while (std::getline(in, line)) {
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq), val = line.substr(eq + 1);
		if (key == "souls")
			souls = Parse::toInt(val);
		for (size_t i = 0; i < defs.size(); ++i) {
			if (key == defs[i].id)
				rank[i] = std::min(defs[i].max_rank, Parse::toInt(val));
		}
	}
}

void MenuSanctuary::save() {
	if (loaded_slot <= 0)
		return;
	std::stringstream path;
	path << settings->path_user << "saves/" << eset->misc.save_prefix << "/" << loaded_slot << "/sanctuary.txt";
	std::ofstream out(path.str().c_str());
	out << "# Random Dungeon: Sanctuary (souls and blessings bought)\nsouls=" << souls << "\n";
	for (size_t i = 0; i < defs.size(); ++i)
		out << defs[i].id << "=" << rank[i] << "\n";
}

int MenuSanctuary::awardSouls(int wave_reached, int kills) {
	loadSave();
	int gained = std::max(0, wave_reached * 3 + kills / 5);
	souls += gained;
	save();
	Utils::logInfo("Sanctuary: +%d souls (%d total)", gained, souls);
	return gained;
}

int MenuSanctuary::getSouls() {
	loadSave();
	return souls;
}

float MenuSanctuary::bonus(const std::string& kind) {
	loadSave();
	float total = 0;
	for (size_t i = 0; i < defs.size(); ++i) {
		for (size_t j = 0; j < defs[i].effects.size(); ++j) {
			if (defs[i].effects[j].kind == kind)
				total += defs[i].effects[j].value * static_cast<float>(rank[i]);
		}
	}
	return total;
}

int MenuSanctuary::cost(size_t i) const {
	return defs[i].cost_base + defs[i].cost_step * rank[i];
}

void MenuSanctuary::open() {
	loadSave();
	visible = true;
	refreshLabels();
	if (!button_buy.empty())
		tablist.setCurrent(button_buy[0]);
}

void MenuSanctuary::refreshLabels() {
	label_souls->setText(msg->getv("Souls: %d", souls));
	for (size_t i = 0; i < defs.size(); ++i) {
		bool maxed = rank[i] >= defs[i].max_rank;
		label_rank[i]->setText(msg->getv("Rank %d / %d", rank[i], defs[i].max_rank));
		button_buy[i]->setLabel(maxed ? msg->get("Maximum rank") : msg->getv("Buy (%d)", cost(i)));
		button_buy[i]->enabled = !maxed && souls >= cost(i);
		button_buy[i]->refresh();
	}
}

void MenuSanctuary::align() {
	window_area = Rect(0, 0, settings->view_w, settings->view_h);
	setBackgroundColor(Color(8, 3, 4, 240));

	const int rows = (static_cast<int>(defs.size()) + COLS - 1) / COLS;
	const int total_w = COLS * CARD_W + (COLS - 1) * GAP;
	const int total_h = rows * CARD_H + (rows - 1) * GAP;
	const int x0 = (settings->view_w - total_w) / 2;
	const int y0 = (settings->view_h - total_h) / 2 + 20;
	label_title->setPos(settings->view_w / 2, y0 - 90);
	label_souls->setPos(settings->view_w / 2, y0 - 38);
	for (size_t i = 0; i < defs.size(); ++i) {
		const int c = static_cast<int>(i) % COLS, r = static_cast<int>(i) / COLS;
		card_pos[i] = Rect(x0 + c * (CARD_W + GAP), y0 + r * (CARD_H + GAP), CARD_W, CARD_H);
		const int cx = card_pos[i].x + CARD_W / 2;
		label_name[i]->setPos(cx, card_pos[i].y + 84);
		label_desc[i]->setPos(cx, card_pos[i].y + 112);
		label_rank[i]->setPos(cx, card_pos[i].y + 134);
		button_buy[i]->setBasePos(cx - button_buy[i]->pos.w / 2, card_pos[i].y + CARD_H - 46, Utils::ALIGN_TOPLEFT);
		button_buy[i]->setPos(0, 0);
	}
	button_close->setBasePos(settings->view_w / 2 - button_close->pos.w / 2, y0 + total_h + 20, Utils::ALIGN_TOPLEFT);
	button_close->setPos(0, 0);
}

void MenuSanctuary::logic() {
	if (!visible)
		return;
	loadSave();
	if (!inpt->usingMouse())
		tablist.logic();

	// automated test hook: RD_SANCTUARY_SHOT=<dir> buys what it can, then screenshots
	const char *shot = getenv("RD_SANCTUARY_SHOT");
	if (shot) {
		static int f = 0;
		++f;
		if (f == 10) {
			for (size_t i = 0; i < defs.size(); ++i) {
				if (rank[i] < defs[i].max_rank && souls >= cost(i)) {
					souls -= cost(i);
					rank[i]++;
				}
			}
			save();
			refreshLabels();
		}
		if (f == 30)
			render_device->screenshot_request = std::string(shot) + "/sanctuary.png";
	}

	for (size_t i = 0; i < defs.size(); ++i) {
		if (button_buy[i]->checkClick() && rank[i] < defs[i].max_rank && souls >= cost(i)) {
			souls -= cost(i);
			rank[i]++;
			save();
			refreshLabels();
			Utils::logInfo("Sanctuary: bought %s rank %d (%d souls left)", defs[i].id.c_str(), rank[i], souls);
		}
	}
	if (button_close->checkClick() || (inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL])) {
		inpt->lock[Input::CANCEL] = true;
		visible = false;
	}
}

void MenuSanctuary::render() {
	if (!visible)
		return;
	Menu::render();
	label_title->render();
	label_souls->render();
	for (size_t i = 0; i < defs.size(); ++i) {
		if (card) {
			card->setDest(card_pos[i].x, card_pos[i].y);
			render_device->render(card);
		}
		if (defs[i].icon >= 0) {
			const int isz = eset->resolutions.icon_size;
			icons->setIcon(defs[i].icon, Point(card_pos[i].x + CARD_W / 2 - isz / 2, card_pos[i].y + 12));
			icons->render();
		}
		label_name[i]->render();
		label_desc[i]->render();
		label_rank[i]->render();
		button_buy[i]->render();
	}
	button_close->render();
}

MenuSanctuary::~MenuSanctuary() {
	delete label_title;
	delete label_souls;
	for (size_t i = 0; i < defs.size(); ++i) {
		delete label_name[i];
		delete label_desc[i];
		delete label_rank[i];
		delete button_buy[i];
	}
	delete button_close;
	delete card;
}
