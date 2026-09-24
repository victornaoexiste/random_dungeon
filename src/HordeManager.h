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

private:
	struct Tier {
		std::string category;
		int min_wave;
		int weight;
	};

	void loadConfig();
	void spawnGroup();
	bool spawnOne(const FPoint& near_pos);
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
	std::vector<Tier> tiers;

	int ticks;
	int next_spawn_tick;
	int wave;
	std::set<Entity*> spawned;
	std::map<Entity*, int> dead_ticks;
};

#endif
