/*
Random Dungeon: horde mode. Host/single-player only (a net client mirrors the
host's enemies, see NetManager.h). Active only on the map named in
engine/horde.txt. Every `interval` seconds it spawns a group of enemies in a
ring around a random living player, drawn from category tiers that unlock as
waves advance; enemy hp/damage grow per wave. Spawned enemies get a net_id, so
GameStatePlay::syncOwnEnemies() replicates them to clients with no extra code.
*/

#ifndef HORDE_MANAGER_H
#define HORDE_MANAGER_H

#include "CommonIncludes.h"

#include <set>

class Entity;
class MenuSanctuary;

class HordeManager {
public:
	HordeManager();

	// Spawns/cleans up for this tick. Returns corpses deleted this tick, so the
	// caller can drop dangling pointers to them (compare addresses only).
	std::vector<Entity*> logic();

	bool isActive() const {
		return active;
	}
	int getWave() const {
		return wave;
	}
	// true on the horde's map(s) (maps/run/...), whether or not this machine
	// runs the horde itself (net clients don't)
	bool isRunMap(const std::string& map);

	// Co-op (see GameStatePlay::coopLogic):
	// players on the arena, for scaling group size and enemy hp
	void setPlayers(int n) { players = n < 1 ? 1 : n; }
	// trailer mode (--trailer): multiplies group size, lifts the enemy cap
	float crowd_mult;
	// true while a fallen hero may still be revived by an ally: the run
	// isn't over until the whole party is down
	bool coop_hold;
	// souls go here at the end of each run (may be NULL)
	MenuSanctuary *sanctuary;
	// net client: no horde here, but keep the run's clock/wave (from the
	// host) and end the run the same way
	void clientLogic(int host_wave, int host_theme, int host_family);

	// Wave themes (engine/horde.txt theme= / family= / boss_every=): each
	// wave gets one, announced on screen (see bannerText)
	int getThemeIndex() const { return theme_index; }
	int getFamilyIndex() const { return family_index; }
	// "Wave 7 - Skeletons!"; changes whenever a new wave starts
	std::string bannerText() const;
	void noteKill() { kills++; }

private:
	struct Tier {
		std::string category;
		int min_wave;
		int weight;
	};
	struct Theme {
		std::string id;       // normal, swarm, elite, ambush, family, boss
		std::string label;    // shown after "Wave N - " (translated); empty = none
		int weight;
		int min_wave;
		float count_mult;
		float hp_mult;
		float interval_mult;
		float dist_mult;      // < 1 spawns closer (ambush)
		std::string category; // only enemies of this category (elite); "" = tiers
	};
	struct Family {
		std::string category; // e.g. rd_skeletons
		std::string name;     // e.g. Skeletons (translated)
	};
	void chooseTheme();
	void spawnBoss();
	bool isBossWave(int w) const;
	std::vector<Theme> themes;
	std::vector<Family> families;
	int boss_every;
	int theme_index;
	int family_index;
	int theme_wave;

	void loadConfig();
	void spawnGroup();
	void endRun();
	void resetForMap();
	void checkRunOver();
	bool spawnOne(const FPoint& near_pos, const std::string& only_category = "", float extra_hp = 1.0f, int extra_levels = 0);
	const Tier* pickTier() const;

	bool config_loaded;
	bool active;
	std::string map_filename;
	std::string last_map;

	float start_delay;
	float interval;
	float wave_length;
	int base_count;
	float count_per_wave;
	int max_alive;
	float spawn_dist_min;
	float spawn_dist_max;
	float corpse_seconds;
	float hp_growth;
	float dmg_growth;
	float xp_multiplier;     // run-only XP boost on every horde kill
	std::string end_map;     // where the hero respawns after dying (run over)
	std::vector<Tier> tiers;

	int kills;
	bool run_over;
	int players;

	int ticks;
	int next_spawn_tick;
	int wave;
	std::set<Entity*> spawned;
	std::map<Entity*, int> dead_ticks;
};

#endif
