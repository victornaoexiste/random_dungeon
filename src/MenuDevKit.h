/*
Random Dungeon: dev kit for the test room.

A small "Kit Dev" button, shown only on the test maps (maps/dev_room.txt and
maps/net_test_room.txt), opens a panel with four tabs:
  Monstros  spawn any rd_* enemy at a chosen level, a training dummy
            (enemies/rd_dummy.txt) with a damage/DPS meter, kill everything
  Itens     search any item by name or id and take it
  Classe    switch to another hero class (re-applies its stats, kit and bar)
  Heroi     infinite stat/power points, god mode, level up, full heal

Everything goes through the engine's normal paths (EntityManager::spawn,
CampaignManager::rewardItem/rewardXP, SaveLoad::loadClass), so it behaves
like the real game. Spawned enemies use EntityManager::spawn, which is not
network-synced (see NetManager.h) -- this is a single-player test tool.
*/

#ifndef MENU_DEV_KIT_H
#define MENU_DEV_KIT_H

#include "CommonIncludes.h"
#include "Menu.h"

#include <deque>
#include <map>

class Entity;
class WidgetButton;
class WidgetInput;
class WidgetLabel;
class WidgetListBox;

class MenuDevKit : public Menu {
public:
	MenuDevKit();
	~MenuDevKit();
	void align();
	void logic();
	void render();

	bool isDevMap();
	bool isMouseOver();
	bool inputFocus();

	// read by MenuCharacter / MenuPowers: while on, free points never run out
	bool infinite_points;
	bool god_mode;

private:
	enum { TAB_MONSTERS = 0, TAB_ITEMS, TAB_CLASS, TAB_HERO, TAB_COUNT };

	void place(WidgetButton *b, int x, int y);
	void place(WidgetLabel *l, int x, int y);
	void buildEnemyList();
	void buildItemList();
	void buildClassList();
	void spawnEnemies(int count);
	void spawnDummy();
	void spawnAt(const std::string& category, const FPoint& pos, int level);
	void resetAttributes();
	void killAll();
	void giveSelectedItem(int quantity);
	void changeClass();
	void levelUp(int levels);
	void updateDamageMeter();
	void refreshLabels();
	void selfTest();

	int tab;
	bool open;
	bool lists_built;

	WidgetButton *button_toggle;
	WidgetButton *tabs[TAB_COUNT];

	// monsters
	WidgetListBox *list_enemies;
	std::vector<std::string> enemy_categories;
	int spawn_level;
	WidgetButton *button_level_down;
	WidgetButton *button_level_up;
	WidgetLabel *label_level;
	WidgetButton *button_spawn1;
	WidgetButton *button_spawn5;
	WidgetButton *button_dummy;
	WidgetButton *button_kill;
	WidgetButton *button_meter_reset;
	WidgetLabel *label_meter[4];

	// items
	WidgetInput *input_filter;
	std::string last_filter;
	WidgetListBox *list_items;
	std::vector<ItemID> item_ids;
	WidgetButton *button_take1;
	WidgetButton *button_take10;
	WidgetButton *button_gold;
	WidgetButton *button_potions;

	// class
	WidgetListBox *list_classes;
	WidgetButton *button_class;
	WidgetLabel *label_class;

	// hero
	WidgetButton *button_points;
	WidgetButton *button_god;
	WidgetButton *button_level1;
	WidgetButton *button_level5;
	WidgetButton *button_heal;
	WidgetButton *button_reset_attr;
	WidgetLabel *label_hero;

	// damage meter (training dummies)
	std::map<Entity*, float> dummy_hp;
	std::deque<std::pair<int, float> > dmg_window; // (tick, damage)
	int ticks;
	float dmg_total;
	float dmg_biggest;

	// RD_DEVKIT_SELFTEST=<dir>: scripted run of every tab, logs + screenshots
	std::string selftest_dir;
	int selftest_tick;
	std::map<std::string, int> slayer_hits;
	std::map<std::string, float> slayer_dmg;
};

#endif
