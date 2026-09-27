/*
Random Dungeon: run upgrades (see MenuRunUpgrade.h).
*/

#include "MenuRunUpgrade.h"

#include "EngineSettings.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "IconManager.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"
#include "StatBlock.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetLabel.h"

#include <cstdlib>

namespace {
	const int CARD_W = 300;
	const int CARD_H = 270;
	const int CARD_GAP = 30;
}

MenuRunUpgrade::MenuRunUpgrade()
	: Menu()
	, pending(0)
	, last_level(0)
	, in_run(false)
	, label_title(new WidgetLabel())
	, card(NULL)
{
	visible = false;
	for (int i = 0; i < CHOICES; ++i) {
		choice[i] = -1;
		label_name[i] = new WidgetLabel();
		label_desc[i] = new WidgetLabel();
		label_rank[i] = new WidgetLabel();
		button_pick[i] = new WidgetButton("images/menus/run_button.png");
		button_pick[i]->setLabel(msg->get("Choose"));
		button_pick[i]->refresh();
		tablist.add(button_pick[i]);

		label_name[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_name[i]->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
		label_desc[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_desc[i]->setFont("font_small");
		label_desc[i]->setColor(font->getColor(FontEngine::COLOR_WIDGET_NORMAL));
		label_rank[i]->setJustify(FontEngine::JUSTIFY_CENTER);
		label_rank[i]->setFont("font_small");
		label_rank[i]->setColor(font->getColor(FontEngine::COLOR_MENU_BONUS));
	}
	label_title->setJustify(FontEngine::JUSTIFY_CENTER);
	label_title->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
	label_title->setText(msg->get("Level up! Choose an upgrade"));

	Image *graphics = render_device->loadImage("images/menus/run_card.png", RenderDevice::ERROR_NONE);
	if (graphics) {
		card = graphics->createSprite();
		graphics->unref();
	}
	load();
	align();
}

void MenuRunUpgrade::load() {
	defs.clear();
	FileParser infile;
	// @CLASS MenuRunUpgrade|Description of engine/run_upgrades.txt
	if (infile.open("engine/run_upgrades.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE)) {
		while (infile.next()) {
			if (infile.new_section && infile.section == "upgrade")
				defs.push_back(Def());
			if (defs.empty())
				continue;
			Def& d = defs.back();
			// @ATTR upgrade.id|string|Identifier.
			if (infile.key == "id") d.id = infile.val;
			// @ATTR upgrade.name|string|Name shown on the card (translatable).
			else if (infile.key == "name") d.name = msg->get(infile.val);
			// @ATTR upgrade.description|string|One line shown under the name (translatable).
			else if (infile.key == "description") d.description = msg->get(infile.val);
			// @ATTR upgrade.icon|int|Icon id.
			else if (infile.key == "icon") d.icon = Parse::toInt(infile.val);
			// @ATTR upgrade.max_rank|int|How many times it can be picked in one run.
			else if (infile.key == "max_rank") d.max_rank = Parse::toInt(infile.val);
			// @ATTR upgrade.effect|repeatable(string, float) : Kind, Value|Per rank. Kinds: damage, hp, speed, attack_speed (percent); crit, absorb, hp_steal, xp_gain (flat).
			else if (infile.key == "effect") {
				Effect e;
				e.kind = Parse::popFirstString(infile.val);
				e.value = Parse::toFloat(Parse::popFirstString(infile.val));
				d.effects.push_back(e);
			}
		}
		infile.close();
	}
	rank.assign(defs.size(), 0);
}

void MenuRunUpgrade::align() {
	window_area = Rect(0, 0, settings->view_w, settings->view_h);
	setBackgroundColor(Color(8, 3, 4, 170));

	const int total = CHOICES * CARD_W + (CHOICES - 1) * CARD_GAP;
	const int x0 = (settings->view_w - total) / 2;
	const int y0 = settings->view_h / 2 - CARD_H / 2;
	label_title->setPos(settings->view_w / 2, y0 - 44);
	for (int i = 0; i < CHOICES; ++i) {
		card_pos[i] = Rect(x0 + i * (CARD_W + CARD_GAP), y0, CARD_W, CARD_H);
		const int cx = card_pos[i].x + CARD_W / 2;
		label_name[i]->setPos(cx, y0 + 112);
		label_desc[i]->setPos(cx, y0 + 150);
		label_rank[i]->setPos(cx, y0 + 180);
		button_pick[i]->setBasePos(cx - button_pick[i]->pos.w / 2, y0 + CARD_H - 50, Utils::ALIGN_TOPLEFT);
		button_pick[i]->setPos(0, 0);
	}
}

int MenuRunUpgrade::getRank(size_t upgrade) const {
	return upgrade < rank.size() ? rank[upgrade] : 0;
}

void MenuRunUpgrade::roll() {
	std::vector<int> pool;
	for (size_t i = 0; i < defs.size(); ++i) {
		if (rank[i] < defs[i].max_rank)
			pool.push_back(static_cast<int>(i));
	}
	for (int i = 0; i < CHOICES; ++i) {
		choice[i] = -1;
		if (pool.empty())
			continue;
		size_t pick = static_cast<size_t>(rand()) % pool.size();
		choice[i] = pool[pick];
		pool.erase(pool.begin() + static_cast<long>(pick));
	}
	for (int i = 0; i < CHOICES; ++i) {
		const bool has = choice[i] >= 0;
		button_pick[i]->enabled = has;
		button_pick[i]->refresh();
		if (!has) {
			label_name[i]->setText("");
			label_desc[i]->setText("");
			label_rank[i]->setText("");
			continue;
		}
		const Def& d = defs[choice[i]];
		label_name[i]->setText(d.name);
		label_desc[i]->setText(d.description);
		label_rank[i]->setText(msg->getv("Rank %d / %d", rank[choice[i]] + 1, d.max_rank));
	}
}

void MenuRunUpgrade::applyTo(StatBlock *hero) {
	std::fill(hero->run_bonus.begin(), hero->run_bonus.end(), 0.f);
	std::fill(hero->run_mult.begin(), hero->run_mult.end(), 1.f);
	hero->run_speed = 1.f;
	hero->run_attack_speed = 1.f;

	for (size_t i = 0; i < defs.size(); ++i) {
		if (rank[i] <= 0)
			continue;
		for (size_t j = 0; j < defs[i].effects.size(); ++j) {
			const Effect& e = defs[i].effects[j];
			const float total = e.value * static_cast<float>(rank[i]);
			const float mult = 1.f + total / 100.f;
			if (e.kind == "damage") {
				for (size_t t = 0; t < eset->damage_types.list.size(); ++t) {
					hero->run_mult[Stats::COUNT + eset->damage_types.indexToMin(t)] *= mult;
					hero->run_mult[Stats::COUNT + eset->damage_types.indexToMax(t)] *= mult;
				}
			}
			else if (e.kind == "hp") hero->run_mult[Stats::HP_MAX] *= mult;
			else if (e.kind == "speed") hero->run_speed *= mult;
			else if (e.kind == "attack_speed") hero->run_attack_speed *= mult;
			else if (e.kind == "crit") hero->run_bonus[Stats::CRIT] += total;
			else if (e.kind == "absorb") {
				hero->run_bonus[Stats::ABS_MIN] += total;
				hero->run_bonus[Stats::ABS_MAX] += total;
			}
			else if (e.kind == "hp_steal") hero->run_bonus[Stats::HP_STEAL] += total;
			else if (e.kind == "xp_gain") hero->run_bonus[Stats::XP_GAIN] += total;
		}
	}
	// recompute current stats (keeps hp/mp, unlike recalc)
	hero->applyEffects();
	hero->refresh_stats = true;
}

void MenuRunUpgrade::clearRun(StatBlock *hero) {
	rank.assign(defs.size(), 0);
	pending = 0;
	visible = false;
	if (hero)
		applyTo(hero);
}

void MenuRunUpgrade::choose(int slot, StatBlock *hero) {
	if (slot < 0 || slot >= CHOICES || choice[slot] < 0)
		return;
	rank[choice[slot]]++;
	Utils::logInfo("RunUpgrade: picked '%s' (rank %d)", defs[choice[slot]].id.c_str(), rank[choice[slot]]);
	pending--;
	visible = false;
	applyTo(hero);
	// the click that picked must not also swing at the ground
	inpt->lock[Input::MAIN1] = true;
}

void MenuRunUpgrade::update(bool in_run_now, StatBlock *hero) {
	if (!hero || defs.empty())
		return;

	if (in_run_now && !in_run) {
		in_run = true;
		clearRun(hero);
		last_level = hero->level;
	}
	else if (!in_run_now && in_run) {
		in_run = false;
		clearRun(hero);
		return;
	}
	if (!in_run)
		return;

	if (!hero->alive) {
		visible = false;
		return;
	}
	if (hero->level > last_level) {
		pending += hero->level - last_level;
		last_level = hero->level;
	}
	if (!visible && pending > 0) {
		roll();
		visible = (choice[0] >= 0);
		if (!visible)
			pending = 0; // everything maxed
		else
			tablist.setCurrent(button_pick[0]);
		// test hook: RD_RUN_UPGRADE_SHOT=<dir> saves the first choice screen
		static bool shot = false;
		const char *dir = getenv("RD_RUN_UPGRADE_SHOT");
		if (visible && dir && !shot) {
			shot = true;
			render_device->screenshot_request = std::string(dir) + "/run_upgrade.png";
		}
	}
	if (!visible)
		return;

	// automated test hook: RD_RUN_UPGRADE_PICK=<slot> picks that card by itself
	const char *auto_pick = getenv("RD_RUN_UPGRADE_PICK");
	if (auto_pick) {
		choose(atoi(auto_pick), hero);
		return;
	}

	if (!inpt->usingMouse())
		tablist.logic();
	for (int i = 0; i < CHOICES; ++i) {
		if (button_pick[i]->checkClick()) {
			choose(i, hero);
			return;
		}
		const int key = Input::BAR_1 + i;
		if (inpt->pressing[key] && !inpt->lock[key]) {
			inpt->lock[key] = true;
			choose(i, hero);
			return;
		}
	}
}

void MenuRunUpgrade::render() {
	if (!visible)
		return;
	Menu::render(); // dim the game behind

	label_title->render();
	for (int i = 0; i < CHOICES; ++i) {
		if (choice[i] < 0)
			continue;
		if (card) {
			card->setDest(card_pos[i].x, card_pos[i].y);
			render_device->render(card);
		}
		const Def& d = defs[choice[i]];
		if (d.icon >= 0) {
			const int isz = eset->resolutions.icon_size;
			icons->setIcon(d.icon, Point(card_pos[i].x + CARD_W / 2 - isz / 2, card_pos[i].y + 28));
			icons->render();
		}
		label_name[i]->render();
		label_desc[i]->render();
		label_rank[i]->render();
		button_pick[i]->render();
	}
}

MenuRunUpgrade::~MenuRunUpgrade() {
	delete label_title;
	for (int i = 0; i < CHOICES; ++i) {
		delete label_name[i];
		delete label_desc[i];
		delete label_rank[i];
		delete button_pick[i];
	}
	delete card;
}
