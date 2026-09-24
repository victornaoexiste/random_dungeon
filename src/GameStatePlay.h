/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2012 Igor Paliychuk
Copyright © 2012-2015 Justin Jacobs

This file is part of FLARE.

FLARE is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

FLARE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
FLARE.  If not, see http://www.gnu.org/licenses/
*/

/**
 * class GameStatePlay
 *
 * Handles logic and rendering of the main action game play
 * Also handles message passing between child objects, often to avoid circular dependencies.
 */

#ifndef GAMESTATEPLAY_H
#define GAMESTATEPLAY_H

#include "CommonIncludes.h"
#include "GameState.h"
#include "Utils.h"

#include <map>
#include <set>

class Avatar;
class Entity;
class GameSlotPreview;
class HordeManager;
class MenuManager;
class QuestLog;
class StatBlock;
class WidgetLabel;

class ActionData;

class Title {
public:
	std::string title;
	int level;
	PowerID power;
	std::vector<StatusID> requires_status;
	std::vector<StatusID> requires_not_status;
	std::string primary_stat_1;
	std::string primary_stat_2;

	Title()
		: title("")
		, level(0)
		, power(0)
		, requires_status()
		, requires_not_status()
		, primary_stat_1("")
		, primary_stat_2("") {
	}
};

class GameStatePlay : public GameState {
private:
	Entity *enemy;

	QuestLog *quests;

	void checkEnemyFocus();
	void checkNPCFocus();
	void checkLoot();
	void checkLootDrop();
	void checkTeleport();
	void checkCancel();
	void checkLog();
	void checkBook();
	void checkEquipmentChange();
	void checkTitle();
	void checkUsedItems();
	void checkNotifications();
	void checkNPCInteraction();
	void checkStash();
	void checkCutscene();
	void checkSaveEvent();
	void updateActionBar(unsigned index);
	void loadTitles();
	void resetNPC();
	bool checkPrimaryStat(const std::string& first, const std::string& second);

	int npc_id;

	std::vector<Title> titles;

	Timer second_timer;

	bool is_first_map_load;

	// Other players, keyed by NetManager player_id: each gets its own
	// StatBlock (just enough for gfx_base/gfx_head/direction) + GameSlotPreview
	// (the same layered-avatar renderer used for the character-select
	// screen), so remote players render as their real class/equipment
	// instead of a placeholder monster. Spawned once that id's appearance
	// arrives (see netmgr->getRemoteAppearances()), dropped once the id
	// stops appearing in netmgr->getRemotePositions().
	struct RemotePlayerVisual {
		StatBlock *stats;
		GameSlotPreview *preview;
		bool in_action_anim; // true while playing a triggered attack/skill anim (see NetManager::sendAction) -- position sync won't override stance/run until it's isCompleted()
	};
	std::map<uint32_t, RemotePlayerVisual> remote_players;
	std::map<uint32_t, FPoint> remote_last_pos;
	bool sent_own_appearance;
	int last_pc_state; // StatBlock::ENTITY_* last tick, to detect a fresh transition into ENTITY_POWER
	void syncRemoteEntities();
	void sendOwnAppearance();
	void syncOwnAction();
	void syncRemotePowerVisuals();

	// Step 5 (shared horde, see NetManager.h): host-authoritative enemy
	// state sync. Host side uses announced_enemy_ids (which net_ids it's
	// already sent an EnemySpawnPacket for); client side owns real Entity
	// objects (also living in entitym->entities, so the existing
	// HazardManager collision code hits them for free) in
	// remote_enemies_local, keyed by net_id, plus remote_enemy_hp_before_combat
	// to diff hp across a single hazards->logic() call and report what our
	// own hits dealt.
	std::set<uint32_t> announced_enemy_ids;
	std::map<uint32_t, Entity*> remote_enemies_local;
	std::map<uint32_t, float> remote_enemy_hp_before_combat;
	void syncOwnEnemies();
	void applyEnemyHitReports();

	// Enemy -> player damage. Host: remote players become enemy AI targets and
	// enemy hazards that hit them are forwarded (see HazardManager::net_player_hits).
	// Client: applies the forwarded hit to our own hero via the real Entity::takeHit().
	StatBlock *net_hit_src;
	HordeManager *horde;
	void updateNetTargets();
	void forwardNetPlayerHits();
	void applyPlayerHits();
	void syncRemoteEnemies();
	void snapshotEnemyHpBeforeCombat();
	void reportEnemyHitsAfterCombat();

	static const unsigned UPDATE_ACTIONBAR_ALL = 0;

public:
	GameStatePlay();
	~GameStatePlay();
	void refreshWidgets();

	bool isPaused();
	void logic();
	void render();
	void resetGame();
};

#endif

