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
#include "GameStateMultiplayer.h"
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
#include "MenuGameOver.h"
#include "MenuManager.h"
#include "NetManager.h"
#include "FontEngine.h"
#include "MenuMiniMap.h"
#include "MenuRunUpgrade.h"
#include "MenuSanctuary.h"
#include "MenuPowers.h"
#include "MenuRegionTitle.h"
#include "MenuStash.h"
#include "MenuTalker.h"
#include "MenuVendor.h"
#include "MessageEngine.h"
#include "ModManager.h"
#include "UtilsFileSystem.h"
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
	, run_upgrade(new MenuRunUpgrade())
	, sanctuary(new MenuSanctuary())
	, second_chance_used(false)
	, net_follow_teleport(false)
{
	revive_timer = 0;
	banner_ticks = 0;
	hurt_last_hp = -1;
	hurt_flash = 0;
	hurt_overlay = NULL;
	trailer_hud = false;
	trailer_cam_lock = false;
	trailer_rec_frame = -1;
	trailer_frames = 0;
	lan_address_frames = 0;
	lan_announced = false;
	for (int i = 0; i < 3; ++i) {
		room_labels[i] = new WidgetLabel();
		room_labels[i]->setJustify(FontEngine::JUSTIFY_RIGHT);
		room_labels[i]->setColor(Color(232, 184, 72, 255));
	}
	banner_label = new WidgetLabel();
	banner_label->setFont("font_region_title");
	banner_label->setJustify(FontEngine::JUSTIFY_CENTER);
	banner_label->setColor(Color(232, 184, 72, 255));
	net_send_frames = 0;
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
	run_upgrade->align();
	sanctuary->align();
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

	std::string mode_map = modeStartMap();
	if (!mode_map.empty()) {
		mapr->teleport_mapname = mode_map;
		mapr->teleport_destination.x = -1;
		mapr->teleport_destination.y = -1;
	}
}

void GameStatePlay::syncPartyMap() {
	if (netmgr->isServer())
		return;
	std::string map;
	float x = 0, y = 0;
	if (netmgr->takeMapChange(map, x, y) && map != mapr->getFilename() && Filesystem::fileExists(mods->locate(map))) {
		mapr->teleportation = true;
		mapr->teleport_mapname = map;
		mapr->teleport_destination.x = x;
		mapr->teleport_destination.y = y;
		net_follow_teleport = true;
		pc->logMsg(msg->get("Following the host to another area."), Avatar::MSG_UNIQUE);
	}
}

std::string GameStatePlay::modeStartMap() {
	// solo or in a group, everyone starts at camp (maps/lobby.txt) and enters
	// the horde through its portal
	if (settings->game_mode == "run" || runEdition()) return "maps/lobby.txt";
	if (settings->game_mode == "test") return "maps/dev_room.txt";
	return "";
}

void GameStatePlay::applyModeToLoadedGame() {
	std::string target = modeStartMap();
	if (target.empty()) {
		// Open world: a character saved inside a run arena or the dev room
		// goes back to the world's starting town instead.
		const std::string& saved = mapr->teleport_mapname;
		if (saved.compare(0, 9, "maps/run/") == 0 || saved == "maps/dev_room.txt" || saved == "maps/lobby.txt")
			target = "maps/world/aurora_a.txt";
		else
			return;
	}
	if (!Filesystem::fileExists(mods->locate(target))) {
		Utils::logError("GameStatePlay: game mode map '%s' not found, keeping the saved map", target.c_str());
		return;
	}
	mapr->teleport_mapname = target;
	mapr->teleport_destination.x = -1;
	mapr->teleport_destination.y = -1;
	mapr->teleportation = true;
	mapr->clearEvents();
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

		// a client only changes map by following the host (syncPartyMap):
		// its own exits, portals and caves are closed while connected
		if (mapr->teleportation && !mapr->teleport_mapname.empty() && !net_follow_teleport &&
			netmgr && netmgr->isActive() && !netmgr->isServer() &&
			!netmgr->getHostMap().empty() && mapr->teleport_mapname != netmgr->getHostMap()) {
			pc->logMsg(msg->get("Only the host can lead the party to another area."), Avatar::MSG_UNIQUE);
			mapr->teleportation = false;
			mapr->teleport_mapname.clear();
			return;
		}
		net_follow_teleport = false;

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

			// multiplayer: the host leads the party, clients follow it here
			if (netmgr && netmgr->isActive() && netmgr->isServer())
				netmgr->sendMapChange(teleport_mapname, pc->stats.pos.x, pc->stats.pos.y);

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
			// handleNewMap() deleted every entity, the network enemy proxies
			// included: forget them, syncRemoteEnemies() recreates the ones the
			// host still reports (a client follows the host from map to map)
			remote_enemies_local.clear();
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
	if (mapr->show_book == "rd:sanctuary") {
		// camp shrine (maps/lobby.txt): the Sanctuary, not a book
		sanctuary->open();
		mapr->show_book = "";
	}
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
		// the other players should see the new gear too
		sent_own_appearance = false;

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

	selftestUiShots();
	onlineLogic();

	// check menus first (top layer gets mouse click priority)
	menu->logic();

	// test hook: RD_GIVE_LEVELS=<n> grants n levels ~3s after entering an arena
	if (getenv("RD_GIVE_LEVELS") && horde->isRunMap(mapr->getFilename())) {
		static int frames = 0;
		if (++frames == settings->max_frames_per_sec * 3) {
			int n = atoi(getenv("RD_GIVE_LEVELS"));
			pc->stats.xp = eset->xp.getLevelXP(std::min(pc->stats.level + n, eset->xp.getMaxLevel()));
			Utils::logInfo("RunUpgrade test: xp set for level %d (hp_max %.0f, dmg %.0f-%.0f, crit %.0f)", pc->stats.level + n,
				static_cast<double>(pc->stats.get(Stats::HP_MAX)), static_cast<double>(pc->stats.getDamageMin(0)), static_cast<double>(pc->stats.getDamageMax(0)), static_cast<double>(pc->stats.get(Stats::CRIT)));
		}
		if (frames == settings->max_frames_per_sec * 9)
			Utils::logInfo("RunUpgrade test: now level %d (hp_max %.0f, dmg %.0f-%.0f, crit %.0f, speed x%.2f, attack x%.2f)", pc->stats.level,
				static_cast<double>(pc->stats.get(Stats::HP_MAX)), static_cast<double>(pc->stats.getDamageMin(0)), static_cast<double>(pc->stats.getDamageMax(0)), static_cast<double>(pc->stats.get(Stats::CRIT)),
				static_cast<double>(pc->stats.run_speed), static_cast<double>(pc->stats.run_attack_speed));
	}

	// test hook: RD_SHOWCASE="rd_mula,rd_boitata" puts those enemies around the
	// hero ~3s into a run; RD_SHOWCASE_SHOT=<dir> takes 4 screenshots after it
	if (getenv("RD_SHOWCASE") && horde->isRunMap(mapr->getFilename())) {
		static int frames = 0;
		const int fps = settings->max_frames_per_sec;
		if (++frames == fps * 3)
			horde->showcase(pc->stats.pos, getenv("RD_SHOWCASE"));
		if (getenv("RD_SHOWCASE_SHOT") && frames > fps * 3 && (frames - fps * 3) % (fps / 2) == 0 && frames <= fps * 5) {
			std::stringstream ss;
			ss << getenv("RD_SHOWCASE_SHOT") << "/showcase_" << (frames - fps * 3) / (fps / 2) << ".png";
			render_device->screenshot_request = ss.str();
		}
	}

	// test hook: RD_CAMP_SHOT=<dir> -- screenshot the camp, then its Sanctuary shrine
	if (getenv("RD_CAMP_SHOT") && mapr->getFilename() == "maps/lobby.txt") {
		static int f = 0;
		const int fps = settings->max_frames_per_sec;
		++f;
		if (f == fps * 3)
			render_device->screenshot_request = std::string(getenv("RD_CAMP_SHOT")) + "/camp.png";
		if (f == fps * 4)
			mapr->show_book = "rd:sanctuary";
		if (f == fps * 5)
			render_device->screenshot_request = std::string(getenv("RD_CAMP_SHOT")) + "/camp_sanctuary.png";
	}

	// Infinite Run: level-up upgrade choice (pauses single-player while open)
	run_upgrade->sanctuary = sanctuary;
	horde->sanctuary = sanctuary;
	run_upgrade->update(horde->isRunMap(mapr->getFilename()), &pc->stats);
	// test hook (see MenuSanctuary::logic): open the Sanctuary from Game Over
	if (getenv("RD_SANCTUARY_SHOT") && menu->game_over->visible && menu->game_over->show_sanctuary && !sanctuary->visible) {
		static int wait = 0;
		if (++wait == 40)
			render_device->screenshot_request = std::string(getenv("RD_SANCTUARY_SHOT")) + "/game_over.png";
		if (wait == 60)
			menu->game_over->sanctuary_clicked = true;
	}
	if (menu->game_over->sanctuary_clicked) {
		menu->game_over->sanctuary_clicked = false;
		sanctuary->open();
	}
	sanctuary->logic();
	menu->game_over->blocked = sanctuary->visible;

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
		selftestPvpAttack();
		pc->logic();

		if (netmgr && netmgr->isActive()) {
			netmgr->pollGame();
			// position/hp at NET_SEND_HZ instead of every frame; receivers
			// interpolate between samples (NetManager.h Step 7)
			if (++net_send_frames >= std::max(1, settings->max_frames_per_sec / NetManager::NET_SEND_HZ)) {
				net_send_frames = 0;
				netmgr->sendPosition(pc->stats.pos.x, pc->stats.pos.y, pc->stats.hp, static_cast<float>(pc->stats.get(Stats::HP_MAX)), pc->stats.alive, netmgr->isServer() && horde->isActive() ? horde->getWave() + 1 : 0, horde->getThemeIndex(), horde->getFamilyIndex());
			}
			if (netmgr->isServer())
				netmgr->setLocalInfo(pc->stats.name, mapr->title);
			if (!sent_own_appearance) {
				sendOwnAppearance();
				sent_own_appearance = true;
			}
			syncPartyMap();
			syncOwnAction();
			syncRemoteEntities();
			syncRemotePowerVisuals();
			applyAllyPowers();
			syncRemoteEnemies();
			applyEnemyHitReports();
			applyPlayerHits();
			snapshotEnemyHpBeforeCombat();
		}
		updateNetTargets();
		coopLogic();
		trailerLogic();

		// Horde mode is host/single-player only; a net client mirrors the host's enemies.
		if (!netmgr || !netmgr->isClient()) {
			std::vector<Entity*> gone = horde->logic();
			for (size_t i = 0; i < gone.size(); ++i) {
				if (hazards->last_enemy == gone[i]) hazards->last_enemy = NULL;
				if (menu->enemy->enemy == gone[i]) menu->enemy->enemy = NULL;
				if (pc->cursor_enemy == gone[i]) pc->cursor_enemy = NULL;
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
			if (net_send_frames == 0)
				netmgr->flushEnemyStates();
		}
		checkLostConnection();

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
 * instead of just sliding to a new position. Animation only -- damage to
 * other players (PvP) goes through HazardManager::pvp_targets instead.
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
	const std::string colors[3] = { pc->stats.color_skin, pc->stats.color_hair, pc->stats.color_cloth };
	netmgr->sendAppearance(pc->stats.name, pc->stats.gfx_base, pc->stats.gfx_head, layers, colors);
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
		netmgr->samplePlayerPos(id, new_pos.x, new_pos.y); // smoothed, see NetManager.h Step 7

		std::map<uint32_t, RemotePlayerVisual>::iterator found = remote_players.find(id);
		if (found == remote_players.end()) {
			std::map<uint32_t, PlayerAppearance>::const_iterator app_it = appearances.find(id);
			if (app_it == appearances.end())
				continue; // wait for their appearance packet before spawning anything

			RemotePlayerVisual rpv;
			rpv.stats = new StatBlock();
			rpv.stats->gfx_base = app_it->second.gfx_base;
			rpv.stats->gfx_head = app_it->second.gfx_head;
			rpv.stats->color_skin = app_it->second.color_skin;
			rpv.stats->color_hair = app_it->second.color_hair;
			rpv.stats->color_cloth = app_it->second.color_cloth;
			rpv.stats->direction = 6;
			rpv.stats->pos = new_pos;
			rpv.stats->hp = it->second.hp;
			rpv.stats->alive = it->second.alive;
			rpv.hp_max = it->second.hp_max;
			rpv.appearance = app_it->second;
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

			// new gear (or look): reload their graphics
			std::map<uint32_t, PlayerAppearance>::const_iterator app_it = appearances.find(id);
			if (app_it != appearances.end()) {
				const PlayerAppearance& app = app_it->second;
				PlayerAppearance& cur = found->second.appearance;
				if (app.gfx_base != cur.gfx_base || app.gfx_head != cur.gfx_head || app.layers != cur.layers ||
				    app.color_skin != cur.color_skin || app.color_hair != cur.color_hair || app.color_cloth != cur.color_cloth) {
					s->gfx_base = app.gfx_base;
					s->gfx_head = app.gfx_head;
					s->color_skin = app.color_skin;
					s->color_hair = app.color_hair;
					s->color_cloth = app.color_cloth;
					preview->loadGraphics(app.layers);
					found->second.in_action_anim = false;
					Utils::logInfo("NetManager: player %u changed equipment", static_cast<unsigned>(id));
				}
				cur = app;
			}

			// hp/death: flinch when hurt, hold the death pose while dead
			const NetPos& np = it->second;
			bool was_alive = s->alive;
			if (np.alive && was_alive && np.hp < s->hp && !found->second.in_action_anim) {
				preview->setAnimation("hit");
				found->second.in_action_anim = true;
			}
			s->hp = np.hp;
			s->alive = np.alive;
			found->second.hp_max = np.hp_max;
			if (!np.alive) {
				if (was_alive || !preview->activeAnimation || preview->activeAnimation->getName() != "die") {
					preview->setAnimation("die");
					found->second.in_action_anim = false;
				}
				s->pos = new_pos;
				remote_last_pos[id] = new_pos;
				remote_players[id].preview->logic();
				continue;
			}
			if (!was_alive) {
				preview->setAnimation("stance");
			}

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

		if (net_send_frames == 0)
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
			netmgr->sampleEnemyPos(net_id, e->stats.pos.x, e->stats.pos.y); // smoothed, see NetManager.h Step 7
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
			// the party's kills, for the run summary (see HordeManager::clientLogic)
			if (counted_dead.insert(net_id).second)
				horde->noteKill();
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
	hazards->pvp_targets.clear();

	// PvP (see NetManager.h Step 6): every other player on our map is a
	// target for our own hazards. A client only counts once it's on the
	// host's map -- positions don't say which map a player is on.
	if (netmgr && netmgr->isActive() && netmgr->isPvp() && pc->stats.alive &&
		(netmgr->isServer() || netmgr->getHostMap() == mapr->getFilename()))
	{
		for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
			if (!it->second.stats->alive)
				continue;
			HazardManager::PvpTarget pt;
			pt.player_id = it->first;
			pt.pos = it->second.stats->pos;
			hazards->pvp_targets.push_back(pt);
		}
	}

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
 * Sends each hazard that hit a remote player this tick to that player (see
 * PlayerHitPacket): enemy hazards on the host, our own hazards with PvP on.
 * Always clears the queue.
 */
void GameStatePlay::forwardNetPlayerHits() {
	std::vector<HazardManager::NetPlayerHit> hits;
	hits.swap(hazards->net_player_hits);
	if (!netmgr || !netmgr->isActive())
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
 * Hits against our own hero from elsewhere: a host enemy's hazard (clients)
 * or another player's hazard with PvP on (anyone). Rebuilds the hazard and
 * runs it through our own hero's real takeHit(),
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

		float hp_before = pc->stats.hp;
		pc->takeHit(h);
		Utils::logInfo("NetManager: our hero was hit, power_id=%u hp %.1f -> %.1f", static_cast<unsigned>(pkt.power_id), static_cast<double>(hp_before), static_cast<double>(pc->stats.hp));
	}
}

/**
 * A small health bar over every other living player (hp comes with their
 * position, see TickPacket), so co-op partners and PvP opponents can see
 * how hurt they are.
 */
void GameStatePlay::renderRemotePlayerBars() {
	const int BAR_W = 48;
	const int BAR_H = 5;
	const int BAR_OFFSET_Y = 132; // above the head, at the default zoom

	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		const StatBlock *s = it->second.stats;
		Point p = Utils::mapToScreen(s->pos.x, s->pos.y, mapr->cam.pos.x, mapr->cam.pos.y);
		int x = p.x - BAR_W / 2;
		int y = p.y - BAR_OFFSET_Y;

		// name above the bar (or above the body, when dead)
		if (!it->second.appearance.name.empty()) {
			font->setFont("font_regular");
			int name_y = s->alive ? y - font->getLineHeight() - 2 : p.y - font->getLineHeight() - 24;
			font->renderShadowed(it->second.appearance.name, p.x, name_y, FontEngine::JUSTIFY_CENTER, NULL, 0, font->getColor(FontEngine::COLOR_MENU_NORMAL));
		}

		if (!s->alive || it->second.hp_max <= 0)
			continue;

		float ratio = std::max(0.0f, std::min(1.0f, s->hp / it->second.hp_max));
		int fill = static_cast<int>(ratio * static_cast<float>(BAR_W - 2) + 0.5f);
		if (fill == 0 && s->hp > 0)
			fill = 1;

		render_device->drawRectangle(Point(x, y), Point(x + BAR_W - 1, y + BAR_H - 1), Color(20, 12, 12, 255));
		for (int row = 1; row < BAR_H - 1; ++row) {
			if (fill < BAR_W - 2)
				render_device->drawLine(x + 1 + fill, y + row, x + BAR_W - 2, y + row, Color(52, 24, 24, 255));
			if (fill > 0)
				render_device->drawLine(x + 1, y + row, x + fill, y + row, Color(196, 40, 36, 255));
		}
	}
}

/**
 * Client only: the host went away (NetManager dropped the session, see
 * NetManager.h Step 7). Say so, and reload the current map so its own
 * enemies spawn again -- we're playing alone now.
 */
void GameStatePlay::checkLostConnection() {
	if (!netmgr || !netmgr->takeLostConnection())
		return;

	Utils::logInfo("NetManager: lost the host (%s), reloading %s to play alone", netmgr->getLastError().c_str(), mapr->getFilename().c_str());
	if (netmgr->getLastError() == "host_left")
		pc->logMsg(msg->get("The host closed the game. You are now playing alone."), Avatar::MSG_UNIQUE);
	else
		pc->logMsg(msg->get("Connection to the host was lost. You are now playing alone."), Avatar::MSG_UNIQUE);

	announced_enemy_ids.clear();
	mapr->teleportation = true;
	mapr->teleport_mapname = mapr->getFilename();
	mapr->teleport_destination = pc->stats.pos;
}

/**
 * Automated UI screenshot hook (env RD_UI_SHOTS=<dir>): opens the main
 * panels one after another like a player would (same key inputs), saves a
 * screenshot of each into <dir>, then quits. For checking the UI theme
 * without clicking through it.
 */
void GameStatePlay::selftestUiShots() {
	static int f = 0;
	const char* dir_env = getenv("RD_UI_SHOTS");
	if (!dir_env)
		return;
	const std::string dir = std::string(dir_env) + "/";
	++f;

	struct Step { int frame; int key; const char* shot; };
	static const Step steps[] = {
		{ 60, Input::INVENTORY, NULL },
		{ 70, Input::CHARACTER, NULL },
		{ 110, -1, "ui_inventory_character.png" },
		{ 120, Input::INVENTORY, NULL },
		{ 130, Input::CHARACTER, NULL },
		{ 140, Input::POWERS, NULL },
		{ 150, Input::LOG, NULL },
		{ 190, -1, "ui_powers_log.png" },
		{ 200, Input::POWERS, NULL },
		{ 210, Input::LOG, NULL },
		{ 220, Input::CANCEL, NULL },
		{ 260, -1, "ui_pause.png" },
		{ 270, Input::CANCEL, NULL },
		{ 300, -1, "ui_hud.png" },
	};
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
		if (f == steps[i].frame) {
			if (steps[i].key >= 0) {
				inpt->pressing[steps[i].key] = true;
				inpt->lock[steps[i].key] = false;
			}
			if (steps[i].shot)
				render_device->screenshot_request = dir + steps[i].shot;
		}
		else if (f == steps[i].frame + 1 && steps[i].key >= 0) {
			inpt->pressing[steps[i].key] = false;
		}
	}
	// then die, for the game over screen
	if (f == 320) {
		pc->stats.hp = 0;
		pc->stats.alive = false;
		pc->stats.cur_state = StatBlock::ENTITY_DEAD;
		pc->stats.corpse = true;
	}
	if (f == 520)
		render_device->screenshot_request = dir + "ui_gameover.png";
	if (f == 540)
		exitRequested = true;
}

/**
 * Automated PvP test hook (env RD_NET_PVP_SELFTEST, net clients only, with
 * the host's PvP on): once a second, stand next to the first other player
 * and use our first hazard power on them. Pair it with --net-host --net-pvp
 * on the other instance and read both flare_log_*.txt.
 */
void GameStatePlay::selftestPvpAttack() {
	static int frames = 0;
	if (!netmgr || !netmgr->isClient() || !netmgr->isPvp() || remote_players.empty() || !getenv("RD_NET_PVP_SELFTEST"))
		return;
	++frames;

	// RD_NET_PVP_SELFTEST=<dir> (anything but "1"): also save what we see
	const std::string shot_dir = getenv("RD_NET_PVP_SELFTEST");
	if (shot_dir != "1") {
		if (frames == settings->max_frames_per_sec * 3 + settings->max_frames_per_sec / 2)
			render_device->screenshot_request = shot_dir + "/pvp_fighting.png";
		else if (!remote_players.begin()->second.stats->alive) {
			static int dead_frames = 0;
			if (++dead_frames == settings->max_frames_per_sec)
				render_device->screenshot_request = shot_dir + "/pvp_dead.png";
		}
	}

	// once, at ~5s: take off the first equipped item, so the other side has
	// to pick up our new look (appearance resend)
	if (frames == settings->max_frames_per_sec * 5) {
		for (int i = 0; i < menu->inv->inventory[MenuInventory::EQUIPMENT].getSlotNumber(); ++i) {
			ItemStack stack = menu->inv->inventory[MenuInventory::EQUIPMENT][i];
			if (stack.empty())
				continue;
			menu->inv->inventory[MenuInventory::EQUIPMENT][i].clear();
			menu->inv->add(stack, MenuInventory::CARRIED, ItemStorage::NO_SLOT, false, false);
			menu->inv->applyEquipment();
			menu->inv->changed_equipment = true;
			Utils::logInfo("PvP selftest: unequipped slot %d", i);
			break;
		}
	}

	if (frames % settings->max_frames_per_sec != 0 || !pc->stats.alive || !remote_players.begin()->second.stats->alive)
		return;

	PowerID power = 0;
	for (size_t i = 0; i < menu->act->hotkeys.size() && power == 0; ++i) {
		PowerID id = menu->act->hotkeys[i];
		if (id != 0 && powers->isValid(id) && powers->powers[id]->use_hazard)
			power = id;
	}
	if (power == 0)
		return;

	FPoint target = remote_players.begin()->second.stats->pos;
	pc->stats.pos = FPoint(target.x + 0.75f, target.y);

	ActionData action;
	action.power = power;
	action.target = target;
	pc->action_queue.push_back(action);
	Utils::logInfo("PvP selftest: using power %u on player %u", static_cast<unsigned>(power), static_cast<unsigned>(remote_players.begin()->first));
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

	// render the static map layers plus the renderables (through the world
	// filter: pixel art + colour grade; everything after stays sharp)
	render_device->beginWorldFilter();
	mapr->render(rens, rens_dead);
	render_device->endWorldFilter();

	renderRemotePlayerBars();
	renderCoop();
	renderWaveBanner();
	renderRoomCode();
	renderHurtFlash();

	// mouseover tooltips
	loot->renderTooltips(mapr->cam.pos);

	if (mapr->map_change) {
		menu->mini->prerender(&mapr->collider, mapr->w, mapr->h);
		mapr->map_change = false;
	}
	menu->mini->setMapTitle(mapr->title);
	menu->mini->net_players.clear();
	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it)
		menu->mini->net_players.push_back(it->second.stats->pos);
	// trailer mode: a clean screen unless F1 or a menu is open
	const bool clean = settings->trailer_mode && !trailer_hud && !menu->exit->visible && !menu->isDragging() &&
		!menu->inv->visible && !menu->pow->visible && !menu->chr->visible && !menu->game_over->visible;
	settings->trailer_clean = clean;
	if (!clean) {
		menu->mini->render(pc->stats.pos);
		menu->region_title->setTitle(mapr->title);
		menu->render();
	}
	run_upgrade->render();
	sanctuary->render();

	// render combat text last - this should make it obvious you're being
	// attacked, even if you have menus open
	if (!isPaused())
		comb->render();

	if (menu->exit->visible)
		renderSignature();
}

bool GameStatePlay::isPaused() {
	if (run_upgrade && run_upgrade->visible && !(netmgr && netmgr->isActive()))
		return true;
	if (sanctuary && sanctuary->visible && !(netmgr && netmgr->isActive()))
		return true;
	// the world keeps running in multiplayer: pausing the host would freeze
	// everyone (and time them out)
	if (netmgr && netmgr->isActive())
		return false;
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
	delete banner_label;
	for (int i = 0; i < 3; ++i)
		delete room_labels[i];
	delete hurt_overlay;
	delete run_upgrade;
	delete sanctuary;
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


namespace {
	const float REVIVE_RANGE = 1.75f;   // tiles
	const float REVIVE_SECONDS = 3.0f;
	const float REVIVE_HP = 0.4f;       // fraction of max hp
}

/**
 * Co-op Infinite Run (see HordeManager::coop_hold): a hero who falls while
 * an ally is still standing lies there waiting instead of getting the Game
 * Over screen; an ally who stays next to them for REVIVE_SECONDS brings
 * them back. Every machine decides for its own hero from the positions it
 * already receives, so this needs no messages of its own. The run ends
 * when the whole party is down.
 */
void GameStatePlay::coopLogic() {
	if (!netmgr || !netmgr->isActive())
		powers->shared_casts.clear();

	bool coop = netmgr && netmgr->isActive() && horde->isRunMap(mapr->getFilename()) &&
		(netmgr->isServer() || netmgr->getHostMap() == mapr->getFilename());

	int players = 1, allies_alive = 0;
	if (coop) {
		for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
			players++;
			if (it->second.stats->alive)
				allies_alive++;
		}
	}
	// test hooks: RD_COOP_GOD keeps this hero alive; RD_COOP_DIE=<s> drops it
	// <s> seconds into the run; RD_COOP_SHOT=<dir> screenshots the downed /
	// reviving moments
	if (getenv("RD_COOP_GOD"))
		pc->stats.hp = pc->stats.get(Stats::HP_MAX);
	if (getenv("RD_COOP_DIE") && horde->isRunMap(mapr->getFilename())) {
		static int frames = 0;
		if (++frames == atoi(getenv("RD_COOP_DIE")) * static_cast<int>(settings->max_frames_per_sec)) {
			Utils::logInfo("Coop test: dropping the hero at %.1f,%.1f", static_cast<double>(pc->stats.pos.x), static_cast<double>(pc->stats.pos.y));
			for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it)
				Utils::logInfo("Coop test: ally %u at %.1f,%.1f alive=%d", it->first, static_cast<double>(it->second.stats->pos.x), static_cast<double>(it->second.stats->pos.y), it->second.stats->alive ? 1 : 0);
			pc->stats.takeDamage(pc->stats.hp + 1, false, Power::SOURCE_TYPE_ENEMY);
		}
	}
	if (coop && getenv("RD_COOP_CAST")) {
		static int frames = 0;
		if (++frames == 25 * static_cast<int>(settings->max_frames_per_sec)) {
			PowerID id = static_cast<PowerID>(atoi(getenv("RD_COOP_CAST")));
			pc->stats.mp = pc->stats.get(Stats::MP_MAX);
			Utils::logInfo("Coop test: casting %d -> %d", static_cast<int>(id), powers->activate(id, &pc->stats, pc->stats.pos, pc->stats.pos) ? 1 : 0);
		}
	}
	if (getenv("RD_COOP_SHOT")) {
		static int shot = 0;
		const bool reviving_other = !revive_help.empty() && revive_help.begin()->second > 1.5f;
		if ((shot == 0 && revive_timer > 1.5f) || (shot <= 1 && reviving_other)) {
			render_device->screenshot_request = std::string(getenv("RD_COOP_SHOT")) + (reviving_other ? "/coop_reviving.png" : "/coop_downed.png");
			shot = reviving_other ? 2 : 1;
		}
	}

	// Sanctuary blessing "second chance": once per run, get back up alone
	if (second_chance_map != mapr->getFilename()) {
		second_chance_map = mapr->getFilename();
		second_chance_used = false;
	}
	const bool chance_ready = !second_chance_used && horde->isRunMap(mapr->getFilename()) && sanctuary->bonus("second_chance") > 0;
	if (chance_ready && pc->stats.corpse &&
		(pc->stats.cur_state == StatBlock::ENTITY_DEAD || pc->stats.cur_state == StatBlock::ENTITY_CRITDEAD))
	{
		second_chance_used = true;
		reviveHero(false);
		pc->stats.hp = pc->stats.get(Stats::HP_MAX) * 0.5f;
		Utils::logInfo("Sanctuary: second chance used");
		pc->logMsg(msg->get("Second chance! You rise again."), Avatar::MSG_UNIQUE);
	}

	pc->hold_game_over = (coop && allies_alive > 0) || chance_ready;
	pc->no_death_penalty = horde->isRunMap(mapr->getFilename());
	horde->coop_hold = pc->hold_game_over;
	horde->setPlayers(players);
	if (netmgr && netmgr->isClient())
		horde->clientLogic(netmgr->getHostWave() > 0 ? netmgr->getHostWave() - 1 : 0, netmgr->getHostTheme(), netmgr->getHostFamily());

	const float dt = 1.0f / static_cast<float>(std::max(1, static_cast<int>(settings->max_frames_per_sec)));
	const bool downed = coop && pc->stats.corpse &&
		(pc->stats.cur_state == StatBlock::ENTITY_DEAD || pc->stats.cur_state == StatBlock::ENTITY_CRITDEAD);

	// being revived
	if (!downed) {
		revive_timer = 0;
	}
	else {
		bool helped = false;
		for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
			if (it->second.stats->alive && Utils::calcDist(it->second.stats->pos, pc->stats.pos) <= REVIVE_RANGE)
				helped = true;
		}
		revive_timer = helped ? revive_timer + dt : std::max(0.0f, revive_timer - dt * 0.5f);
		if (revive_timer >= REVIVE_SECONDS)
			reviveHero();
	}

	// reviving others (only drawn here; each hero revives itself)
	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		float& t = revive_help[it->first];
		if (coop && !it->second.stats->alive && pc->stats.alive && Utils::calcDist(it->second.stats->pos, pc->stats.pos) <= REVIVE_RANGE)
			t = std::min(REVIVE_SECONDS, t + dt);
		else
			t = 0;
	}
}

void GameStatePlay::reviveHero(bool by_ally) {
	revive_timer = 0;
	pc->stats.hp = std::max(1.0f, pc->stats.get(Stats::HP_MAX) * REVIVE_HP);
	pc->stats.alive = true;
	pc->stats.corpse = false;
	pc->stats.cur_state = StatBlock::ENTITY_STANCE;
	pc->stats.death_penalty = false;
	menu->game_over->visible = false;
	menu->pow->resetToBasePowers();
	menu->pow->setUnlockedPowers();
	powers->activatePassives(&pc->stats);
	pc->stats.refresh_stats = true;
	if (by_ally) {
		pc->logMsg(msg->get("An ally brought you back!"), Avatar::MSG_UNIQUE);
		Utils::logInfo("Coop: revived by an ally");
	}
}

/**
 * Co-op overlays: "you fell" with the revive progress, and a progress bar
 * over a fallen ally we're standing next to.
 */
void GameStatePlay::renderCoop() {
	const Color gold(232, 184, 72, 255), dark(40, 16, 16, 255), fillc(120, 220, 110, 255);
	const int BAR_W = 120, BAR_H = 8;

	if (pc->hold_game_over && pc->stats.corpse) {
		font->setFont("font_regular");
		int cx = settings->view_w / 2, cy = settings->view_h - 190;
		font->renderShadowed(msg->get("You fell! Stay close to an ally to be revived."), cx, cy, FontEngine::JUSTIFY_CENTER, NULL, 0, gold);
		int x = cx - BAR_W / 2, y = cy + font->getLineHeight() + 6;
		int fill = static_cast<int>(static_cast<float>(BAR_W - 2) * std::min(1.0f, revive_timer / REVIVE_SECONDS));
		render_device->drawRectangle(Point(x, y), Point(x + BAR_W - 1, y + BAR_H - 1), dark);
		for (int row = 1; row < BAR_H - 1; ++row)
			if (fill > 0) render_device->drawLine(x + 1, y + row, x + fill, y + row, fillc);
	}

	for (std::map<uint32_t, RemotePlayerVisual>::iterator it = remote_players.begin(); it != remote_players.end(); ++it) {
		float t = revive_help[it->first];
		if (t <= 0)
			continue;
		Point p = Utils::mapToScreen(it->second.stats->pos.x, it->second.stats->pos.y, mapr->cam.pos.x, mapr->cam.pos.y);
		int x = p.x - BAR_W / 4, y = p.y - 70;
		int w = BAR_W / 2;
		int fill = static_cast<int>(static_cast<float>(w - 2) * std::min(1.0f, t / REVIVE_SECONDS));
		font->setFont("font_small");
		font->renderShadowed(msg->get("Reviving..."), p.x, y - font->getLineHeight() - 2, FontEngine::JUSTIFY_CENTER, NULL, 0, gold);
		render_device->drawRectangle(Point(x, y), Point(x + w - 1, y + BAR_H - 1), dark);
		for (int row = 1; row < BAR_H - 1; ++row)
			if (fill > 0) render_device->drawLine(x + 1, y + row, x + fill, y + row, fillc);
	}
}

/**
 * Co-op party skills (Power::share_radius): send the ones our hero cast,
 * and take the effects of allies' casts when our hero is within reach.
 */
void GameStatePlay::applyAllyPowers() {
	std::vector<PowerID> mine;
	mine.swap(powers->shared_casts);
	for (size_t i = 0; i < mine.size(); ++i)
		netmgr->sendAllyPower(static_cast<uint32_t>(mine[i]), pc->stats.pos.x, pc->stats.pos.y, powers->powers[mine[i]]->share_radius);

	std::vector<PowerVisualPacket> theirs = netmgr->drainAllyPowers();
	for (size_t i = 0; i < theirs.size(); ++i) {
		PowerID id = static_cast<PowerID>(theirs[i].power_id);
		if (!powers->isValid(id) || !pc->stats.alive)
			continue;
		if (Utils::calcDist(pc->stats.pos, FPoint(theirs[i].origin_x, theirs[i].origin_y)) > theirs[i].target_x)
			continue;
		powers->effect(&pc->stats, &pc->stats, id, Power::SOURCE_TYPE_HERO);
		pc->stats.refresh_stats = true;
		std::map<uint32_t, RemotePlayerVisual>::iterator who = remote_players.find(theirs[i].player_id);
		std::string name = (who != remote_players.end() && !who->second.appearance.name.empty()) ? who->second.appearance.name : msg->get("An ally");
		pc->logMsg(msg->getv("%s: %s on you", name.c_str(), powers->powers[id]->name.c_str()), Avatar::MSG_NORMAL);
		Utils::logInfo("Coop: got %s from player %u", powers->powers[id]->name.c_str(), static_cast<unsigned>(theirs[i].player_id));
	}
}

/**
 * Pause menu "Play with friends": a solo game becomes the host of an online
 * room (friends type the code in Multiplayer) and of the LAN (search / IP).
 * The same button closes it again; friends get "the host left" and go on solo.
 */
void GameStatePlay::onlineLogic() {
	MenuExit *ex = menu->exit;
	// test hook: RD_OPEN_ROOM=<seconds> clicks "Play with friends" by itself
	if (getenv("RD_OPEN_ROOM") && !(netmgr && netmgr->isActive())) {
		static int frames = 0;
		if (++frames == atoi(getenv("RD_OPEN_ROOM")) * settings->max_frames_per_sec)
			ex->online_clicked = true;
	}
	if (ex->online_clicked) {
		ex->online_clicked = false;
		if (netmgr && netmgr->isServer()) {
			netmgr->shutdown();
			room_announced.clear();
			pc->logMsg(msg->get("Your game is solo again."), Avatar::MSG_NORMAL);
		}
		else if (!netmgr || !netmgr->isActive()) {
			if (!netmgr)
				netmgr = new NetManager();
			// LAN port if free (another copy may hold it), else any port:
			// the online room doesn't care which
			if (netmgr->startServer(GameStateMultiplayer::HOST_PORT) || netmgr->startServer(0)) {
				netmgr->setPvp(settings->net_pvp);
				announced_enemy_ids.clear();
				sent_own_appearance = false;
				netmgr->sendMapChange(mapr->getFilename(), pc->stats.pos.x, pc->stats.pos.y);
				netmgr->openRoom();
				room_announced.clear();
			}
			else {
				pc->logMsg(msg->get("Could not open the game to friends."), Avatar::MSG_NORMAL);
			}
		}
	}

	std::string status, button = msg->get("Play with friends");
	bool enabled = true;
	if (!netmgr || !netmgr->isActive()) {
		status = msg->get("Solo game");
	}
	else if (netmgr->isClient()) {
		status = msg->get("In a friend's game");
		enabled = false;
	}
	else {
		button = msg->get("Close to friends");
		if (!netmgr->getRoomCode().empty())
			status = msg->getv("Room code: %s", netmgr->getRoomCode().c_str());
		else if (netmgr->isRoomRequested() && netmgr->getRoomError().empty())
			status = msg->get("Opening room...");
		// friends on the same network: search, or type this address
		if (lan_address.empty() || ++lan_address_frames > 5 * settings->max_frames_per_sec) {
			lan_address = NetManager::lanAddressText(netmgr->getServerPort());
			vpn_address = NetManager::vpnAddressText(netmgr->getServerPort());
			lan_address_frames = 0;
		}
		if (!lan_address.empty()) {
			const std::string lan = msg->getv("LAN: %s", lan_address.c_str());
			status = status.empty() ? lan : status + "  " + lan;
		}
		if (!vpn_address.empty())
			status = status.empty() ? vpn_address : status + "  " + vpn_address;
		else if (status.empty()) {
			status = msg->get("Open on LAN");
		}
	}
	ex->setOnlineStatus(status, button, enabled);

	if (netmgr && netmgr->isServer() && !lan_address.empty() && !lan_announced) {
		lan_announced = true;
		pc->logMsg(msg->getv("Open on the local network: friends use Search, or type %s", lan_address.c_str()), Avatar::MSG_UNIQUE);
	}
	if (!(netmgr && netmgr->isServer()))
		lan_announced = false;
	if (netmgr && netmgr->isServer() && !netmgr->getRoomCode().empty() && netmgr->getRoomCode() != room_announced) {
		room_announced = netmgr->getRoomCode();
		pc->logMsg(msg->getv("Room open! Friends join with the code %s", room_announced.c_str()), Avatar::MSG_UNIQUE);
	}
}

// while hosting an online room, its code stays in a corner of the screen
void GameStatePlay::renderRoomCode() {
	if (!netmgr || !netmgr->isServer() || settings->trailer_clean)
		return;
	// one line each, under the minimap (top right)
	std::vector<std::string> lines;
	if (!netmgr->getRoomCode().empty())
		lines.push_back(msg->getv("Room: %s", netmgr->getRoomCode().c_str()));
	if (!lan_address.empty())
		lines.push_back(msg->getv("LAN: %s", lan_address.c_str()));
	if (!vpn_address.empty())
		lines.push_back(vpn_address);
	for (size_t i = 0; i < lines.size() && i < 3; ++i) {
		WidgetLabel *l = room_labels[i];
		if (l->getText() != lines[i])
			l->setText(lines[i]);
		l->setPos(settings->view_w - 12, 350 + static_cast<int>(i) * 22);
		l->render();
	}
}

void GameStatePlay::renderWaveBanner() {
	if (!horde->isActive()) {
		banner_text.clear();
		return;
	}
	// clients learn the wave a moment after arriving: skip "Wave 1" until then
	if (netmgr && netmgr->isClient() && netmgr->getHostWave() == 0)
		return;
	std::string text = horde->bannerText();
	if (text != banner_text) {
		banner_text = text;
		banner_ticks = 3 * settings->max_frames_per_sec;
		banner_label->setText(text);
		// test hook: RD_BANNER_SHOT=<dir> screenshots every banner
		if (getenv("RD_BANNER_SHOT")) {
			static int n = 0;
			char name[48];
			snprintf(name, sizeof(name), "/banner_%02d.png", ++n);
			render_device->screenshot_request = std::string(getenv("RD_BANNER_SHOT")) + name;
		}
	}
	if (banner_ticks <= 0)
		return;
	banner_ticks--;
	const int fade = settings->max_frames_per_sec / 2;
	int alpha = banner_ticks < fade ? 255 * banner_ticks / std::max(1, fade) : 255;
	banner_label->setAlpha(static_cast<uint8_t>(alpha));
	banner_label->setPos(settings->view_w / 2, settings->view_h / 4);
	banner_label->render();
}

/**
 * A red flash over the screen when the hero loses more than 12% of their
 * health in one frame -- heavy hits should feel heavy.
 */
void GameStatePlay::renderHurtFlash() {
	const float hp = pc->stats.hp;
	const float hp_max = pc->stats.get(Stats::HP_MAX);
	if (hurt_last_hp >= 0 && hp_max > 0 && hurt_last_hp - hp > hp_max * 0.12f && pc->stats.alive)
		hurt_flash = settings->max_frames_per_sec / 3;
	hurt_last_hp = hp;
	if (hurt_flash <= 0)
		return;

	if (!hurt_overlay || hurt_overlay_size.x != settings->view_w || hurt_overlay_size.y != settings->view_h) {
		delete hurt_overlay;
		hurt_overlay = NULL;
		Image *img = render_device->createImage(settings->view_w, settings->view_h);
		if (!img)
			return;
		img->fillWithColor(Color(150, 10, 8, 255));
		hurt_overlay = img->createSprite();
		img->unref();
		hurt_overlay_size = Point(settings->view_w, settings->view_h);
	}
	const int full = std::max(1, static_cast<int>(settings->max_frames_per_sec) / 3);
	hurt_overlay->alpha_mod = static_cast<uint8_t>(90 * hurt_flash / full);
	hurt_overlay->setDest(0, 0);
	render_device->render(hurt_overlay);
	hurt_flash--;
}

/**
 * Trailer / screenshot mode (command line --trailer). For shooting the
 * game's page and videos: the HUD starts hidden, the hero can't die, the
 * horde is four times bigger and the screen doesn't shake.
 *   F1  show / hide the HUD
 *   F2  screenshot            -> <user folder>/trailer/shot_NNN.png
 *   F3  start / stop recording frames at 15 fps (max 20 s)
 *                              -> <user folder>/trailer/gif_NNN/frame_NNNN.bmp (half size)
 *       (make a GIF with tools/make_gif.py, or drop the frames on ezgif.com)
 *   F4  lock / unlock the camera where it is
 */
void GameStatePlay::trailerLogic() {
	if (!settings->trailer_mode)
		return;

	const std::string base = settings->path_user + "trailer";
	// test hook: RD_TRAILER_TEST presses F2 at 12s and F3 at 13s / 16s
	if (getenv("RD_TRAILER_TEST")) {
		static int f = 0;
		const int fps = static_cast<int>(settings->max_frames_per_sec);
		++f;
		if (std::string(getenv("RD_TRAILER_TEST")) == "shots") {
			// a screenshot every 4 s from 10 s on (store page material)
			if (f >= 10 * fps && f % (getenv("RD_TRAILER_EVERY") ? atoi(getenv("RD_TRAILER_EVERY")) * fps / 2 : 4 * fps) == 0) inpt->last_key = SDL_SCANCODE_F2;
		}
		else {
			if (f == 12 * fps) inpt->last_key = SDL_SCANCODE_F2;
			if (f == 13 * fps || f == 16 * fps) inpt->last_key = SDL_SCANCODE_F3;
		}
	}
	int key = inpt->last_key;
	if (key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_F2 || key == SDL_SCANCODE_F3 || key == SDL_SCANCODE_F4)
		inpt->last_key = -1;

	if (key == SDL_SCANCODE_F1)
		trailer_hud = !trailer_hud;

	if (key == SDL_SCANCODE_F2) {
		Filesystem::createDir(base);
		for (int n = 1; n < 1000; ++n) {
			char name[32];
			snprintf(name, sizeof(name), "/shot_%03d.png", n);
			if (!Filesystem::fileExists(base + name)) {
				render_device->screenshot_request = base + name;
				Utils::logInfo("Trailer: screenshot %s", (base + name).c_str());
				break;
			}
		}
	}

	if (key == SDL_SCANCODE_F3) {
		if (trailer_rec_frame >= 0) {
			Utils::logInfo("Trailer: recorded %d frames in %s", trailer_rec_frame, trailer_rec_dir.c_str());
			trailer_rec_frame = -1;
		}
		else {
			Filesystem::createDir(base);
			for (int n = 1; n < 1000; ++n) {
				char name[32];
				snprintf(name, sizeof(name), "/gif_%03d", n);
				if (!Filesystem::pathExists(base + name)) {
					trailer_rec_dir = base + name;
					Filesystem::createDir(trailer_rec_dir);
					trailer_rec_frame = 0;
					trailer_frames = 0;
					Utils::logInfo("Trailer: recording to %s", trailer_rec_dir.c_str());
					break;
				}
			}
		}
	}
	if (trailer_rec_frame >= 0) {
		const int step = std::max(1, static_cast<int>(settings->max_frames_per_sec) / 15);
		// one frame at a time: a new request would replace one not yet saved
		if (trailer_frames++ % step == 0 && render_device->screenshot_request.empty()) {
			char name[32];
			snprintf(name, sizeof(name), "/frame_%04d.bmp", trailer_rec_frame++);
			render_device->screenshot_request = trailer_rec_dir + name;
		}
		if (trailer_rec_frame >= 15 * 20) {
			Utils::logInfo("Trailer: recorded %d frames in %s (limit)", trailer_rec_frame, trailer_rec_dir.c_str());
			trailer_rec_frame = -1;
		}
	}

	if (key == SDL_SCANCODE_F4) {
		trailer_cam_lock = !trailer_cam_lock;
		trailer_cam_pos = mapr->cam.pos;
	}
	if (trailer_cam_lock)
		mapr->cam.warpTo(trailer_cam_pos);

	// the show must go on
	pc->stats.hp = pc->stats.get(Stats::HP_MAX);
	pc->stats.mp = pc->stats.get(Stats::MP_MAX);
	horde->crowd_mult = getenv("RD_TRAILER_CROWD") ? static_cast<float>(atof(getenv("RD_TRAILER_CROWD"))) : 4.0f;
	mapr->cam.shake_timer.reset(Timer::END);
}
