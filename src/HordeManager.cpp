#include "Avatar.h"
#include "EnemyGroupManager.h"
#include "EngineSettings.h"
#include "Entity.h"
#include "EntityManager.h"
#include "FileParser.h"
#include "HordeManager.h"
#include "MapCollision.h"
#include "MapRenderer.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "StatBlock.h"
#include "Utils.h"
#include "UtilsMath.h"
#include "UtilsParsing.h"

#include <cmath>

HordeManager::HordeManager()
	: config_loaded(false)
	, active(false)
	, start_delay(5)
	, interval(8)
	, wave_length(45)
	, base_count(4)
	, count_per_wave(2)
	, max_alive(60)
	, spawn_dist_min(16)
	, spawn_dist_max(24)
	, corpse_seconds(10)
	, hp_growth(0.25f)
	, dmg_growth(0.10f)
	, ticks(0)
	, next_spawn_tick(0)
	, wave(0)
{
}

void HordeManager::loadConfig() {
	config_loaded = true;

	FileParser infile;
	if (!infile.open("engine/horde.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE))
		return;

	while (infile.next()) {
		if (infile.key == "map") map_filename = infile.val;
		else if (infile.key == "start_delay") start_delay = Parse::toFloat(infile.val);
		else if (infile.key == "interval") interval = Parse::toFloat(infile.val);
		else if (infile.key == "wave_length") wave_length = Parse::toFloat(infile.val);
		else if (infile.key == "base_count") base_count = Parse::toInt(infile.val);
		else if (infile.key == "count_per_wave") count_per_wave = Parse::toFloat(infile.val);
		else if (infile.key == "max_alive") max_alive = Parse::toInt(infile.val);
		else if (infile.key == "spawn_dist_min") spawn_dist_min = Parse::toFloat(infile.val);
		else if (infile.key == "spawn_dist_max") spawn_dist_max = Parse::toFloat(infile.val);
		else if (infile.key == "corpse_seconds") corpse_seconds = Parse::toFloat(infile.val);
		else if (infile.key == "hp_growth") hp_growth = Parse::toFloat(infile.val);
		else if (infile.key == "dmg_growth") dmg_growth = Parse::toFloat(infile.val);
		else if (infile.key == "tier") {
			// tier=category,min_wave,weight
			Tier t;
			t.category = Parse::popFirstString(infile.val);
			t.min_wave = Parse::popFirstInt(infile.val);
			t.weight = std::max(1, Parse::popFirstInt(infile.val));
			tiers.push_back(t);
		}
	}
	infile.close();

	Utils::logInfo("HordeManager: map=%s interval=%.1fs wave_length=%.1fs tiers=%u", map_filename.c_str(), static_cast<double>(interval), static_cast<double>(wave_length), static_cast<unsigned>(tiers.size()));
}

const HordeManager::Tier* HordeManager::pickTier() const {
	int total = 0;
	for (size_t i = 0; i < tiers.size(); ++i) {
		if (tiers[i].min_wave <= wave)
			total += tiers[i].weight;
	}
	if (total <= 0)
		return NULL;

	int roll = Math::randBetween(0, total - 1);
	for (size_t i = 0; i < tiers.size(); ++i) {
		if (tiers[i].min_wave > wave)
			continue;
		if (roll < tiers[i].weight)
			return &tiers[i];
		roll -= tiers[i].weight;
	}
	return NULL;
}

bool HordeManager::spawnOne(const FPoint& near_pos) {
	const Tier* tier = pickTier();
	if (!tier)
		return false;

	std::vector<Enemy_Level> pool = enemyg->getEnemiesInCategory(tier->category);
	if (pool.empty())
		return false;

	FPoint pos;
	bool found = false;
	for (int attempt = 0; attempt < 12 && !found; ++attempt) {
		float angle = Math::randBetweenF(0, 2.0f * static_cast<float>(M_PI));
		float dist = Math::randBetweenF(spawn_dist_min, spawn_dist_max);
		pos.x = near_pos.x + std::cos(angle) * dist;
		pos.y = near_pos.y + std::sin(angle) * dist;
		found = mapr->collider.isValidPosition(pos.x, pos.y, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_ALL_ENTITIES);
	}
	if (!found)
		return false;

	const Enemy_Level& choice = pool[static_cast<size_t>(Math::randBetween(0, static_cast<int>(pool.size()) - 1))];
	Entity *e = entitym->getEntityPrototype(choice.type);

	StatBlock& s = e->stats;
	s.pos = pos;
	s.direction = static_cast<unsigned char>(Math::randBetween(0, 7));
	s.level = 1 + wave;

	// Grow hp and damage per wave through the engine's own per-level stat
	// mechanism (base = starting + (level-1) * per_level, see StatBlock::recalc).
	s.per_level[Stats::HP_MAX] = s.starting[Stats::HP_MAX] * hp_growth;
	for (size_t i = 0; i < eset->damage_types.list.size(); ++i) {
		size_t imin = Stats::COUNT + eset->damage_types.indexToMin(i);
		size_t imax = Stats::COUNT + eset->damage_types.indexToMax(i);
		s.per_level[imin] = s.starting[imin] * dmg_growth;
		s.per_level[imax] = s.starting[imax] * dmg_growth;
	}

	// A horde never idles: always in combat, always hunting the nearest player.
	s.combat_style = StatBlock::COMBAT_AGGRESSIVE;
	s.threat_range = std::max(s.threat_range, spawn_dist_max * 2);
	s.threat_range_far = std::max(s.threat_range_far, spawn_dist_max * 4);

	s.recalc();
	s.net_id = entitym->next_net_id++;

	entitym->entities.push_back(e);
	mapr->collider.block(pos.x, pos.y, !MapCollision::IS_ALLY);
	spawned.insert(e);
	return true;
}

void HordeManager::spawnGroup() {
	int alive = 0;
	for (std::set<Entity*>::iterator it = spawned.begin(); it != spawned.end(); ++it) {
		if (dead_ticks.find(*it) == dead_ticks.end())
			alive++;
	}

	int want = base_count + static_cast<int>(count_per_wave * static_cast<float>(wave));
	want = std::min(want, max_alive - alive);

	std::vector<FPoint> targets;
	if (pc && pc->stats.alive)
		targets.push_back(pc->stats.pos);
	for (size_t i = 0; i < entitym->net_targets.size(); ++i) {
		if (entitym->net_targets[i].stats->alive)
			targets.push_back(entitym->net_targets[i].stats->pos);
	}
	if (targets.empty())
		return;

	// One group per spawn tick: all enemies share an anchor player so they
	// arrive as a pack instead of an even ring.
	FPoint anchor = targets[static_cast<size_t>(Math::randBetween(0, static_cast<int>(targets.size()) - 1))];
	for (int i = 0; i < want; ++i)
		spawnOne(anchor);
}

std::vector<Entity*> HordeManager::logic() {
	std::vector<Entity*> removed;

	if (!config_loaded)
		loadConfig();

	if (last_map != mapr->getFilename()) {
		last_map = mapr->getFilename();
		// the map change already deleted every old entity (EntityManager::handleNewMap)
		spawned.clear();
		dead_ticks.clear();
		ticks = 0;
		wave = 0;
		next_spawn_tick = static_cast<int>(start_delay * static_cast<float>(settings->max_frames_per_sec));
	}

	active = (!map_filename.empty() && mapr->getFilename() == map_filename);
	if (!active)
		return removed;

	const float fps = static_cast<float>(settings->max_frames_per_sec);
	ticks++;
	wave = static_cast<int>(static_cast<float>(ticks) / (wave_length * fps));

	// Corpse cleanup, so a long session doesn't keep every kill in memory forever.
	const int corpse_ticks = static_cast<int>(corpse_seconds * fps);
	for (std::set<Entity*>::iterator it = spawned.begin(); it != spawned.end();) {
		Entity *e = *it;
		StatBlock& s = e->stats;
		bool dead = (s.cur_state == StatBlock::ENTITY_DEAD || s.cur_state == StatBlock::ENTITY_CRITDEAD);
		if (dead && dead_ticks.find(e) == dead_ticks.end())
			dead_ticks[e] = 0;

		if (dead && ++dead_ticks[e] >= corpse_ticks) {
			if (s.corpse_has_collision)
				mapr->collider.unblock(s.pos.x, s.pos.y);
			for (size_t i = 0; i < entitym->entities.size(); ++i) {
				if (entitym->entities[i] == e) {
					entitym->entities.erase(entitym->entities.begin() + i);
					break;
				}
			}
			dead_ticks.erase(e);
			removed.push_back(e);
			e->unloadSounds();
			delete e;
			spawned.erase(it++);
		}
		else {
			++it;
		}
	}

	if (ticks >= next_spawn_tick) {
		spawnGroup();
		next_spawn_tick = ticks + std::max(1, static_cast<int>(interval * fps));
	}

	return removed;
}
