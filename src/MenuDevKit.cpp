/*
Random Dungeon: dev kit for the test room. See MenuDevKit.h.
*/

#include "Avatar.h"
#include "CampaignManager.h"
#include "EngineSettings.h"
#include "Entity.h"
#include "EntityManager.h"
#include "EventManager.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "InputState.h"
#include "ItemManager.h"
#include "MapRenderer.h"
#include "Menu.h"
#include "MenuCharacter.h"
#include "MenuDevKit.h"
#include "MenuInventory.h"
#include "MenuManager.h"
#include "MessageEngine.h"
#include "NPC.h"
#include "NPCManager.h"
#include "ModManager.h"
#include "UtilsFileSystem.h"
#include "PowerManager.h"
#include "RenderDevice.h"
#include "SaveLoad.h"
#include "Settings.h"
#include "NetManager.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetInput.h"
#include "WidgetLabel.h"
#include "WidgetListBox.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {
	const std::string BUTTON = "images/menus/devkit/button.png";
	const std::string BUTTON_SMALL = "images/menus/devkit/button_small.png";
	const std::string BUTTON_TAB = "images/menus/devkit/button_tab.png";
	const std::string BUTTON_WIDE = "images/menus/devkit/button_wide.png";
	const std::string LISTBOX = "images/menus/devkit/listbox.png";
	const std::string DUMMY = "enemies/rd_dummy.txt";
	bool isDummy(const std::string& type) { return type.compare(0, 16, "enemies/rd_dummy") == 0; } // rd_dummy, rd_dummy_undead
	const int PANEL_W = 400;
	const int PANEL_H = 540;
	const int METER_SECONDS = 5;
	const int MAX_ITEM_ROWS = 400;

	WidgetButton* makeButton(const std::string& file, const std::string& label) {
		WidgetButton *b = new WidgetButton(file);
		b->setLabel(label);
		b->refresh();
		return b;
	}

	std::string lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		return s;
	}

	std::string fmt(float v) {
		std::stringstream ss;
		ss << static_cast<long>(v + 0.5f);
		return ss.str();
	}
}

MenuDevKit::MenuDevKit()
	: Menu()
	, infinite_points(false)
	, god_mode(false)
	, tab(TAB_MONSTERS)
	, open(false)
	, lists_built(false)
	, spawn_level(1)
	, ticks(0)
	, dmg_total(0)
	, dmg_biggest(0)
	, selftest_tick(0)
{
	const char* st = getenv("RD_DEVKIT_SELFTEST");
	if (st)
		selftest_dir = st;

	button_toggle = makeButton(BUTTON_TAB, msg->get("Dev Kit"));

	const char* tab_names[TAB_COUNT] = {"Monsters", "Items", "Class", "Hero"};
	for (int i = 0; i < TAB_COUNT; ++i)
		tabs[i] = makeButton(BUTTON_TAB, msg->get(tab_names[i]));

	// monsters
	list_enemies = new WidgetListBox(11, LISTBOX);
	list_enemies->can_deselect = false;
	button_level_down = makeButton(BUTTON_SMALL, "-");
	button_level_up = makeButton(BUTTON_SMALL, "+");
	label_level = new WidgetLabel();
	button_spawn1 = makeButton(BUTTON, msg->get("Spawn 1"));
	button_spawn5 = makeButton(BUTTON, msg->get("Spawn 5"));
	button_dummy = makeButton(BUTTON, msg->get("Training Dummy"));
	button_kill = makeButton(BUTTON, msg->get("Kill All"));
	button_meter_reset = makeButton(BUTTON, msg->get("Reset Meter"));
	for (int i = 0; i < 4; ++i)
		label_meter[i] = new WidgetLabel();

	// items
	input_filter = new WidgetInput(WidgetInput::DEFAULT_FILE);
	list_items = new WidgetListBox(11, LISTBOX);
	list_items->can_deselect = false;
	button_take1 = makeButton(BUTTON, msg->get("Take 1"));
	button_take10 = makeButton(BUTTON, msg->get("Take 10"));
	button_gold = makeButton(BUTTON, msg->get("+1000 Gold"));
	button_potions = makeButton(BUTTON, msg->get("+10 Potions"));

	// class
	list_classes = new WidgetListBox(6, LISTBOX);
	list_classes->can_deselect = false;
	button_class = makeButton(BUTTON, msg->get("Switch Class"));
	label_class = new WidgetLabel();

	// hero
	button_points = makeButton(BUTTON_WIDE, "");
	button_god = makeButton(BUTTON_WIDE, "");
	button_level1 = makeButton(BUTTON, msg->get("+1 Level"));
	button_level5 = makeButton(BUTTON, msg->get("+5 Levels"));
	button_heal = makeButton(BUTTON, msg->get("Full Heal"));
	button_reset_attr = makeButton(BUTTON, msg->get("Reset Attributes"));
	label_hero = new WidgetLabel();

	visible = false;
	align();
}

MenuDevKit::~MenuDevKit() {
	delete button_toggle;
	for (int i = 0; i < TAB_COUNT; ++i)
		delete tabs[i];
	delete list_enemies;
	delete button_level_down;
	delete button_level_up;
	delete label_level;
	delete button_spawn1;
	delete button_spawn5;
	delete button_dummy;
	delete button_kill;
	delete button_meter_reset;
	for (int i = 0; i < 4; ++i)
		delete label_meter[i];
	delete input_filter;
	delete list_items;
	delete button_take1;
	delete button_take10;
	delete button_gold;
	delete button_potions;
	delete list_classes;
	delete button_class;
	delete label_class;
	delete button_points;
	delete button_god;
	delete button_level1;
	delete button_level5;
	delete button_heal;
	delete button_reset_attr;
	delete label_hero;
}

void MenuDevKit::place(WidgetButton *b, int x, int y) {
	b->setBasePos(x, y, Utils::ALIGN_TOPLEFT);
	b->setPos(window_area.x, window_area.y);
}

void MenuDevKit::place(WidgetLabel *l, int x, int y) {
	l->setBasePos(x, y, Utils::ALIGN_TOPLEFT);
	l->setPos(window_area.x, window_area.y);
	l->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
}

void MenuDevKit::align() {
	// panel on the left, under the hp/mp bars; toggle button at the top centre
	setWindowPos(12, 110);
	alignment = Utils::ALIGN_TOPLEFT;
	window_area.w = PANEL_W;
	window_area.h = PANEL_H;
	// themed panel art if the UI mod ships one, plain translucent box otherwise
	if (Filesystem::fileExists(mods->locate("images/menus/devkit/panel.png")))
		setBackground("images/menus/devkit/panel.png");
	else
		setBackgroundColor(Color(18, 15, 12, 225));
	Menu::align();

	button_toggle->setBasePos(-36, 8, Utils::ALIGN_TOP);
	button_toggle->setPos(0, 0);

	for (int i = 0; i < TAB_COUNT; ++i)
		place(tabs[i], 10 + i * 95, 10);

	// monsters
	list_enemies->setBasePos(10, 46, Utils::ALIGN_TOPLEFT);
	list_enemies->setPos(window_area.x, window_area.y);
	place(label_level, 10, 305);
	place(button_level_down, 250, 296);
	place(button_level_up, 290, 296);
	place(button_spawn1, 10, 330);
	place(button_spawn5, 180, 330);
	place(button_dummy, 10, 362);
	place(button_kill, 180, 362);
	place(button_meter_reset, 10, 394);
	for (int i = 0; i < 4; ++i)
		place(label_meter[i], 10, 434 + i * 22);

	// items
	input_filter->setBasePos(10, 46, Utils::ALIGN_TOPLEFT);
	input_filter->setPos(window_area.x, window_area.y);
	input_filter->resize(330);
	list_items->setBasePos(10, 84, Utils::ALIGN_TOPLEFT);
	list_items->setPos(window_area.x, window_area.y);
	place(button_take1, 10, 340);
	place(button_take10, 180, 340);
	place(button_gold, 10, 372);
	place(button_potions, 180, 372);

	// class
	list_classes->setBasePos(10, 46, Utils::ALIGN_TOPLEFT);
	list_classes->setPos(window_area.x, window_area.y);
	place(button_class, 10, 190);
	place(label_class, 10, 232);

	// hero
	place(button_points, 10, 46);
	place(button_god, 10, 78);
	place(button_level1, 10, 110);
	place(button_level5, 180, 110);
	place(button_heal, 10, 142);
	place(button_reset_attr, 180, 142);
	place(label_hero, 10, 184);
}

bool MenuDevKit::isDevMap() {
	// network clients don't get the kit: what it spawns is decided by the host
	if (netmgr && netmgr->isClient())
		return false;
	const std::string map = mapr->getFilename();
	return map == "maps/dev_room.txt" || map == "maps/net_test_room.txt";
}

bool MenuDevKit::isMouseOver() {
	if (!isDevMap())
		return false;
	if (Utils::isWithinRect(button_toggle->pos, inpt->mouse))
		return true;
	return open && Utils::isWithinRect(window_area, inpt->mouse);
}

bool MenuDevKit::inputFocus() {
	return open && tab == TAB_ITEMS && input_filter->edit_mode;
}

// ------------------------------------------------------------------ lists
void MenuDevKit::buildEnemyList() {
	list_enemies->clear();
	enemy_categories.clear();
	std::vector<std::string> files = mods->list("enemies", !ModManager::LIST_FULL_PATHS);
	std::vector<std::pair<std::string, std::string> > found; // (name, category)
	for (size_t i = 0; i < files.size(); ++i) {
		// our enemies use their file name as their own category (rd_x.txt -> rd_x)
		if (files[i].compare(0, 11, "enemies/rd_") != 0)
			continue;
		std::string category = files[i].substr(8, files[i].size() - 8 - 4);
		std::string name = category;
		FileParser infile;
		if (infile.open(files[i], FileParser::MOD_FILE, FileParser::ERROR_NONE)) {
			while (infile.next()) {
				if (infile.key == "name")
					name = msg->get(infile.val);
			}
			infile.close();
		}
		found.push_back(std::make_pair(name, category));
	}
	std::sort(found.begin(), found.end());
	for (size_t i = 0; i < found.size(); ++i) {
		list_enemies->append(found[i].first, found[i].second);
		enemy_categories.push_back(found[i].second);
	}
	if (!found.empty())
		list_enemies->select(0);
}

void MenuDevKit::buildItemList() {
	list_items->clear();
	item_ids.clear();
	std::string filter = lower(input_filter->getText());
	for (ItemID id = 1; id < items->items.size() && item_ids.size() < MAX_ITEM_ROWS; ++id) {
		if (!items->isValid(id))
			continue;
		std::string name = items->getItemName(id);
		std::stringstream idstr;
		idstr << id;
		if (!filter.empty() && lower(name).find(filter) == std::string::npos && idstr.str().find(filter) == std::string::npos)
			continue;
		list_items->append(idstr.str() + "  " + name, "");
		item_ids.push_back(id);
	}
	if (!item_ids.empty())
		list_items->select(0);
	last_filter = input_filter->getText();
}

void MenuDevKit::buildClassList() {
	list_classes->clear();
	for (size_t i = 0; i < eset->hero_classes.list.size(); ++i)
		list_classes->append(msg->get(eset->hero_classes.list[i].name), "");
	if (list_classes->getSize() > 0)
		list_classes->select(0);
}

// ------------------------------------------------------------------ actions
void MenuDevKit::spawnEnemies(int count) {
	int sel = list_enemies->getSelected();
	if (sel < 0 || static_cast<size_t>(sel) >= enemy_categories.size())
		return;
	EventComponent ec;
	std::stringstream ss;
	ss << "fixed," << spawn_level;
	ec.s = ss.str();
	for (int i = 0; i < count; ++i) {
		FPoint p = mapr->collider.getRandomNeighbor(Point(pc->stats.pos), 4, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_ALL_ENTITIES);
		spawnAt(enemy_categories[sel], p, spawn_level);
	}
}

/**
 * Same path as HordeManager::spawnOne: a real Entity with a net_id, so when
 * this is the host the monster is also sent to every connected client.
 */
void MenuDevKit::spawnAt(const std::string& category, const FPoint& pos, int level) {
	if (!mapr->collider.isValidPosition(pos.x, pos.y, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_ALL_ENTITIES))
		return;
	Entity *e = entitym->getEntityPrototype("enemies/" + category + ".txt");
	if (!e)
		return;
	e->stats.pos = pos;
	e->stats.direction = static_cast<unsigned char>(rand() % 8);
	if (level > 0)
		e->stats.level = level;
	e->stats.recalc();
	e->stats.net_id = entitym->next_net_id++;
	entitym->entities.push_back(e);
	mapr->collider.block(pos.x, pos.y, !MapCollision::IS_ALLY);
}

/**
 * Gives back every spent attribute point (primary stats back to the class
 * start). Also done automatically when "infinite points" is turned off with
 * more points spent than the level allows.
 */
void MenuDevKit::resetAttributes() {
	for (size_t i = 0; i < eset->primary_stats.list.size(); ++i)
		pc->stats.primary[i] = pc->stats.primary_starting[i];
	pc->stats.recalc();
	menu->inv->changed_equipment = true;
	menu->chr->refreshStats();
}

void MenuDevKit::spawnDummy() {
	FPoint p = mapr->collider.getRandomNeighbor(Point(pc->stats.pos), 3, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_ALL_ENTITIES);
	spawnAt("rd_dummy", p, 0);
}

void MenuDevKit::killAll() {
	for (size_t i = 0; i < entitym->entities.size(); ++i) {
		StatBlock& s = entitym->entities[i]->stats;
		if (s.hero || s.hero_ally || !s.alive || s.net_proxy || s.hp <= 0)
			continue;
		if (isDummy(entitym->entities[i]->type_filename))
			continue; // training dummies stay
		s.takeDamage(s.hp + 1, false, Power::SOURCE_TYPE_HERO);
	}
}

void MenuDevKit::giveSelectedItem(int quantity) {
	int sel = list_items->getSelected();
	if (sel < 0 || static_cast<size_t>(sel) >= item_ids.size())
		return;
	ItemID id = item_ids[sel];
	int max_q = items->items[id]->max_quantity;
	for (int given = 0; given < quantity; given += max_q) {
		ItemStack stack(id, std::min(max_q, quantity - given));
		camp->rewardItem(stack);
	}
}

void MenuDevKit::changeClass() {
	int sel = list_classes->getSelected();
	if (sel < 0)
		return;

	// the current gear goes to the bag, so the new class kit can be equipped
	MenuItemStorage& equipment = menu->inv->inventory[MenuInventory::EQUIPMENT];
	for (int i = 0; i < equipment.getSlotNumber(); ++i) {
		if (!equipment[i].empty()) {
			menu->inv->add(equipment[i], MenuInventory::CARRIED, ItemStorage::NO_SLOT, !MenuInventory::ADD_PLAY_SOUND, !MenuInventory::ADD_AUTO_EQUIP);
			equipment[i].clear();
		}
	}

	// back to base stats (Avatar::init starts every primary at 1), then apply
	// the class exactly like a new game does; spent points become free again
	for (size_t i = 0; i < eset->primary_stats.list.size(); ++i)
		pc->stats.primary[i] = 1;
	pc->stats.powers_list.clear();
	save_load->loadClass(sel);
	menu->inv->changed_equipment = true;
}

void MenuDevKit::levelUp(int levels) {
	int target = std::min(pc->stats.level + levels, eset->xp.getMaxLevel());
	unsigned long need = eset->xp.getLevelXP(target);
	if (need > pc->stats.xp)
		camp->rewardXP(static_cast<float>(need - pc->stats.xp), CampaignManager::XP_SHOW_MSG);
}

void MenuDevKit::updateDamageMeter() {
	ticks++;
	std::map<Entity*, float> seen;
	for (size_t i = 0; i < entitym->entities.size(); ++i) {
		Entity *e = entitym->entities[i];
		if (!isDummy(e->type_filename) || !e->stats.alive)
			continue;
		float hp = e->stats.hp;
		std::map<Entity*, float>::iterator it = dummy_hp.find(e);
		if (it != dummy_hp.end() && it->second > hp) {
			float dmg = it->second - hp;
			dmg_total += dmg;
			dmg_biggest = std::max(dmg_biggest, dmg);
			dmg_window.push_back(std::make_pair(ticks, dmg));
		}
		seen[e] = hp;
	}
	dummy_hp.swap(seen);

	int window_ticks = METER_SECONDS * settings->max_frames_per_sec;
	while (!dmg_window.empty() && dmg_window.front().first < ticks - window_ticks)
		dmg_window.pop_front();
}

void MenuDevKit::refreshLabels() {
	std::stringstream lvl;
	lvl << msg->get("Spawn level") << ": " << spawn_level;
	label_level->setText(lvl.str());

	float window = 0;
	for (size_t i = 0; i < dmg_window.size(); ++i)
		window += dmg_window[i].second;
	label_meter[0]->setText(msg->get("Dummies") + ": " + fmt(static_cast<float>(dummy_hp.size())));
	label_meter[1]->setText(msg->get("Total damage") + ": " + fmt(dmg_total));
	label_meter[2]->setText(msg->get("DPS (last 5s)") + ": " + fmt(window / METER_SECONDS));
	label_meter[3]->setText(msg->get("Biggest hit") + ": " + fmt(dmg_biggest));

	label_class->setText(msg->get("Current class") + ": " + msg->get(pc->stats.character_class));

	button_points->setLabel(std::string(msg->get("Infinite points")) + (infinite_points ? ": ON" : ": OFF"));
	button_points->refresh();
	button_god->setLabel(std::string(msg->get("God mode")) + (god_mode ? ": ON" : ": OFF"));
	button_god->refresh();

	std::stringstream hero;
	hero << msg->get("Level") << " " << pc->stats.level << "  |  XP " << pc->stats.xp;
	label_hero->setText(hero.str());
}

/**
 * Automated check of every tab, for testing without a person at the mouse:
 * RD_DEVKIT_SELFTEST=/some/dir ./flare --load-slot=1 --mode=test
 * Results go to the log ("DevKit selftest: ..."), screenshots to that dir.
 */
void MenuDevKit::selfTest() {
	const int t = ++selftest_tick;
	const std::string dir = selftest_dir + "/";
	if (t == 60) {
		if (!lists_built) { buildEnemyList(); buildItemList(); buildClassList(); lists_built = true; }
		open = true;
		god_mode = true;
		tab = TAB_MONSTERS;
		spawn_level = 5;
		spawnEnemies(3);
		spawnDummy();
		Utils::logInfo("DevKit selftest: %d enemy types, %d items listed, %d classes", list_enemies->getSize(), list_items->getSize(), list_classes->getSize());
		// vendors placed in the dev room: what did their random stock roll?
		for (size_t i = 0; i < npcs->npcs.size(); ++i) {
			NPC *n = npcs->npcs[i];
			int filled = 0;
			std::string sample;
			for (int j = 0; j < n->stock.getSlotNumber(); ++j) {
				if (n->stock[j].empty()) continue;
				if (filled < 3) sample += (filled ? ", " : "") + items->getItemName(n->stock[j].item);
				filled++;
			}
			Utils::logInfo("DevKit selftest: npc '%s' vendor=%d stock=%d [%s]", n->name.c_str(), n->vendor, filled, sample.c_str());
		}
	}
	else if (t == 120) {
		int n = 0;
		for (size_t i = 0; i < entitym->entities.size(); ++i) {
			Entity *e = entitym->entities[i];
			if (isDummy(e->type_filename)) { e->stats.takeDamage(250, false, Power::SOURCE_TYPE_HERO); n++; }
		}
		Utils::logInfo("DevKit selftest: %u entities on map, hit %d dummies for 250", static_cast<unsigned>(entitym->entities.size()), n);
	}
	else if (t == 121 || t == 135) {
		for (size_t i = 0; i < entitym->entities.size(); ++i) {
			Entity *e = entitym->entities[i];
			if (isDummy(e->type_filename))
				Utils::logInfo("DevKit selftest: t=%d dummy hp=%.0f/%.0f alive=%d tracked=%u", t, e->stats.hp, e->stats.get(Stats::HP_MAX), e->stats.alive, static_cast<unsigned>(dummy_hp.size()));
		}
	}
	else if (t == 150) {
		Utils::logInfo("DevKit selftest: meter total=%.0f biggest=%.0f window_hits=%u", dmg_total, dmg_biggest, static_cast<unsigned>(dmg_window.size()));
		render_device->screenshot_request = dir + "devkit_monsters.png";
	}
	else if (t == 170) {
		tab = TAB_ITEMS;
		input_filter->setText("501");
	}
	else if (t == 200) {
		int before = menu->inv->inventory[MenuInventory::CARRIED].count(item_ids.empty() ? 0 : item_ids[0]);
		giveSelectedItem(1);
		int after = menu->inv->inventory[MenuInventory::CARRIED].count(item_ids.empty() ? 0 : item_ids[0]);
		Utils::logInfo("DevKit selftest: filter '501' -> %d items, took '%s' (%d -> %d in bag)", list_items->getSize(),
			item_ids.empty() ? "-" : items->getItemName(item_ids[0]).c_str(), before, after);
		render_device->screenshot_request = dir + "devkit_items.png";
	}
	else if (t == 240) {
		tab = TAB_CLASS;
		list_classes->select(0); // Warrior: may use the Silver Longsword (slayer test below)
		std::string before = pc->stats.character_class;
		changeClass();
		Utils::logInfo("DevKit selftest: class %s -> %s, powers=%u", before.c_str(), pc->stats.character_class.c_str(), static_cast<unsigned>(pc->stats.powers_list.size()));
	}
	else if (t == 260) {
		render_device->screenshot_request = dir + "devkit_class.png";
	}
	else if (t == 280) {
		tab = TAB_HERO;
		int lvl = pc->stats.level;
		infinite_points = true;
		levelUp(1);
		menu->chr->refreshStats();
		menu->chr->visible = true;
		Utils::logInfo("DevKit selftest: level %d, asked +1 (xp now %lu), infinite points on", lvl, pc->stats.xp);
	}
	else if (t == 290) {
		// slayer check: equip the Silver Longsword (5012, slayer=undead,25)
		if (items->isValid(5012)) {
			int slot = menu->inv->getEquipSlotFromItem(5012, !MenuInventory::ONLY_EMPTY_SLOTS);
			MenuItemStorage& eq = menu->inv->inventory[MenuInventory::EQUIPMENT];
			if (slot >= 0) {
				if (!eq[slot].empty())
					menu->inv->add(eq[slot], MenuInventory::CARRIED, ItemStorage::NO_SLOT, !MenuInventory::ADD_PLAY_SOUND, !MenuInventory::ADD_AUTO_EQUIP);
				eq[slot] = ItemStack(5012, 1);
				menu->inv->changed_equipment = true;
			}
		}
	}
	else if (t == 310) {
		std::string sl;
		for (std::map<std::string, int>::const_iterator it = pc->stats.slayer_bonus.begin(); it != pc->stats.slayer_bonus.end(); ++it) {
			std::stringstream ss; ss << it->first << "+" << it->second << "% ";
			sl += ss.str();
		}
		Utils::logInfo("DevKit selftest: slayer bonuses with Silver Longsword: %s", sl.empty() ? "(none)" : sl.c_str());
		Utils::logInfo("DevKit selftest: level after level-up: %d", pc->stats.level);
		render_device->screenshot_request = dir + "devkit_hero.png";
	}
	else if (t == 330) {
		menu->chr->visible = false;
		killAll();
	}
	else if (t == 380) {
		// real-hit slayer test: a plain and an undead dummy right next to the hero
		spawnAt("rd_dummy", FPoint(static_cast<float>(static_cast<int>(pc->stats.pos.x) + 1) + 0.5f, static_cast<float>(static_cast<int>(pc->stats.pos.y)) + 0.5f), 0);
		spawnAt("rd_dummy_undead", FPoint(static_cast<float>(static_cast<int>(pc->stats.pos.x)) + 0.5f, static_cast<float>(static_cast<int>(pc->stats.pos.y) + 1) + 0.5f), 0);
	}
	if (t >= 400 && t < 500) {
		// per-hit bookkeeping for the slayer test: every hp drop is one hit
		static std::map<Entity*, float> last;
		for (size_t i = 0; i < entitym->entities.size(); ++i) {
			Entity *e = entitym->entities[i];
			if (!isDummy(e->type_filename) || Utils::calcDist(e->stats.pos, pc->stats.pos) >= 3.f)
				continue;
			if (last.count(e) && e->stats.hp < last[e]) {
				slayer_hits[e->type_filename] += 1;
				slayer_dmg[e->type_filename] += last[e] - e->stats.hp;
			}
			last[e] = e->stats.hp;
		}
	}
	if (t >= 400 && t < 480 && t % 2 == 0) {
		// swing (power 1) alternately at each of the two new dummies
		std::vector<Entity*> targets;
		for (size_t i = 0; i < entitym->entities.size(); ++i) {
			Entity *e = entitym->entities[i];
			if (isDummy(e->type_filename) && Utils::calcDist(e->stats.pos, pc->stats.pos) < 3.f)
				targets.push_back(e);
		}
		if (!targets.empty()) {
			Entity *e = targets[(t / 2) % targets.size()];
			pc->stats.hp = static_cast<float>(pc->stats.get(Stats::HP_MAX));
			pc->stats.direction = Utils::calcDirection(pc->stats.pos.x, pc->stats.pos.y, e->stats.pos.x, e->stats.pos.y);
			powers->activate(1, &pc->stats, pc->stats.pos, e->stats.pos);
		}
	}
	if (t == 500) {
		for (std::map<std::string, int>::iterator it = slayer_hits.begin(); it != slayer_hits.end(); ++it)
			Utils::logInfo("DevKit selftest: slayer hit test %s: %d hits, %.1f damage per hit", it->first.c_str(), it->second, static_cast<double>(slayer_dmg[it->first] / static_cast<float>(it->second)));
	}
	else if (t == 360) {
		int alive = 0;
		for (size_t i = 0; i < entitym->entities.size(); ++i) {
			const StatBlock& s = entitym->entities[i]->stats;
			if (!s.hero && !s.hero_ally && s.alive && s.hp > 0) alive++;
		}
		Utils::logInfo("DevKit selftest: after Kill All, %d hostile alive. done.", alive);
	}
}

// ------------------------------------------------------------------ frame
void MenuDevKit::logic() {
	if (!isDevMap()) {
		open = false;
		visible = false;
		return;
	}

	if (god_mode && pc->stats.alive) {
		pc->stats.hp = static_cast<float>(pc->stats.get(Stats::HP_MAX));
		pc->stats.mp = static_cast<float>(pc->stats.get(Stats::MP_MAX));
	}
	updateDamageMeter();
	if (!selftest_dir.empty())
		selfTest();

	if (button_toggle->checkClick()) {
		open = !open;
		if (open && !lists_built) {
			buildEnemyList();
			buildItemList();
			buildClassList();
			lists_built = true;
		}
	}
	visible = open;
	if (!open)
		return;

	for (int i = 0; i < TAB_COUNT; ++i) {
		if (tabs[i]->checkClick())
			tab = i;
	}

	if (tab == TAB_MONSTERS) {
		list_enemies->checkClick();
		if (button_level_down->checkClick()) spawn_level = std::max(1, spawn_level - 1);
		if (button_level_up->checkClick()) spawn_level = std::min(50, spawn_level + 1);
		if (button_spawn1->checkClick()) spawnEnemies(1);
		if (button_spawn5->checkClick()) spawnEnemies(5);
		if (button_dummy->checkClick()) spawnDummy();
		if (button_kill->checkClick()) killAll();
		if (button_meter_reset->checkClick()) {
			dmg_total = dmg_biggest = 0;
			dmg_window.clear();
			// heal the dummies back to full
			for (size_t i = 0; i < entitym->entities.size(); ++i) {
				Entity *e = entitym->entities[i];
				if (isDummy(e->type_filename) && e->stats.alive)
					e->stats.hp = static_cast<float>(e->stats.get(Stats::HP_MAX));
			}
			dummy_hp.clear();
		}
	}
	else if (tab == TAB_ITEMS) {
		input_filter->logic();
		if (input_filter->getText() != last_filter)
			buildItemList();
		list_items->checkClick();
		if (button_take1->checkClick()) giveSelectedItem(1);
		if (button_take10->checkClick()) giveSelectedItem(10);
		if (button_gold->checkClick()) camp->rewardCurrency(1000);
		if (button_potions->checkClick()) {
			camp->rewardItem(ItemStack(2, 10));
			camp->rewardItem(ItemStack(3, 10));
		}
	}
	else if (tab == TAB_CLASS) {
		list_classes->checkClick();
		if (button_class->checkClick()) changeClass();
	}
	else if (tab == TAB_HERO) {
		if (button_points->checkClick()) {
			infinite_points = !infinite_points;
			if (!infinite_points) {
				// more points spent than the level gives: hand them back
				int spent = 0;
				for (size_t i = 0; i < eset->primary_stats.list.size(); ++i)
					spent += pc->stats.primary[i] - pc->stats.primary_starting[i];
				if (spent > (pc->stats.level - 1) * pc->stats.stat_points_per_level)
					resetAttributes();
			}
			menu->chr->refreshStats();
		}
		if (button_reset_attr->checkClick()) resetAttributes();
		if (button_god->checkClick()) god_mode = !god_mode;
		if (button_level1->checkClick()) levelUp(1);
		if (button_level5->checkClick()) levelUp(5);
		if (button_heal->checkClick()) {
			pc->stats.hp = static_cast<float>(pc->stats.get(Stats::HP_MAX));
			pc->stats.mp = static_cast<float>(pc->stats.get(Stats::MP_MAX));
		}
	}

	refreshLabels();
}

void MenuDevKit::render() {
	if (!isDevMap())
		return;

	button_toggle->render();
	if (!open)
		return;

	Menu::render(); // background

	for (int i = 0; i < TAB_COUNT; ++i)
		tabs[i]->render();

	if (tab == TAB_MONSTERS) {
		list_enemies->render();
		label_level->render();
		button_level_down->render();
		button_level_up->render();
		button_spawn1->render();
		button_spawn5->render();
		button_dummy->render();
		button_kill->render();
		button_meter_reset->render();
		for (int i = 0; i < 4; ++i)
			label_meter[i]->render();
	}
	else if (tab == TAB_ITEMS) {
		input_filter->render();
		list_items->render();
		button_take1->render();
		button_take10->render();
		button_gold->render();
		button_potions->render();
	}
	else if (tab == TAB_CLASS) {
		list_classes->render();
		button_class->render();
		label_class->render();
	}
	else if (tab == TAB_HERO) {
		button_points->render();
		button_god->render();
		button_level1->render();
		button_level5->render();
		button_heal->render();
		button_reset_attr->render();
		label_hero->render();
	}
}
