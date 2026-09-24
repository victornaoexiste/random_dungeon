/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2012 Igor Paliychuk
Copyright © 2012-2014 Henrik Andersson
Copyright © 2012 Stefan Beller
Copyright © 2013 Kurt Rinnert
Copyright © 2012-2016 Justin Jacobs

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

#include "Animation.h"
#include "Avatar.h"
#include "CampaignManager.h"
#include "CombatText.h"
#include "CursorManager.h"
#include "EnemyGroupManager.h"
#include "Entity.h"
#include "EntityManager.h"
#include "EngineSettings.h"
#include "FileParser.h"
#include "FogOfWar.h"
#include "GameSlotPreview.h"
#include "GameState.h"
#include "GameStateCutscene.h"
#include "GameStatePlay.h"
#include "GameStateTitle.h"
#include "Hazard.h"
#include "HazardManager.h"
#include "HordeManager.h"
#include "InputState.h"
#include "LootManager.h"
#include "MapRenderer.h"
#include "Menu.h"
#include "MenuActionBar.h"
#include "MenuBook.h"
#include "MenuCharacter.h"
#include "MenuDevConsole.h"
#include "MenuEnemy.h"
#include "MenuExit.h"
#include "MenuHUDLog.h"
#include "MenuInventory.h"
#include "MenuLog.h"
#include "MenuManager.h"
#include "NetManager.h"
#include "MenuMiniMap.h"
#include "MenuPowers.h"
#include "MenuRegionTitle.h"
#include "MenuStash.h"
#include "MenuTalker.h"
#include "MenuVendor.h"
#include "ModManager.h"
#include "NPC.h"
#include "NPCManager.h"
#include "PowerManager.h"
#include "QuestLog.h"
#include "RenderDevice.h"
#include "SaveLoad.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "SoundManager.h"
#include "UtilsParsing.h"
#include "WidgetLabel.h"
#include "XPScaling.h"

#include <cassert>

GameStatePlay::GameStatePlay()
	: GameState()
	, enemy(NULL)
	, npc_id(-1)
	, is_first_map_load(true)
	, sent_own_appearance(false)
	, last_pc_state(-1)
	, net_hit_src(NULL)
	, horde(new HordeManager())
{
	second_timer.setDuration(settings->max_frames_per_sec);

	hasMusic = true;
	has_background = false;
	// GameEngine scope variables

	if (items == NULL)
		items = new ItemManager();

	camp = new CampaignManager();
	eventm = new EventManager();

	loot = new LootManager();
	powers = new PowerManager();
	fow = new FogOfWar();
	mapr = new MapRenderer();
	pc = new Avatar();
	entitym = new EntityManager();
	enemyg = new EnemyGroupManager();
	hazards = new HazardManager();
	menu = new MenuManager();
	npcs = new NPCManager();
	quests = new QuestLog(menu->questlog);
	xp_scaling = new XPScaling();

	// load the config file for character titles
	loadTitles();

	refreshWidgets();
}

void GameStatePlay::refreshWidgets() {
	menu->alignAll();
}

/**
 * Reset all game states to a new game.
 */
void GameStatePlay::resetGame() {
	camp->resetAllStatuses();
	pc->init();
	pc->stats.currency = 0;
	menu->act->clear(!MenuActionBar::CLEAR_SKIP_ITEMS);
	menu->inv->inventory[0].clear();
	menu->inv->inventory[1].clear();
	menu->inv->changed_equipment = true;
	menu->inv->currency = 0;
	menu->questlog->clearAll();
	quests->createQuestList();
	menu->hudlog->clear();

	// Finalize new character settings
	menu->talker->setHero(pc->stats);
	pc->loadSounds();

	mapr->teleportation = true;
	mapr->teleport_mapname = "maps/spawn.txt";
}

/**
 * Check mouseover for enemies.
 * class variable "enemy" contains a live enemy on mouseover.
 * This function also sets enemy mouseover for Menu Enemy.
 */
void GameStatePlay::checkEnemyFocus() {
	pc->stats.target_corpse = NULL;
	pc->stats.target_nearest = NULL;
	pc->stats.target_nearest_corpse = NULL;
	pc->stats.target_nearest_dist = 0;
	pc->stats.target_nearest_corpse_dist = 0;

	FPoint src_pos = pc->stats.pos;

	// check the last hit enemy first
	// if there's none, then either get the nearest enemy or one under the mouse (depending on mouse mode)
	if (!inpt->usingMouse()) {
		if (hazards->last_enemy) {
			if (enemy == hazards->last_enemy) {
				if (!menu->enemy->timeout.isEnd() && hazards->last_enemy->stats.hp > 0)
					return;
				else
					hazards->last_enemy = NULL;
			}
			enemy = hazards->last_enemy;
		}
		else {
			enemy = entitym->getNearestEntity(pc->stats.pos, !EntityManager::GET_CORPSE, NULL, eset->misc.interact_range);
		}
	}
	else {
		if (hazards->last_enemy) {
			enemy = hazards->last_enemy;
			hazards->last_enemy = NULL;
		}
		else {
			enemy = entitym->entityFocus(inpt->mouse, mapr->cam.pos, EntityManager::IS_ALIVE);
			if (enemy) {
				curs->setCursor(CursorManager::CURSOR_ATTACK);
			}
			src_pos = Utils::screenToMap(inpt->mouse.x, inpt->mouse.y, mapr->cam.pos.x, mapr->cam.pos.y);

		}
	}

	if (enemy) {
		// set the actual menu with the enemy selected above
		if (!enemy->stats.suppress_hp) {
			menu->enemy->enemy = enemy;
			menu->enemy->timeout.reset(Timer::BEGIN);
		}
	}
	else if (inpt->usingMouse()) {
		// if we're using a mouse and we didn't select an enemy, try selecting a dead one instead
		Entity *temp_enemy = entitym->entityFocus(inpt->mouse, mapr->cam.pos, !EntityManager::IS_ALIVE);
		if (temp_enemy && !temp_enemy->stats.suppress_hp) {
			pc->stats.target_corpse = &(temp_enemy->stats);
			menu->enemy->enemy = temp_enemy;
			menu->enemy->timeout.reset(Timer::BEGIN);
		}
	}

	// save the highlighted enemy position for auto-targeting purposes
	if (enemy) {
		pc->cursor_enemy = enemy;
	}
	else {
		pc->cursor_enemy = NULL;
	}

	// save the positions of the nearest enemies for powers that use "target_nearest"
	Entity *nearest = entitym->getNearestEntity(src_pos, !EntityManager::GET_CORPSE, &(pc->stats.target_nearest_dist), eset->misc.interact_range);
	if (nearest)
		pc->stats.target_nearest = &(nearest->stats);
	Entity *nearest_corpse = entitym->getNearestEntity(src_pos, EntityManager::GET_CORPSE, &(pc->stats.target_nearest_corpse_dist), eset->misc.interact_range);
	if (nearest_corpse)
		pc->stats.target_nearest_corpse = &(nearest_corpse->stats);
}

/**
 * Similar to the above checkEnemyFocus(), but handles NPCManager instead
 */
void GameStatePlay::checkNPCFocus() {
	Entity *focus_npc;

	if (!inpt->usingMouse() && (!menu->enemy->enemy || menu->enemy->enemy->stats.hero_ally)) {
		// TODO bug? If mixed monster allies and npc allies, npc allies will always be highlighted, regardless of distance to player
		focus_npc = npcs->getNearestNPC(pc->stats.pos);
	}
	else {
		focus_npc = npcs->npcFocus(inpt->mouse, mapr->cam.pos, true);
	}

	if (focus_npc) {
		// set the actual menu with the npc selected above
		if (!focus_npc->stats.suppress_hp) {
			menu->enemy->enemy = focus_npc;
			menu->enemy->timeout.reset(Timer::BEGIN);
		}
	}
	else if (inpt->usingMouse()) {
		// if we're using a mouse and we didn't select an npc, try selecting a dead one instead
		Entity *temp_npc = npcs->npcFocus(inpt->mouse, mapr->cam.pos, false);
		if (temp_npc) {
			menu->enemy->enemy = temp_npc;
			menu->enemy->timeout.reset(Timer::BEGIN);
		}
	}
}

/**
 * Check to see if the player is picking up loot on the ground
 */
void GameStatePlay::checkLoot() {

	if (!pc->stats.alive)
		return;

	if (menu->isDragging())
		return;

	ItemStack pickup;

	// Autopickup
	pickup = loot->checkAutoPickup(pc->stats.pos);

	// Normal pickups
	if (pickup.empty() && !pc->using_main1) {
		pickup = loot->checkPickup(inpt->mouse, mapr->cam.pos, pc->stats.pos);
	}

	if (!pickup.empty()) {
		menu->inv->add(pickup, MenuInventory::CARRIED, ItemStorage::NO_SLOT, MenuInventory::ADD_PLAY_SOUND, MenuInventory::ADD_AUTO_EQUIP);
		if (items->isValid(pickup.item)) {
			StatusID pickup_status = camp->registerStatus(items->items[pickup.item]->pickup_status);
			camp->setStatus(pickup_status);
		}
		pickup.clear();
	}

}

void GameStatePlay::checkTeleport() {
	bool on_load_teleport = false;

	// both map events and player powers can cause teleportation
	if (mapr->teleportation || pc->stats.teleportation) {

		if (mapr->fogofwar)
			if(fow->fog_layer_id != 0)
				fow->handleIntramapTeleport();

		mapr->collider.unblock(pc->stats.pos.x, pc->stats.pos.y);

		if (mapr->teleportation) {
			// camera gets interpolated movement during intramap teleport
			// during intermap teleport, we set the camera to the player position
			pc->stats.pos.x = mapr->teleport_destination.x;
			pc->stats.pos.y = mapr->teleport_destination.y;
			pc->teleport_camera_lock = true;
		}
		else {
			pc->stats.pos.x = pc->stats.teleport_destination.x;
			pc->stats.pos.y = pc->stats.teleport_destination.y;
		}

		// if we're not changing map, move allies to a the player's new position
		// when changing maps, entitym->handleNewMap() does something similar to this
		if (mapr->teleport_mapname.empty()) {
			FPoint spawn_pos = mapr->collider.getRandomNeighbor(Point(pc->stats.pos), 1, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_ALL_ENTITIES);
			for (unsigned int i=0; i < entitym->entities.size(); i++) {
				if(entitym->entities[i]->stats.hero_ally && entitym->entities[i]->stats.alive && entitym->entities[i]->stats.speed > 0) {
					mapr->collider.unblock(entitym->entities[i]->stats.pos.x, entitym->entities[i]->stats.pos.y);
					entitym->entities[i]->stats.pos = spawn_pos;
					mapr->collider.block(entitym->entities[i]->stats.pos.x, entitym->entities[i]->stats.pos.y, MapCollision::IS_ALLY);
				}
			}
		}

		// process intermap teleport
		if (mapr->teleportation && !mapr->teleport_mapname.empty()) {
			mapr->cam.warpTo(pc->stats.pos);
			std::string teleport_mapname = mapr->teleport_mapname;
			mapr->teleport_mapname = "";
			inpt->lock_all = (teleport_mapname == "maps/spawn.txt");
			mapr->executeOnMapExitEvents();
			showLoading();
			render_device->cleanupQueuedImages();
			save_load->saveFOW(); // TODO handle save_onload/save_onexit?
			mapr->load(teleport_mapname);
			setLoadingFrame();

			// use the default hero spawn position for this map
			if (mapr->force_spawn_pos || (mapr->teleport_destination.x == -1 && mapr->teleport_destination.y == -1)) {
				pc->stats.pos.x = mapr->hero_pos.x;
				pc->stats.pos.y = mapr->hero_pos.y;

				if (mapr->teleport_destination_id > 0) {
					for (size_t i = 0; i < mapr->events.size(); ++i) {
						EventComponent* ec_hero_pos = mapr->events[i].getComponent(EventComponent::INTERMAP_ID);
						if (ec_hero_pos && ec_hero_pos->data[0].Int == mapr->teleport_destination_id) {
							pc->stats.pos.x = static_cast<float>(mapr->events[i].location.x) + 0.5f;
							pc->stats.pos.y = static_cast<float>(mapr->events[i].location.y) + 0.5f;
							break;
						}
					}
				}
				mapr->cam.warpTo(pc->stats.pos);
			}

			// store this as the new respawn point (provided the tile is open)
			if (mapr->collider.isValidPosition(pc->stats.pos.x, pc->stats.pos.y, MapCollision::MOVE_NORMAL, MapCollision::COLLIDE_TYPE_HERO)) {
				mapr->respawn_map = teleport_mapname;
				mapr->respawn_point = pc->stats.pos;
			}
			else {
				Utils::logError("GameStatePlay: Spawn position (%d, %d) is blocked.", static_cast<int>(pc->stats.pos.x), static_cast<int>(pc->stats.pos.y));
			}

			pc->handleNewMap();
			hazards->handleNewMap();
			loot->handleNewMap();
			powers->handleNewMap(&mapr->collider);
			menu->enemy->handleNewMap();
			menu->stash->visible = false;

			// switch off teleport flag so we can check if an on_load event has teleportation
			mapr->teleportation = false;

			mapr->executeOnLoadEvents();
			if (mapr->teleportation)
				on_load_teleport = true;

			// enemies and npcs should be initialized AFTER on_load events execute
			entitym->handleNewMap();
			npcs->handleNewMap();
			resetNPC();

			menu->mini->prerender(&mapr->collider, mapr->w, mapr->h);

			// return to title (permadeath) OR auto-save
			if (pc->stats.permadeath && pc->stats.cur_state == StatBlock::ENTITY_DEAD) {
				snd->stopMusic();
				showLoading();
				setRequestedGameState(new GameStateTitle());
			}
			else if (eset->misc.save_onload) {
				if (!is_first_map_load)
					save_load->saveGame();
				else
					is_first_map_load = false;
			}
		}

		if (mapr->collider.isOutsideMap(pc->stats.pos.x, pc->stats.pos.y)) {
			Utils::logError("GameStatePlay: Teleport position is outside of map bounds.");
			pc->stats.pos.x = 0.5f;
			pc->stats.pos.y = 0.5f;
		}

		mapr->collider.block(pc->stats.pos.x, pc->stats.pos.y, !MapCollision::IS_ALLY);

		pc->stats.teleportation = false;

		if (settings->mouse_move) {
			pc->mm_target_object = Avatar::MM_TARGET_NONE;
			pc->setDesiredMMTarget(pc->stats.pos);
		}
	}

	if (!on_load_teleport && mapr->teleport_mapname.empty())
		mapr->teleportation = false;
}

/**
 * Check for cancel key to exit menus or exit the game.
 * Also check closing the game window entirely.
 */
void GameStatePlay::checkCancel() {
	bool save_on_exit = eset->misc.save_onexit && !(pc->stats.permadeath && pc->stats.cur_state == StatBlock::ENTITY_DEAD);

	if (save_on_exit && eset->misc.save_pos_onexit) {
		mapr->respawn_point = pc->stats.pos;
	}

	// if user has clicked exit game from exit menu
	if (menu->requestingExit()) {
		menu->closeAll();

		if (save_on_exit)
			save_load->saveGame();

		// audio levels can be changed in the pause menu, so update our settings file
		settings->saveSettings();
		inpt->saveKeyBindings();

		snd->stopMusic();
		showLoading();
		setRequestedGameState(new GameStateTitle());

		save_load->setGameSlot(0);
	}

	// if user closes the window
	if (inpt->done) {
		menu->closeAll();

		if (save_on_exit)
			save_load->saveGame();

		settings->saveSettings();
		inpt->saveKeyBindings();

		snd->stopMusic();
		exitRequested = true;
	}
}

/**
 * Check for log messages from various child objects
 */
void GameStatePlay::checkLog() {

	// If the player has just respawned, we want to clear the HUD log
	if (pc->respawn) {
		menu->hudlog->clear();
	}

	while (!pc->log_msg.empty()) {
		const std::string& str = pc->log_msg.front().first;
		const int msg_type = pc->log_msg.front().second;

		menu->questlog->add(str, MenuLog::TYPE_MESSAGES, msg_type);
		menu->hudlog->add(str, msg_type);

		pc->log_msg.pop();
	}
}

/**
 * Check if we need to open book
 */
void GameStatePlay::checkBook() {
	// Map events can open books
	if (!mapr->show_book.empty()) {
		menu->book->setBookFilename(mapr->show_book);
		mapr->show_book = "";
	}

	// items can be readable books
	if (!menu->inv->show_book.empty()) {
		menu->book->setBookFilename(menu->inv->show_book);
		menu->inv->show_book = "";
	}
}

void GameStatePlay::loadTitles() {
	FileParser infile;
	// @CLASS GameStatePlay: Titles|Description of engine/titles.txt
	if (infile.open("engine/titles.txt", FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) {
		while (infile.next()) {
			if (infile.new_section && infile.section == "title") {
				Title t;
				titles.push_back(t);
			}

			if (titles.empty()) continue;

			Title& title = titles.back();

			if (infile.key == "title") {
				// @ATTR title.title|string|The displayed title.
				title.title = infile.val;
			}
			else if (infile.key == "level") {
				// @ATTR title.level|int|Requires level.
				title.level = Parse::toInt(infile.val);
			}
			else if (infile.key == "power") {
				// @ATTR title.power|power_id|Requires power.
				title.power = powers->verifyID(Parse::toPowerID(infile.val), &infile, !PowerManager::ALLOW_ZERO_ID);
			}
			else if (infile.key == "requires_status") {
				// @ATTR title.requires_status|list(string)|Requires status.
				std::string repeat_val = Parse::popFirstString(infile.val);
				while (!repeat_val.empty()) {
					title.requires_status.push_back(camp->registerStatus(repeat_val));
					repeat_val = Parse::popFirstString(infile.val);
				}
			}
			else if (infile.key == "requires_not_status") {
				// @ATTR title.requires_not_status|list(string)|Requires not status.
				std::string repeat_val = Parse::popFirstString(infile.val);
				while (!repeat_val.empty()) {
					title.requires_not_status.push_back(camp->registerStatus(repeat_val));
					repeat_val = Parse::popFirstString(infile.val);
				}
			}
			else if (infile.key == "primary_stat") {
				// @ATTR title.primary_stat|predefined_string, predefined_string : Primary stat, Lesser primary stat|Required primary stat(s). The lesser stat is optional.
				title.primary_stat_1 = Parse::popFirstString(infile.val);
				title.primary_stat_2 = Parse::popFirstString(infile.val);
			}
			else infile.error("GameStatePlay: '%s' is not a valid key.", infile.key.c_str());
		}
		infile.close();
	}
}

void GameStatePlay::checkTitle() {
	if (!pc->stats.check_title || titles.empty())
		return;

	int title_id = -1;

	for (unsigned i=0; i<titles.size(); i++) {
		if (titles[i].title.empty())
			continue;

		if (titles[i].level > 0 && pc->stats.level < titles[i].level)
			continue;
		if (titles[i].power > 0 && std::find(pc->stats.powers_list.begin(), pc->stats.powers_list.end(), titles[i].power) == pc->stats.powers_list.end())
			continue;
		if (!titles[i].primary_stat_1.empty() && !checkPrimaryStat(titles[i].primary_stat_1, titles[i].primary_stat_2))
			continue;

		bool status_failed = false;
		for (size_t j = 0; j < titles[i].requires_status.size(); ++j) {
			if (!camp->checkStatus(titles[i].requires_status[j])) {
				status_failed = true;
				break;
			}
		}
		for (size_t j = 0; j < titles[i].requires_not_status.size(); ++j) {
			if (camp->checkStatus(titles[i].requires_not_status[j])) {
				status_failed = true;
				break;
			}
		}

		if (status_failed)
			continue;

		// Title meets the requirements
		title_id = i;
		break;
	}

	if (title_id != -1) pc->stats.character_subclass = titles[title_id].title;
	pc->stats.check_title = false;
	pc->stats.refresh_stats = true;
}

void GameStatePlay::checkEquipmentChange() {
	if (menu->inv->changed_equipment) {
		// force the actionbar to update when we change gear
		menu->act->updated = true;

		pc->loadAnimations();

		if (pc->feet_index != -1) {
			ItemID feet_id = menu->inv->inventory[MenuInventory::EQUIPMENT][pc->feet_index].item;
			if (items->isValid(feet_id))
				pc->loadStepFX(items->items[feet_id]->stepfx);
		}
	}

	menu->inv->changed_equipment = false;
}

void GameStatePlay::checkLootDrop() {

	// if the player has dropped an item from the inventory
	while (!menu->drop_stack.empty()) {
		if (!menu->drop_stack.front().empty()) {
			loot->addLoot(menu->drop_stack.front(), pc->stats.pos, LootManager::DROPPED_BY_HERO);
		}
		menu->drop_stack.pop();
	}

	// if the player has dropped a quest reward because inventory full
	while (!camp->drop_stack.empty()) {
		if (!camp->drop_stack.front().empty()) {
			loot->addLoot(camp->drop_stack.front(), pc->stats.pos, LootManager::DROPPED_BY_HERO);
		}
		camp->drop_stack.pop();
	}

	// if the player been directly given items, but their inventory is full
	// this happens when adding currency from older save files
	while (!menu->inv->drop_stack.empty()) {
		if (!menu->inv->drop_stack.front().empty()) {
			loot->addLoot(menu->inv->drop_stack.front(), pc->stats.pos, LootManager::DROPPED_BY_HERO);
		}
		menu->inv->drop_stack.pop();
	}
}

/**
 * Removes items as required by certain powers
 */
void GameStatePlay::checkUsedItems() {
	for (unsigned i=0; i<powers->used_items.size(); i++) {
		menu->inv->remove(powers->used_items[i], 1);
	}
	for (unsigned i=0; i<powers->used_equipped_items.size(); i++) {
		menu->inv->inventory[MenuInventory::EQUIPMENT].remove(powers->used_equipped_items[i], 1);
		menu->inv->applyEquipment();
	}
	powers->used_items.clear();
	powers->used_equipped_items.clear();
}

/**
 * Marks the menu if it needs attention.
 */
void GameStatePlay::checkNotifications() {
	if (pc->newLevelNotification || menu->chr->getUnspent() > 0) {
		pc->newLevelNotification = false;
		menu->act->requires_attention[MenuActionBar::MENU_CHARACTER] = !menu->chr->visible;
	}
	if (menu->pow->newPowerNotification) {
		menu->pow->newPowerNotification = false;
		menu->act->requires_attention[MenuActionBar::MENU_POWERS] = !menu->pow->visible;
	}
	if (quests->newQuestNotification) {
		quests->newQuestNotification = false;
		menu->act->requires_attention[MenuActionBar::MENU_LOG] = !menu->questlog->visible && !pc->questlog_dismissed;
		pc->questlog_dismissed = false;
	}

	// if the player is transformed into a creature, don't notifications for the powers menu
	if (pc->stats.transformed) {
		menu->act->requires_attention[MenuActionBar::MENU_POWERS] = false;
	}
}

/**
 * If the player has clicked on an NPC, the game mode might be changed.
 * If a player walks away from an NPC, end the interaction with that NPC
 * If an NPC is giving a reward, process it
 */
void GameStatePlay::checkNPCInteraction() {
	if (pc->using_main1 || !pc->stats.humanoid)
		return;

	// reset movement restrictions when we're not in dialog
	if (!menu->talker->visible) {
		pc->allow_movement = true;
	}

	if (npc_id != -1 && !menu->isNPCMenuVisible()) {
		// if we have an NPC, but no NPC windows are open, clear the NPC
		resetNPC();
	}

	// get NPC by ID
	// event NPCs take precedence over map NPCs
	if (mapr->event_npc != "") {
		// if the player is already interacting with an NPC when triggering an event NPC, clear the current NPC
		if (npc_id != -1) {
			resetNPC();
		}
		npc_id = mapr->npc_id = npcs->getID(mapr->event_npc);
		menu->talker->npc_from_map = false;
	}
	else if (mapr->npc_id != -1) {
		npc_id = mapr->npc_id;
		menu->talker->npc_from_map = true;
	}
	mapr->event_npc = "";
	mapr->npc_id = -1;

	if (npc_id != -1) {
		bool interact_with_npc = false;
		if (menu->talker->npc_from_map) {
			float interact_distance = Utils::calcDist(pc->stats.pos, npcs->npcs[npc_id]->stats.pos);
			bool npc_is_alive = !npcs->npcs[npc_id]->stats.hero_ally || npcs->npcs[npc_id]->stats.hp > 0;

			if (interact_distance < eset->misc.interact_range && npc_is_alive) {
				interact_with_npc = true;
			}
			else {
				resetNPC();
			}
		}
		else {
			// npc is from event
			interact_with_npc = true;

			// since its impossible for the player to walk away from event NPCs, we disable their movement here
			pc->allow_movement = false;
		}

		if (interact_with_npc) {
			if (!menu->isNPCMenuVisible()) {
				if (inpt->pressing[Input::MAIN1] && inpt->usingMouse()) inpt->lock[Input::MAIN1] = true;
				if (inpt->pressing[Input::ACCEPT]) inpt->lock[Input::ACCEPT] = true;

				menu->closeAll();
				menu->talker->setNPC(npcs->npcs[npc_id]);
				menu->talker->chooseDialogNode(-1);
			}
		}
	}
}

void GameStatePlay::checkStash() {
	if (mapr->stash) {
		// If triggered, open the stash and inventory menus
		menu->closeAll();
		menu->inv->visible = true;
		menu->stash->visible = true;
		mapr->stash = false;
		menu->stash->validate(menu->drop_stack);
	}
	else if (menu->stash->visible) {
		// Close stash if inventory is closed
		if (!menu->inv->visible) {
			menu->resetDrag();
			menu->stash->visible = false;
			if (menu->inv->sfx_close == 0) {
				snd->play(menu->stash->sfx_close, snd->DEFAULT_CHANNEL, snd->NO_POS, !snd->LOOP);
			}
		}

		// If the player walks away from the stash, close its menu
		float interact_distance = Utils::calcDist(pc->stats.pos, mapr->stash_pos);
		if (interact_distance > eset->misc.interact_range || !pc->stats.alive) {
			menu->resetDrag();
			menu->stash->visible = false;
			snd->play(menu->stash->sfx_close, snd->DEFAULT_CHANNEL, snd->NO_POS, !snd->LOOP);
		}

	}

	// If the stash has been updated, save the game
	if (menu->stash->checkUpdates()) {
		save_load->saveGame();
	}
}

void GameStatePlay::checkCutscene() {
	if (!mapr->cutscene)
		return;

	showLoading();
	GameStateCutscene *cutscene = new GameStateCutscene(NULL);

	if (!cutscene->load(mapr->cutscene_file)) {
		delete cutscene;
		mapr->cutscene = false;
		return;
	}

	// handle respawn point and set game play game_slot
	cutscene->game_slot = save_load->getGameSlot();

	if (mapr->teleportation) {

		if (mapr->teleport_mapname != "")
			mapr->respawn_map = mapr->teleport_mapname;

		mapr->respawn_point = mapr->teleport_destination;

	}
	else {
		mapr->respawn_point = pc->stats.pos;
	}

	if (eset->misc.save_oncutscene)
		save_load->saveGame();

	menu->closeAll();

	setRequestedGameState(cutscene);
}

void GameStatePlay::checkSaveEvent() {
	if (mapr->save_game) {
		mapr->respawn_point = pc->stats.pos;
		save_load->saveGame();
		mapr->save_game = false;
	}
}

/**
 * Recursively update the action bar powers based on equipment
 */
void GameStatePlay::updateActionBar(unsigned index) {
	if (menu->act->slots_count == 0 || index > menu->act->slots_count - 1) return;

	if (items->items.empty()) return;

	for (unsigned i = index; i < menu->act->slots_count; i++) {
		if (menu->act->hotkeys[i] == 0) continue;

		PowerID id = menu->inv->getPowerMod(menu->act->hotkeys_mod[i]);
		if (id > 0) {
			menu->act->hotkeys_mod[i] = id;
			return updateActionBar(i);
		}
	}
}

/**
 * Process all actions for a single frame
 * This includes some message passing between child object
 */
void GameStatePlay::logic() {
	if (inpt->window_resized)
		refreshWidgets();

	curs->setLowHP(pc->isLowHpCursorEnabled() && pc->isLowHp());

	checkCutscene();

	// check menus first (top layer gets mouse click priority)
	menu->logic();

	if (!isPaused()) {
		if (!second_timer.isEnd())
			second_timer.tick();
		else {
			pc->time_played++;
			second_timer.reset(Timer::BEGIN);
		}

		// these actions only occur when the game isn't paused
		if (pc->stats.alive) checkLoot();
		checkEnemyFocus();
		checkNPCFocus();
		if (pc->stats.alive) {
			mapr->checkHotspots();
			mapr->checkNearestEvent();
			checkNPCInteraction();
		}
		checkTitle();

		menu->act->checkAction(pc->action_queue);
		pc->logic();

		if (netmgr && netmgr->isActive()) {
			netmgr->pollGame();
			netmgr->sendPosition(pc->stats.pos.x, pc->stats.pos.y);
			if (!sent_own_appearance) {
				sendOwnAppearance();
				sent_own_appearance = true;
			}
			syncOwnAction();
			syncRemoteEntities();
			syncRemotePowerVisuals();
			syncRemoteEnemies();
			applyEnemyHitReports();
			applyPlayerHits();
			snapshotEnemyHpBeforeCombat();
		}
		updateNetTargets();

		// Horde mode is host/single-player only; a net client mirrors the host's enemies.
		if (settings->net_join_target.empty()) {
			std::vector<Entity*> gone = horde->logic();
			for (size_t i = 0; i < gone.size(); ++i) {
				if (hazards->last_enemy == gone[i]) hazards->last_enemy = NULL;
				if (menu->enemy->enemy == gone[i]) menu->enemy->enemy = NULL;
				if (pc->cursor_enemy == gone[i]) pc->cursor_enemy = NULL;
			}
			if (!gone.empty()) {
				pc->stats.target_corpse = NULL;
				pc->stats.target_nearest = NULL;
				pc->stats.target_nearest_corpse = NULL;
			}
		}

		// transfer hero data to enemies, for AI use
		if (pc->stats.get(Stats::STEALTH) > 100) entitym->hero_stealth = 100;
		else entitym->hero_stealth = pc->stats.get(Stats::STEALTH);

		entitym->logic();
		hazards->logic();

		forwardNetPlayerHits();

		if (netmgr && netmgr->isActive()) {
			reportEnemyHitsAfterCombat();
			syncOwnEnemies();
		}

		loot->logic();
		npcs->logic();

		comb->logic(mapr->cam.pos);
	}

	// close menus when the player dies, but still allow them to be reopened
	if (pc->close_menus) {
		pc->close_menus = false;
		menu->closeAll();
	}

	// these actions occur whether the game is paused or not.
	// TODO Why? Some of these probably don't need to be executed when paused
	checkTeleport();
	checkLootDrop();
	checkLog();
	checkBook();
	checkEquipmentChange();
	checkUsedItems();
	checkStash();
	checkSaveEvent();
	checkNotifications();
	checkCancel();

	mapr->logic(isPaused());
	mapr->enemies_cleared = entitym->isCleared();
	quests->logic();

	pc->checkTransform();

	// change hero powers on transformation
	if (pc->setPowers) {
		pc->setPowers = false;
		if (!pc->stats.humanoid && menu->pow->visible) menu->closeRight();
		// save ActionBar state and lock slots from removing/replacing power
		for (int i = 0; i < MenuActionBar::SLOT_MAX ; i++) {
			menu->act->hotkeys_temp[i] = menu->act->hotkeys[i];
			menu->act->hotkeys[i] = 0;
		}
		int count = MenuActionBar::SLOT_MAIN1;
		// put creature powers on action bar
		for (size_t i=0; i<pc->charmed_stats->powers_ai.size(); i++) {
			if (powers->isValid(pc->charmed_stats->powers_ai[i].id) && powers->powers[pc->charmed_stats->powers_ai[i].id]->beacon != true) {
				menu->act->hotkeys[count] = pc->charmed_stats->powers_ai[i].id;
				menu->act->locked[count] = true;
				count++;
				if (count == MenuActionBar::SLOT_MAX)
					count = 0;
				else if (count == MenuActionBar::SLOT_MAIN1)
					// we've filled the actionbar, stop adding powers to it
					break;
			}
		}
		if (pc->stats.manual_untransform && powers->isValid(pc->untransform_power)) {
			menu->act->hotkeys[count] = pc->untransform_power;
			menu->act->locked[count] = true;
		}
		else if (pc->stats.manual_untransform && pc->untransform_power == 0)
			Utils::logError("GameStatePlay: Untransform power not found, you can't untransform manually");

		menu->act->updated = true;

		// reapply equipment if the transformation allows it
		if (pc->stats.transform_with_equipment)
			menu->inv->applyEquipment();
	}
	// revert hero powers
	if (pc->revertPowers) {
		pc->revertPowers = false;

		// restore ActionBar state
		for (int i = 0; i < MenuActionBar::SLOT_MAX; i++) {
			menu->act->hotkeys[i] = menu->act->hotkeys_temp[i];
			menu->act->locked[i] = false;
		}

		menu->act->updated = true;

		// also reapply equipment here, to account items that give bonuses to base stats
		menu->inv->applyEquipment();
	}

	// when the hero (re)spawns, reapply equipment & passive effects
	if (pc->respawn) {
		pc->stats.alive = true;
		pc->stats.corpse = false;
		pc->stats.cur_state = StatBlock::ENTITY_STANCE;
		menu->inv->applyEquipment();
		menu->inv->changed_equipment = true;
		checkEquipmentChange();
		pc->stats.hp = pc->stats.get(Stats::HP_MAX);
		pc->stats.logic();
		pc->stats.recalc();
		menu->pow->resetToBasePowers();
		menu->pow->setUnlockedPowers();
		powers->activatePassives(&pc->stats);
		pc->respawn = false;
	}

	// use a normal mouse cursor is menus are open
	if (menu->menus_open) {
		curs->setCursor(CursorManager::CURSOR_NORMAL);
	}

	// update the action bar as it may have been changed by items
	if (menu->act->updated) {
		menu->act->updated = false;

		// set all hotkeys to their base powers
		for (unsigned i = 0; i < menu->act->slots_count; i++) {
			menu->act->hotkeys_mod[i] = menu->act->hotkeys[i];
		}

		updateActionBar(UPDATE_ACTIONBAR_ALL);
	}

	// reload music if changed in the pause menu
	if (menu->exit->reload_music) {
		mapr->loadMusic();
		menu->exit->reload_music = false;
	}
}


/**
 * Detects a fresh transition of our own hero into StatBlock::ENTITY_POWER
 * (an attack or skill use) and sends pc->attack_anim once via
 * NetManager::sendAction(), so remote players see us swing/cast/shoot
 * instead of just sliding to a new position. Animation only -- doesn't
 * touch hazards/damage, so this can't hurt anyone over the network yet.
 */
void GameStatePlay::syncOwnAction() {
	int cur_state = pc->stats.cur_state;
	if (cur_state == StatBlock::ENTITY_POWER && last_pc_state != StatBlock::ENTITY_POWER) {
		if (!pc->attack_anim.empty())
			netmgr->sendAction(pc->attack_anim);

		// Also tell everyone else to visually replay the hazard itself
		// (e.g. a fireball actually traveling across the screen), not just
		// our body swinging/casting -- see NetManager.h's PowerVisualPacket
		// comment for how the receiving side keeps this damage-free.
		if (powers->isValid(pc->current_power) && powers->powers[pc->current_power]->use_hazard) {
			netmgr->sendPowerVisual(static_cast<uint32_t>(pc->current_power), pc->stats.pos.x, pc->stats.pos.y, pc->act_target.x, pc->act_target.y);
		}
	}
	last_pc_state = cur_state;
}

/**
 * Sends our own look (body/head + per-layer equipped item graphics) once
 * per connection, via NetManager::sendAppearance(). Uses a throwaway
 * GameSlotPreview purely to compute the layer list the same way the
 * character-select screen does (GameSlotPreview::getPreviewGfx) -- nothing
 * from it is rendered locally.
 */
void GameStatePlay::sendOwnAppearance() {
	GameSlotPreview temp_preview;
	temp_preview.setStatBlock(&pc->stats);
	temp_preview.loadDefaultGraphics();
	std::vector<std::string> layers = temp_preview.getPreviewGfx(menu->inv);
	netmgr->sendAppearance(pc->stats.gfx_base, pc->stats.gfx_head, layers);
}

/**
 * Applies incoming PowerVisualPacket events (see NetManager.h) by calling
 * the real PowerManager::activate() with the remote player's minimal
 * StatBlock (see RemotePlayerVisual) -- this gets the power's own
 * animation/speed/lifespan for free, exactly like a real cast, so e.g. a
 * fireball actually travels across the screen instead of just the
 * caster's body animation. The resulting Hazard(s) are marked
 * cosmetic_only so HazardManager::logic() skips their entity collision --
 * the real hit already happened (or will) on whichever side the power
 * actually originated from; this side must not deal damage or grant
 * rewards a second time.
 */
void GameStatePlay::syncRemotePowerVisuals() {
	std::vector<PowerVisualPacket> events = netmgr->drainPowerVisualEvents();
	if (events.empty())
		return;

	for (size_t i = 0; i < events.size(); i++) {
		std::map<uint32_t, RemotePlayerVisual>::iterator found = remote_players.find(events[i].player_id);
		if (found == remote_players.end())
			continue; // haven't spawned their GameSlotPreview yet; drop it

		FPoint origin(events[i].origin_x, events[i].origin_y);
		FPoint target(events[i].target_x, events[i].target_y);
		size_t hazards_before = powers->hazards.size();

		powers->activate(static_cast<PowerID>(events[i].power_id), found->second.stats, origin, target);

		if (powers->hazards.size() > hazards_before)
			powers->hazards.back()->cosmetic_only = true;
	}
}

/**
 * Spawn/update/despawn one GameSlotPreview per connected player_id, from
 * NetManager::getRemotePositions()/getRemoteAppearances(). Each remote
 * player renders with their own body/head/equipment, the same layered
 * renderer used for the character-select screen -- not a placeholder
 * monster. A player_id with a position but no appearance yet (it arrives
 * separately, once) just doesn't render until the appearance packet lands.
 */
void GameStatePlay::syncRemoteEntities() {
	const std::map<uint32_t, NetPos>& positions = netmgr->getRemotePositions();
	const std::map<uint32_t, PlayerAppearance>& appearances = netmgr->getRemoteAppearances();

	// Apply any attack/skill triggers first, so the position pass below
	// knows not to immediately override them with stance/run.
	std::vector<std::pair<uint32_t, std::string> > actions = netmgr->drainActionEvents();
	for (size_t i = 0; i < actions.size(); i++) {
		std::map<uint32_t, RemotePlayerVisual>::iterator found = remote_players.find(actions[i].first);
		if (found != remote_players.end() && !actions[i].second.empty()) {
			found->second.preview->setAnimation(actions[i].second);
			found->second.in_action_anim = true;
		}
	}

	// Spawn/update
	for (std::map<uint32_t, NetPos>::const_iterator it = positions.begin(); it != positions.end(); ++it) {
		uint32_t id = it->first;
		FPoint new_pos(it->second.x, it->second.y);

		std::map<uint32_t, RemotePlayerVisual>::iterator found = remote_players.find(id);
		if (found == remote_players.end()) {
			std::map<uint32_t, PlayerAppearance>::const_iterator app_it = appearances.find(id);
			if (app_it == appearances.end())
				continue; // wait for their appearance packet before spawning anything

			RemotePlayerVisual rpv;
			rpv.stats = new StatBlock();
			rpv.stats->gfx_base = app_it->second.gfx_base;
			rpv.stats->gfx_head = app_it->second.gfx_head;
			rpv.stats->direction = 6;
			rpv.stats->pos = new_pos;
			rpv.in_action_anim = false;

			rpv.preview = new GameSlotPreview();
			rpv.preview->setStatBlock(rpv.stats);
			rpv.preview->loadGraphics(app_it->second.layers);

			remote_players[id] = rpv;
			remote_last_pos[id] = new_pos;
		}
		else {
			StatBlock *s = found->second.stats;
			GameSlotPreview *preview = found->second.preview;

			// Let a triggered attack/skill animation play out before position
			// updates are allowed to switch it back to stance/run.
			if (found->second.in_action_anim) {
				if (preview->activeAnimation && preview->activeAnimation->isCompleted())
					found->second.in_action_anim = false;
			}

			if (!found->second.in_action_anim) {
				FPoint old_pos = remote_last_pos[id];
				float dx = new_pos.x - old_pos.x;
				float dy = new_pos.y - old_pos.y;

				if (dx*dx + dy*dy > 0.0001f) {
					s->direction = Utils::calcDirection(old_pos.x, old_pos.y, new_pos.x, new_pos.y);
					if (preview->activeAnimation && preview->activeAnimation->getName() != "run")
						preview->setAnimation("run");
				}
				else if (preview->activeAnimation && preview->activeAnimation->getName() != "stance") {
					preview->setAnimation("stance");
				}
			}

			s->pos = new_pos;
			remote_last_pos[id] = new_pos;
		}

		remote_players[id].preview->logic();
	}

	// Despawn anyone no longer in positions (disconnected)
	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end();) {
		if (positions.find(it->first) == positions.end()) {
			delete it->second.preview;
			delete it->second.stats;
			remote_last_pos.erase(it->first);
			remote_players.erase(it++);
		}
		else {
			++it;
		}
	}
}

/**
 * Host only (no-op elsewhere, see NetManager::sendEnemySpawn/sendEnemyState/
 * sendEnemyDespawn's internal role checks): announce every networked map
 * enemy (see EntityManager's net_id assignment in handleNewMap) the first
 * time it's seen, then broadcast its position/anim/hp every tick. Diffs
 * against announced_enemy_ids to notice when one disappears (dies and the
 * map is reloaded, since entities are only ever removed at map change --
 * see EntityManager::handleNewMap) and tell clients to drop it.
 */
void GameStatePlay::syncOwnEnemies() {
	std::set<uint32_t> current_ids;

	for (size_t i = 0; i < entitym->entities.size(); i++) {
		Entity *e = entitym->entities[i];
		StatBlock &s = e->stats;
		if (s.net_id == 0 || s.net_proxy)
			continue;

		current_ids.insert(s.net_id);

		if (announced_enemy_ids.find(s.net_id) == announced_enemy_ids.end()) {
			netmgr->sendEnemySpawn(s.net_id, e->type_filename);
			announced_enemy_ids.insert(s.net_id);
		}

		float hp_max = s.get(Stats::HP_MAX);
		float hp_pct_f = (hp_max > 0) ? (s.hp / hp_max) * 100.0f : 0.0f;
		hp_pct_f = std::max(0.0f, std::min(100.0f, hp_pct_f));
		uint8_t hp_percent = static_cast<uint8_t>(hp_pct_f);

		bool alive = (s.cur_state != StatBlock::ENTITY_DEAD && s.cur_state != StatBlock::ENTITY_CRITDEAD);
		std::string anim_name = e->activeAnimation ? e->activeAnimation->getName() : "";

		static std::map<uint32_t, bool> debug_last_alive;
		std::map<uint32_t, bool>::iterator dbg = debug_last_alive.find(s.net_id);
		if (dbg == debug_last_alive.end() || dbg->second != alive) {
			Utils::logInfo("NetManager: enemy net_id=%u alive=%d hp=%.1f cur_state=%d (host-side)", static_cast<unsigned>(s.net_id), alive ? 1 : 0, static_cast<double>(s.hp), s.cur_state);
			debug_last_alive[s.net_id] = alive;
		}

		netmgr->sendEnemyState(s.net_id, s.pos.x, s.pos.y, s.direction, hp_percent, alive, anim_name);
	}

	for (std::set<uint32_t>::iterator it = announced_enemy_ids.begin(); it != announced_enemy_ids.end();) {
		if (current_ids.find(*it) == current_ids.end()) {
			netmgr->sendEnemyDespawn(*it);
			announced_enemy_ids.erase(it++);
		}
		else {
			++it;
		}
	}
}

/**
 * Host only in effect (drainEnemyHitEvents() is only ever non-empty on the
 * host -- see EnemyHitPacket handling in NetManager::pollGame). Applies
 * damage a client's own local hit already computed (see the big comment in
 * NetManager.h: the damage number is correct as-is, since the client hit an
 * identical enemy definition) to the real, authoritative Entity, WITHOUT
 * going through StatBlock::takeDamage() -- that would grant this hit's XP/
 * loot again on the host's side, on top of what the attacking client
 * already granted itself locally. This only needs to keep hp/death state
 * correct for what the host (and, via syncOwnEnemies, everyone else) sees.
 */
void GameStatePlay::applyEnemyHitReports() {
	std::vector<std::pair<uint32_t, float> > hits = netmgr->drainEnemyHitEvents();
	if (hits.empty())
		return;

	for (size_t h = 0; h < hits.size(); h++) {
		for (size_t i = 0; i < entitym->entities.size(); i++) {
			StatBlock &s = entitym->entities[i]->stats;
			if (s.net_id != hits[h].first || s.net_id == 0)
				continue;

			if (s.cur_state == StatBlock::ENTITY_DEAD || s.cur_state == StatBlock::ENTITY_CRITDEAD)
				break;

			s.hp -= hits[h].second;
			if (s.hp <= 0) {
				s.hp = 0;
				s.cur_state = StatBlock::ENTITY_DEAD;
				if (!s.corpse_has_collision)
					mapr->collider.unblock(s.pos.x, s.pos.y);
			}
			break;
		}
	}
}

/**
 * Client only in effect (getRemoteEnemies() is only ever non-empty on a
 * client -- see EnemySpawnPacket/EnemyStatePacket handling in NetManager::
 * pollGame). Spawns/updates/despawns one real Entity per net_id directly
 * into entitym->entities -- not a lightweight preview like RemotePlayerVisual,
 * because these need to be hit by our own local hazards through the
 * existing, unmodified HazardManager::logic() collision loop. Position/
 * direction/hp/animation are forced from the network every tick; see
 * StatBlock::net_proxy and the matching check in EntityManager::logic()
 * for why that's safe (no local AI fighting the synced state).
 */
void GameStatePlay::syncRemoteEnemies() {
	const std::map<uint32_t, RemoteEnemyState>& remote = netmgr->getRemoteEnemies();

	for (std::map<uint32_t, RemoteEnemyState>::const_iterator it = remote.begin(); it != remote.end(); ++it) {
		uint32_t net_id = it->first;
		const RemoteEnemyState& re = it->second;
		if (re.type_filename.empty())
			continue; // EnemySpawnPacket (reliable) just hasn't arrived yet

		std::map<uint32_t, Entity*>::iterator found = remote_enemies_local.find(net_id);
		Entity *e;
		if (found == remote_enemies_local.end()) {
			e = entitym->getEntityPrototype(re.type_filename);
			e->stats.net_id = net_id;
			e->stats.net_proxy = true;
			e->stats.recalc();
			e->stats.pos = FPoint(re.x, re.y);
			e->stats.direction = re.direction;
			e->setAnimation("stance");
			entitym->entities.push_back(e);
			remote_enemies_local[net_id] = e;
		}
		else {
			e = found->second;

			// If our own local hazard already killed this proxy (real
			// Entity::takeHit()/takeDamage() ran on it as a side effect of
			// this client's own hazards->logic(), same as single-player --
			// see the big comment in NetManager.h), don't let a stale
			// "still alive" broadcast -- the host hasn't processed our
			// sendEnemyHit() report yet -- resurrect it before that round
			// trip catches up. Once dead here, it stays dead until an
			// actual EnemyDespawnPacket removes it below.
			//
			// takeDamage() only sets cur_state -- it doesn't touch the
			// animation (that's normally EntityBehavior's job, which
			// EntityManager::logic() skips entirely for net_proxy entities,
			// see StatBlock::net_proxy). Without this, the proxy is dead
			// but visually frozen on whatever it was doing (stance/run),
			// looking exactly like a sync bug instead of a death.
			if (e->stats.cur_state == StatBlock::ENTITY_DEAD || e->stats.cur_state == StatBlock::ENTITY_CRITDEAD) {
				if (e->activeAnimation && e->activeAnimation->getName() != "die")
					e->setAnimation("die");
				continue;
			}

			e->stats.pos.x = re.x;
			e->stats.pos.y = re.y;
			e->stats.direction = re.direction;
		}

		float hp_max = e->stats.get(Stats::HP_MAX);
		e->stats.hp = hp_max * (static_cast<float>(re.hp_percent) / 100.0f);

		bool alive = (re.alive != 0);

		static std::map<uint32_t, uint8_t> debug_last_alive_recv;
		std::map<uint32_t, uint8_t>::iterator dbg = debug_last_alive_recv.find(net_id);
		if (dbg == debug_last_alive_recv.end() || dbg->second != re.alive) {
			Utils::logInfo("NetManager: enemy net_id=%u alive=%d hp_percent=%u cur_state=%d (client-side proxy)", static_cast<unsigned>(net_id), static_cast<int>(re.alive), static_cast<unsigned>(re.hp_percent), e->stats.cur_state);
			debug_last_alive_recv[net_id] = re.alive;
		}

		if (!alive) {
			if (e->stats.cur_state != StatBlock::ENTITY_DEAD && e->stats.cur_state != StatBlock::ENTITY_CRITDEAD) {
				e->stats.cur_state = StatBlock::ENTITY_DEAD;
				e->setAnimation("die");
			}
		}
		else if (!re.anim.empty()) {
			e->setAnimation(re.anim);
		}
	}

	// Despawn anyone the host stopped reporting (dead & map reloaded, etc.)
	for (std::map<uint32_t, Entity*>::iterator it = remote_enemies_local.begin(); it != remote_enemies_local.end();) {
		if (remote.find(it->first) == remote.end()) {
			for (size_t i = 0; i < entitym->entities.size(); i++) {
				if (entitym->entities[i] == it->second) {
					entitym->entities.erase(entitym->entities.begin() + i);
					break;
				}
			}
			it->second->unloadSounds();
			delete it->second;
			remote_enemy_hp_before_combat.erase(it->first);
			remote_enemies_local.erase(it++);
		}
		else {
			++it;
		}
	}
}

/**
 * Host only. Publishes every remote player's minimal StatBlock as an enemy AI
 * target (see EntityManager::net_targets); cleared everywhere else so the AI
 * behaves exactly like single-player when there is no network session.
 */
void GameStatePlay::updateNetTargets() {
	entitym->net_targets.clear();
	if (!netmgr || !netmgr->isServer())
		return;

	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		EntityManager::NetTarget nt;
		nt.id = it->first;
		nt.stats = it->second.stats;
		entitym->net_targets.push_back(nt);
	}
}

/**
 * Host only. Sends each enemy hazard that hit a remote player this tick to
 * that player's client (see PlayerHitPacket); always clears the queue.
 */
void GameStatePlay::forwardNetPlayerHits() {
	std::vector<HazardManager::NetPlayerHit> hits;
	hits.swap(hazards->net_player_hits);
	if (!netmgr || !netmgr->isServer())
		return;

	for (size_t i = 0; i < hits.size(); ++i) {
		PlayerHitPacket pkt;
		pkt.power_id = hits[i].power_id;
		pkt.pos_x = hits[i].pos.x;
		pkt.pos_y = hits[i].pos.y;
		pkt.crit_chance = hits[i].crit_chance;
		pkt.accuracy = hits[i].accuracy;
		pkt.num_damage = static_cast<uint32_t>(std::min(hits[i].damage.size(), PlayerHitPacket::MAX_DAMAGE_TYPES));
		for (size_t d = 0; d < pkt.num_damage; ++d) {
			pkt.dmg_min[d] = hits[i].damage[d].min;
			pkt.dmg_max[d] = hits[i].damage[d].max;
		}
		netmgr->sendPlayerHit(hits[i].player_id, pkt);
	}
}

/**
 * Client only in effect (drainPlayerHitEvents() is empty elsewhere). Rebuilds
 * the host's enemy hazard and runs it through our own hero's real takeHit(),
 * so defense/avoidance/resists/effects and death all behave like single-player.
 */
void GameStatePlay::applyPlayerHits() {
	std::vector<PlayerHitPacket> hits = netmgr->drainPlayerHitEvents();
	for (size_t i = 0; i < hits.size(); ++i) {
		const PlayerHitPacket& pkt = hits[i];
		if (!powers->isValid(static_cast<PowerID>(pkt.power_id)) || !pc->stats.alive)
			continue;

		if (!net_hit_src)
			net_hit_src = new StatBlock();
		net_hit_src->pos = FPoint(pkt.pos_x, pkt.pos_y);

		Hazard h(&mapr->collider);
		h.power = powers->powers[pkt.power_id];
		h.power_index = static_cast<PowerID>(pkt.power_id);
		h.source_type = Power::SOURCE_TYPE_ENEMY;
		h.src_stats = net_hit_src;
		h.pos = FPoint(pkt.pos_x, pkt.pos_y);
		h.crit_chance = pkt.crit_chance;
		h.accuracy = pkt.accuracy;
		for (size_t d = 0; d < h.damage.size() && d < pkt.num_damage && d < PlayerHitPacket::MAX_DAMAGE_TYPES; ++d) {
			h.damage[d].min = pkt.dmg_min[d];
			h.damage[d].max = pkt.dmg_max[d];
		}

		pc->takeHit(h);
	}
}

/**
 * Client only. Snapshots each proxy's hp right before HazardManager::logic()
 * runs this tick, so reportEnemyHitsAfterCombat() can tell how much damage
 * our own hazards just dealt.
 */
void GameStatePlay::snapshotEnemyHpBeforeCombat() {
	for (std::map<uint32_t, Entity*>::iterator it = remote_enemies_local.begin(); it != remote_enemies_local.end(); ++it) {
		remote_enemy_hp_before_combat[it->first] = it->second->stats.hp;
	}
}

/**
 * Client only. Reports whatever hp a proxy lost to our own hazards this
 * tick (see NetManager::sendEnemyHit) so the host's authoritative copy --
 * and everyone else's view -- reflects damage from every player, not just
 * the host's own hits. Rewards (XP/loot) already happened locally when
 * Entity::takeHit()/StatBlock::takeDamage() ran on our proxy as a normal
 * side effect of HazardManager::logic() -- this call has nothing to do
 * with that, see the comment in NetManager.h.
 */
void GameStatePlay::reportEnemyHitsAfterCombat() {
	for (std::map<uint32_t, Entity*>::iterator it = remote_enemies_local.begin(); it != remote_enemies_local.end(); ++it) {
		std::map<uint32_t, float>::iterator prev = remote_enemy_hp_before_combat.find(it->first);
		if (prev == remote_enemy_hp_before_combat.end())
			continue;

		float dealt = prev->second - it->second->stats.hp;
		if (dealt > 0.0f)
			netmgr->sendEnemyHit(it->first, dealt);
	}
}

/**
 * Render all graphics for a single frame
 */
void GameStatePlay::render() {
	if (mapr->is_spawn_map)
		return;

	// Create a list of Renderables from all objects not already on the map.
	// split the list into the beings alive (may move) and dead beings (must not move)
	std::vector<Renderable> rens;
	std::vector<Renderable> rens_dead;

	pc->addRenders(rens);

	entitym->addRenders(rens, rens_dead);

	npcs->addRenders(rens); // npcs cannot be dead

	loot->addRenders(rens, rens_dead);

	hazards->addRenders(rens, rens_dead);

	// Other connected players (see syncRemoteEntities()). GameSlotPreview
	// doesn't set map_pos on its own (it's normally used at a fixed screen
	// position, e.g. character select), so we fill it in here.
	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		std::vector<Renderable> player_rens;
		it->second.preview->addRenders(player_rens);
		for (size_t i = 0; i < player_rens.size(); ++i) {
			player_rens[i].map_pos = it->second.stats->pos;
		}
		rens.insert(rens.end(), player_rens.begin(), player_rens.end());
	}

	// render the static map layers plus the renderables
	mapr->render(rens, rens_dead);

	// mouseover tooltips
	loot->renderTooltips(mapr->cam.pos);

	if (mapr->map_change) {
		menu->mini->prerender(&mapr->collider, mapr->w, mapr->h);
		mapr->map_change = false;
	}
	menu->mini->setMapTitle(mapr->title);
	menu->mini->render(pc->stats.pos);
	menu->region_title->setTitle(mapr->title);
	menu->render();

	// render combat text last - this should make it obvious you're being
	// attacked, even if you have menus open
	if (!isPaused())
		comb->render();
}

bool GameStatePlay::isPaused() {
	return menu->pause;
}

void GameStatePlay::resetNPC() {
	if (menu->vendor->visible) {
		snd->play(menu->vendor->sfx_close, snd->DEFAULT_CHANNEL, snd->NO_POS, !snd->LOOP);
	}

	npc_id = -1;
	menu->talker->npc_from_map = true;
	menu->resetDrag();
	menu->vendor->setNPC(NULL);
	menu->talker->setNPC(NULL);
}

bool GameStatePlay::checkPrimaryStat(const std::string& first, const std::string& second) {
	int high = 0;
	size_t high_index = eset->primary_stats.list.size();
	size_t low_index = eset->primary_stats.list.size();

	for (size_t i = 0; i < eset->primary_stats.list.size(); ++i) {
		int stat = pc->stats.get_primary(i);
		if (stat > high) {
			if (high_index != eset->primary_stats.list.size()) {
				low_index = high_index;
			}
			high = stat;
			high_index = i;
		}
		else if (stat == high && low_index == eset->primary_stats.list.size()) {
			low_index = i;
		}
		else if (low_index == eset->primary_stats.list.size() || (low_index < eset->primary_stats.list.size() && stat > pc->stats.get_primary(low_index))) {
			low_index = i;
		}
	}

	// if the first primary stat doesn't match, we don't care about the second one
	if (high_index != eset->primary_stats.list.size() && first != eset->primary_stats.list[high_index].id)
		return false;

	if (!second.empty()) {
		if (low_index != eset->primary_stats.list.size() && second != eset->primary_stats.list[low_index].id)
			return false;
	}
	else if (pc->stats.get_primary(high_index) == pc->stats.get_primary(low_index)) {
		// titles that require a single stat are ignored if two stats are equal
		return false;
	}

	return true;
}

GameStatePlay::~GameStatePlay() {
	delete net_hit_src;
	delete horde;
	curs->setLowHP(false);

	// Leaving gameplay (Save & Exit, death, starting a different character...)
	// -- disconnect here rather than leaving the connection open for the rest
	// of the process. netmgr currently only ever connects/hosts once, at
	// startup (see main.cpp), so without this the other side keeps seeing our
	// last-known position/appearance frozen in place indefinitely once we're
	// back at the title screen and no longer calling sendPosition() etc.
	if (netmgr) {
		netmgr->shutdown();
	}

	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		delete it->second.preview;
		delete it->second.stats;
	}
	remote_players.clear();

	// Proxy Entities in remote_enemies_local also live in entitym->entities,
	// which owns and deletes them (see EntityManager::~EntityManager) -- just
	// drop our own references.
	remote_enemies_local.clear();

	delete quests;
	delete npcs;
	delete hazards;
	delete entitym;
	delete pc;
	delete mapr;
	delete menu;
	delete loot;
	delete camp;
	delete items;
	delete powers;
	delete fow;
	delete xp_scaling;

	delete enemyg;

	delete eventm;

	// NULL-ify shared game resources
	pc = NULL;
	menu = NULL;
	camp = NULL;
	enemyg = NULL;
	entitym = NULL;
	eventm = NULL;
	items = NULL;
	loot = NULL;
	mapr = NULL;
	menu_act = NULL;
	menu_powers = NULL;
	powers = NULL;
	fow = NULL;
	xp_scaling = NULL;
}

