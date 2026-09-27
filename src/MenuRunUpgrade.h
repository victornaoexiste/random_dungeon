/*
Random Dungeon: run upgrades (Infinite Run).

Every level gained during a run offers 3 random upgrades out of the ones in
engine/run_upgrades.txt; the player picks one (mouse, touch, gamepad or the
1/2/3 keys). Upgrades stack up to their max rank and only last for the run:
they're cleared when the hero leaves the arena or the run ends. They act
through StatBlock::run_bonus / run_mult / run_speed / run_attack_speed.

Single-player pauses while the choice is open (see GameStatePlay::isPaused);
multiplayer doesn't, each player picks their own upgrades.
*/

#ifndef MENU_RUN_UPGRADE_H
#define MENU_RUN_UPGRADE_H

#include "CommonIncludes.h"
#include "Menu.h"
#include "Utils.h"

class StatBlock;
class WidgetButton;
class WidgetLabel;

class MenuRunUpgrade : public Menu {
public:
	static const int CHOICES = 3;

	MenuRunUpgrade();
	~MenuRunUpgrade();

	// once per frame, before the game logic; in_run = the hero is in an arena
	void update(bool in_run, StatBlock *hero);
	void align();
	void render();

	// ranks picked this run, e.g. for a summary
	int getRank(size_t upgrade) const;
	size_t count() const { return defs.size(); }

private:
	class Effect {
	public:
		std::string kind; // damage, hp, speed, attack_speed, crit, absorb, hp_steal, xp_gain
		float value;      // percent for damage/hp/speed/attack_speed, flat otherwise
	};
	class Def {
	public:
		Def() : icon(-1), max_rank(5) {}
		std::string id;
		std::string name;
		std::string description;
		int icon;
		int max_rank;
		std::vector<Effect> effects;
	};

	void load();
	void roll();
	void choose(int slot, StatBlock *hero);
	void clearRun(StatBlock *hero);
	void applyTo(StatBlock *hero);

	std::vector<Def> defs;
	std::vector<int> rank;
	int pending;           // level-ups still waiting for a choice
	int last_level;        // hero level already accounted for
	bool in_run;
	int choice[CHOICES];   // def index per card, -1 = empty

	WidgetLabel *label_title;
	WidgetLabel *label_name[CHOICES];
	WidgetLabel *label_desc[CHOICES];
	WidgetLabel *label_rank[CHOICES];
	WidgetButton *button_pick[CHOICES];
	Sprite *card;
	Rect card_pos[CHOICES];
};

#endif
