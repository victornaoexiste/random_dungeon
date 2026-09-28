/*
Random Dungeon: the Sanctuary (Infinite Run meta progression).

Every run ends with souls (HordeManager::endRun -> awardSouls): waves reached
and kills. Souls buy permanent blessings from engine/sanctuary.txt (health,
damage, gold, a second chance...), kept per character in
saves/<prefix>/<slot>/sanctuary.txt. The blessings only act inside a run:
MenuRunUpgrade::applyTo adds them next to the run's own upgrades.

Opened from the Game Over screen's "Sanctuary" button. A grid of cards, one
per blessing, each with its rank, next cost and a Buy button.
*/

#ifndef MENU_SANCTUARY_H
#define MENU_SANCTUARY_H

#include "CommonIncludes.h"
#include "Menu.h"
#include "Utils.h"

class StatBlock;
class WidgetButton;
class WidgetLabel;

class MenuSanctuary : public Menu {
public:
	MenuSanctuary();
	~MenuSanctuary();

	void logic();
	void align();
	void render();
	void open();

	// souls for a finished run; returns how many were awarded
	int awardSouls(int wave_reached, int kills);
	int getSouls();

	// total bonus of one effect kind over all bought ranks (see MenuRunUpgrade::applyTo)
	float bonus(const std::string& kind);

private:
	class Effect {
	public:
		std::string kind;
		float value;
	};
	class Blessing {
	public:
		Blessing() : icon(-1), max_rank(5), cost_base(10), cost_step(5) {}
		std::string id;
		std::string name;
		std::string description;
		int icon;
		int max_rank;
		int cost_base;
		int cost_step;
		std::vector<Effect> effects;
	};

	void loadDefs();
	void loadSave();
	void save();
	int cost(size_t i) const;
	void refreshLabels();

	std::vector<Blessing> defs;
	std::vector<int> rank;
	int souls;
	int loaded_slot;

	WidgetLabel *label_title;
	WidgetLabel *label_souls;
	std::vector<WidgetLabel*> label_name;
	std::vector<WidgetLabel*> label_desc;
	std::vector<WidgetLabel*> label_rank;
	std::vector<WidgetButton*> button_buy;
	WidgetButton *button_close;
	Sprite *card;
	std::vector<Rect> card_pos;
};

#endif
