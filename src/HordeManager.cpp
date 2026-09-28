#include "Avatar.h"
#include "MenuGameOver.h"
#include "MenuManager.h"
#include "MenuSanctuary.h"
#include "MessageEngine.h"
#include "SaveLoad.h"
#include "UtilsFileSystem.h"
#include "EnemyGroupManager.h"
#include "EngineSettings.h"
#include "Entity.h"
#include "EntityManager.h"
#include "FileParser.h"
#include "HordeManager.h"
#include "GameState.h"
#include "MapCollision.h"
#include "MapRenderer.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "StatBlock.h"
#include "Utils.h"
#include "UtilsMath.h"
#include "UtilsParsing.h"

#include <cmath>
#include <set>
#include <cstdlib>
#include <fstream>
#include <sstream>

HordeManager::HordeManager()
	: coop_hold(false)
	, sanctuary(NULL)
	, config_loaded(false)
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
	, xp_multiplier(1)
	, kills(0)
	, run_over(false)
	, players(1)
	, ticks(0)
	, next_spawn_tick(0)
	, wave(0)
{
	boss_every = 5;
	theme_index = 0;
	family_index = 0;
	theme_wave = -1;
}

bool HordeManager::isRunMap(const std::string& map) {
	if (!config_loaded)
		loadConfig();
	if (map_filename.empty())
		return false;
	bool folder = map_filename[map_filename.size() - 1] == '/';
	return map == map_filename || (folder && map.compare(0, map_filename.size(), map_filename) == 0);
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
		else if (infile.key == "xp_multiplier") xp_multiplier = Parse::toFloat(infile.val);
		else if (infile.key == "end_map") end_map = infile.val;
		else if (infile.key == "boss_every") boss_every = std::max(0, Parse::toInt(infile.val));
		else if (infile.key == "theme") {
			// theme=id,weight,min_wave,count_mult,hp_mult,interval_mult,dist_mult,category,label
			Theme t;
			t.id = Parse::popFirstString(infile.val);
			t.weight = std::max(0, Parse::popFirstInt(infile.val));
			t.min_wave = Parse::popFirstInt(infile.val);
			t.count_mult = Parse::toFloat(Parse::popFirstString(infile.val), 1);
			t.hp_mult = Parse::toFloat(Parse::popFirstString(infile.val), 1);
			t.interval_mult = Parse::toFloat(Parse::popFirstString(infile.val), 1);
			t.dist_mult = Parse::toFloat(Parse::popFirstString(infile.val), 1);
			t.category = Parse::popFirstString(infile.val);
			t.label = Parse::popFirstString(infile.val);
			themes.push_back(t);
		}
		else if (infile.key == "family") {
			// family=category,Name
			Family f;
			f.category = Parse::popFirstString(infile.val);
			f.name = Parse::popFirstString(infile.val);
			families.push_back(f);
		}
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

bool HordeManager::spawnOne(const FPoint& near_pos, const std::string& only_category, float extra_hp, int extra_levels) {
	std::vector<Enemy_Level> pool;
	if (!only_category.empty()) {
		// a theme's category (a family, the elites...), limited to enemies whose
		// tier is already unlocked
		std::set<std::string> unlocked;
		for (size_t i = 0; i < tiers.size(); ++i) {
			if (tiers[i].min_wave > wave)
				continue;
			std::vector<Enemy_Level> t = enemyg->getEnemiesInCategory(tiers[i].category);
			for (size_t j = 0; j < t.size(); ++j)
				unlocked.insert(t[j].type);
		}
		std::vector<Enemy_Level> all = enemyg->getEnemiesInCategory(only_category);
		for (size_t j = 0; j < all.size(); ++j) {
			if (unlocked.count(all[j].type))
				pool.push_back(all[j]);
		}
	}
	if (pool.empty()) {
		const Tier* tier = pickTier();
		if (!tier)
			return false;
		pool = enemyg->getEnemiesInCategory(tier->category);
	}
	if (pool.empty())
		return false;

	const Theme* theme = (theme_index >= 0 && static_cast<size_t>(theme_index) < themes.size()) ? &themes[theme_index] : NULL;
	const float dist_mult = theme ? theme->dist_mult : 1.0f;

	FPoint pos;
	bool found = false;
	for (int attempt = 0; attempt < 12 && !found; ++attempt) {
		float angle = Math::randBetweenF(0, 2.0f * static_cast<float>(M_PI));
		float dist = Math::randBetweenF(spawn_dist_min, spawn_dist_max) * dist_mult;
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
	s.level = 1 + wave + extra_levels;

	// co-op: tougher enemies for a bigger party (+35% hp per extra player);
	// themes and bosses scale hp too
	float hp_scale = extra_hp * (theme ? theme->hp_mult : 1.0f);
	if (players > 1)
		hp_scale *= 1.0f + 0.35f * static_cast<float>(players - 1);
	s.starting[Stats::HP_MAX] *= hp_scale;

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
	s.xp = static_cast<unsigned long>(static_cast<float>(s.xp) * xp_multiplier + 0.5f);
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

	const Theme* theme = (theme_index >= 0 && static_cast<size_t>(theme_index) < themes.size()) ? &themes[theme_index] : NULL;
	int want = base_count + static_cast<int>(count_per_wave * static_cast<float>(wave));
	// co-op: bigger packs for a bigger party (+60% per extra player)
	want = static_cast<int>(static_cast<float>(want) * (1.0f + 0.6f * static_cast<float>(players - 1)) * (theme ? theme->count_mult : 1.0f) + 0.5f);
	want = std::max(1, want);
	std::string category = theme ? theme->category : "";
	if (theme && theme->id == "family" && static_cast<size_t>(family_index) < families.size())
		category = families[family_index].category;
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
	for (int i = 0; i < want && i < max_alive - alive; ++i)
		spawnOne(anchor, category);
}

std::vector<Entity*> HordeManager::logic() {
	std::vector<Entity*> removed;

	if (!config_loaded)
		loadConfig();

	if (last_map != mapr->getFilename())
		resetForMap();

	// map= may name one map, or a folder ending in '/' (e.g. maps/run/) so the
	// horde runs on any arena in it -- the run start map picks one at random.
	const std::string& cur = mapr->getFilename();
	bool folder = !map_filename.empty() && map_filename[map_filename.size() - 1] == '/';
	active = !map_filename.empty() && (cur == map_filename || (folder && cur.compare(0, map_filename.size(), map_filename) == 0));
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
		if (dead && dead_ticks.find(e) == dead_ticks.end()) {
			dead_ticks[e] = 0;
			kills++;
		}

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

	checkRunOver();
	if (run_over) {
		// automated test hook (see MenuDevKit.h): capture the Game Over summary once
		static bool shot = false;
		const char* st = getenv("RD_DEVKIT_SELFTEST");
		if (st && !shot && menu->game_over->visible) {
			render_device->screenshot_request = std::string(st) + "/run_over.png";
			shot = true;
		}
		else if (st && shot && menu->game_over->visible) {
			menu->game_over->continue_clicked = true; // then check that we respawn at end_map
		}
		return removed;
	}

	if (wave != theme_wave) {
		theme_wave = wave;
		chooseTheme();
		if (isBossWave(wave))
			spawnBoss();
	}

	if (ticks >= next_spawn_tick) {
		spawnGroup();
		const float im = (static_cast<size_t>(theme_index) < themes.size()) ? themes[theme_index].interval_mult : 1.0f;
		next_spawn_tick = ticks + std::max(1, static_cast<int>(interval * im * fps));
	}

	return removed;
}

bool HordeManager::isBossWave(int w) const {
	return boss_every > 0 && (w + 1) % boss_every == 0;
}

/**
 * Picks this wave's theme (weighted, by min_wave). Boss waves use the
 * "boss" theme; wave 1 is always the first theme (normal).
 */
void HordeManager::chooseTheme() {
	theme_index = 0;
	if (themes.empty())
		return;
	if (isBossWave(wave)) {
		for (size_t i = 0; i < themes.size(); ++i)
			if (themes[i].id == "boss") theme_index = static_cast<int>(i);
		return;
	}
	if (wave == 0)
		return;
	int total = 0;
	for (size_t i = 0; i < themes.size(); ++i)
		if (themes[i].id != "boss" && themes[i].min_wave <= wave) total += themes[i].weight;
	if (total > 0) {
		int roll = Math::randBetween(0, total - 1);
		for (size_t i = 0; i < themes.size(); ++i) {
			if (themes[i].id == "boss" || themes[i].min_wave > wave)
				continue;
			if (roll < themes[i].weight) {
				theme_index = static_cast<int>(i);
				break;
			}
			roll -= themes[i].weight;
		}
	}
	if (themes[theme_index].id == "family" && !families.empty())
		family_index = Math::randBetween(0, static_cast<int>(families.size()) - 1);
	Utils::logInfo("HordeManager: wave %d theme '%s' %s", wave + 1, themes[theme_index].id.c_str(),
		themes[theme_index].id == "family" && !families.empty() ? families[family_index].category.c_str() : "");
}

/**
 * Boss waves: one big enemy (a real boss once they're unlocked, an elite
 * before that) with extra hp and levels, next to a random player.
 */
void HordeManager::spawnBoss() {
	std::vector<FPoint> targets;
	if (pc && pc->stats.alive)
		targets.push_back(pc->stats.pos);
	for (size_t i = 0; i < entitym->net_targets.size(); ++i)
		if (entitym->net_targets[i].stats->alive)
			targets.push_back(entitym->net_targets[i].stats->pos);
	if (targets.empty())
		return;
	FPoint anchor = targets[static_cast<size_t>(Math::randBetween(0, static_cast<int>(targets.size()) - 1))];

	std::string cat = "rd_elite";
	for (size_t i = 0; i < tiers.size(); ++i)
		if (tiers[i].category == "rd_boss" && tiers[i].min_wave <= wave) cat = "rd_boss";

	// spawnOne() draws from the tiers: offer only the boss category for this one
	std::vector<Tier> saved = tiers;
	Tier only;
	only.category = cat;
	only.min_wave = 0;
	only.weight = 1;
	tiers.clear();
	tiers.push_back(only);
	size_t before = entitym->entities.size();
	spawnOne(anchor, "", cat == "rd_boss" ? 2.0f : 4.0f, 2);
	tiers = saved;
	if (entitym->entities.size() > before)
		Utils::logInfo("HordeManager: boss wave %d: %s", wave + 1, entitym->entities.back()->stats.name.c_str());
}

std::string HordeManager::bannerText() const {
	std::string s = msg->getv("Wave %d", wave + 1);
	if (static_cast<size_t>(theme_index) < themes.size()) {
		const Theme& t = themes[theme_index];
		if (t.id == "family" && static_cast<size_t>(family_index) < families.size())
			s += " - " + msg->get(families[family_index].name) + "!";
		else if (!t.label.empty())
			s += " - " + msg->get(t.label);
	}
	return s;
}

void HordeManager::resetForMap() {
	last_map = mapr->getFilename();
	// the map change already deleted every old entity (EntityManager::handleNewMap)
	spawned.clear();
	dead_ticks.clear();
	ticks = 0;
	wave = 0;
	kills = 0;
	run_over = false;
	next_spawn_tick = static_cast<int>(start_delay * static_cast<float>(settings->max_frames_per_sec));
	theme_index = 0;
	family_index = 0;
	theme_wave = -1;
}

/**
 * The hero died: the run is over (summary on the Game Over screen, respawn
 * at end_map) -- unless an ally can still revive them (coop_hold).
 */
void HordeManager::checkRunOver() {
	if (!run_over && !coop_hold && pc && (pc->stats.cur_state == StatBlock::ENTITY_DEAD || pc->stats.cur_state == StatBlock::ENTITY_CRITDEAD)) {
		run_over = true;
		endRun();
	}
}

void HordeManager::clientLogic(int host_wave, int host_theme, int host_family) {
	if (!config_loaded)
		loadConfig();
	if (last_map != mapr->getFilename())
		resetForMap();
	active = isRunMap(mapr->getFilename());
	if (!active)
		return;
	ticks++;
	wave = std::max(wave, host_wave);
	theme_index = host_theme;
	family_index = host_family;
	checkRunOver();
}

/**
 * Run over: show wave/kills/time/level on the Game Over screen, keep a per-save
 * best (saves/<prefix>/<slot>/run_best.txt) and send the respawn to end_map, so
 * "Continue" leaves the arena instead of dropping back into the same run.
 */
void HordeManager::endRun() {
	const int fps = std::max(1, static_cast<int>(settings->max_frames_per_sec));
	const int seconds = ticks / fps;
	const int reached = wave + 1;

	std::stringstream summary;
	summary << msg->get("Wave") << " " << reached << "  |  " << kills << " " << msg->get("kills") << "  |  "
		<< seconds / 60 << "m" << (seconds % 60 < 10 ? "0" : "") << seconds % 60 << "s  |  " << msg->get("Level") << " " << pc->stats.level;

	// best run of this character
	int best_wave = 0, best_kills = 0;
	std::string best_path;
	if (save_load && save_load->getGameSlot() > 0) {
		std::stringstream p;
		p << settings->path_user << "saves/" << eset->misc.save_prefix << "/" << save_load->getGameSlot() << "/run_best.txt";
		best_path = p.str();
		std::ifstream in(best_path.c_str());
		std::string line;
		while (std::getline(in, line)) {
			if (line.compare(0, 5, "wave=") == 0) best_wave = Parse::toInt(line.substr(5));
			else if (line.compare(0, 6, "kills=") == 0) best_kills = Parse::toInt(line.substr(6));
		}
	}
	bool record = reached > best_wave || (reached == best_wave && kills > best_kills);
	if (record && !best_path.empty()) {
		std::ofstream out(best_path.c_str());
		out << "wave=" << reached << "\nkills=" << kills << "\nseconds=" << seconds << "\nlevel=" << pc->stats.level << "\n";
		best_wave = reached;
		best_kills = kills;
	}
	std::stringstream best;
	if (record)
		best << msg->get("New record!");
	else
		best << msg->get("Best") << ": " << msg->get("Wave") << " " << best_wave << ", " << best_kills << " " << msg->get("kills");

	// Sanctuary: souls for this run, spent on permanent blessings
	if (sanctuary) {
		int gained = sanctuary->awardSouls(reached, kills);
		best << "  |  " << msg->getv("+%d souls (%d)", gained, sanctuary->getSouls());
		menu->game_over->show_sanctuary = true;
	}

	menu->game_over->setInfo(summary.str(), best.str());
	pc->logMsg(msg->get("Run over") + ": " + summary.str(), Avatar::MSG_NORMAL);
	Utils::logInfo("HordeManager: run over -- wave %d, %d kills, %ds, level %d%s", reached, kills, seconds, pc->stats.level, record ? " (record)" : "");

	// run edition (see GameState::runEdition): there is no open world to go
	// back to, "Continue" starts a fresh run
	const std::string target = GameState::runEdition() ? std::string("maps/run/start.txt") : end_map;
	if (!target.empty()) {
		mapr->respawn_map = target;
		mapr->respawn_point.x = -1;
		mapr->respawn_point.y = -1;
	}
}
