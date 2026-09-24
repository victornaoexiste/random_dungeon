/*
ENet-based network transport for Random Dungeon's multiplayer.

Step 1 (--net-server/--net-client, see runServerTestLoop/runClientTestLoop)
proved the ENet dependency builds, links, and that a server/client pair can
exchange packets, as a headless path outside init()/mainLoop().

Step 2 (pollGame/sendPosition/getRemotePositions, see main.cpp --net-host/
--net-join and GameStatePlay::logic()/render()) wires that same transport
into the real, graphical game and supports an arbitrary number of peers,
not just one: every connected client gets a stable player_id (its ENet
connectID), the server relays each player's position to every OTHER
connected player (star topology -- everyone only talks to the server),
and player_id 0 is reserved for the host's own hero. GameStatePlay spawns
one lightweight Entity per player_id it sees and drops it when that id
stops appearing in getRemotePositions(), reusing Entity::animsets caching
(see EntityManager::getEntityPrototype) so many concurrent players stays
cheap -- the same mechanism already used to spawn enemy hordes.

Step 3 (sendAppearance/getRemoteAppearances) sends each player's real look
-- body/head option plus per-layer equipped item graphics, the same data
GameSlotPreview uses to render the character-select screen preview -- once
on connect, over the reliable channel (1). GameStatePlay renders each
remote player with their own GameSlotPreview instead of a placeholder
monster Entity once that data arrives.

Step 4 (sendAction/drainActionEvents) sends "I just started this attack/
skill animation" as a one-shot event (channel 2, reliable) whenever the
local hero enters StatBlock::ENTITY_POWER, so remote players visibly
swing/cast/shoot instead of just sliding around. This is animation only --
no hazard/damage sync yet, so an attack a remote player throws doesn't
actually hurt anyone here.

Server-authoritative model: matches the target deployment (game runs on a
dedicated Linux box, players connect as clients).

Step 5 (sendEnemySpawn/sendEnemyState/sendEnemyDespawn, getRemoteEnemies)
makes the horde itself shared instead of per-client: only the host runs
EntityManager's normal map-enemy spawn/AI/combat (see EntityManager::
handleNewMap, gated on settings->net_join_target being empty); a net client
skips that spawn pass entirely and instead mirrors whatever the host
reports, spawning one real Entity per net_id into its own entitym->entities
(see GameStatePlay::syncRemoteEnemies) so the *existing* HazardManager
collision code can hit it completely unmodified -- these proxies just have
their position/animation/hp forced from the network every tick instead of
running EntityBehavior AI (see StatBlock::net_proxy and the check in
EntityManager::logic()).

Because a proxy loads the exact same enemy definition file as the host's
real copy (see NetManager::sendEnemySpawn's type_filename -> EntityManager::
getEntityPrototype), its damage formula, resistances, drop table etc. are
byte-for-byte identical -- only current hp differs, and that has no effect
on how much damage a hit computes, only on whether it kills. So a client's
own local hazard hits already run the engine's real Entity::takeHit() ->
StatBlock::takeDamage() unmodified, which means XP and loot already resolve
entirely locally, per player, exactly like single-player -- no network
message needed for rewards at all. This naturally gives each player their
own drop instead of one shared pickup, without any dedicated loot-sync
code.

What *does* need a network round trip is keeping the shared health bar
consistent: when a client's hazard chips a proxy's hp, sendEnemyHit reports
(net_id, damage) to the host (client -> server only, no relay), and the
host subtracts that from its own authoritative Entity so everyone's next
broadcast reflects the whole party's damage, not just the host's own hits.

Known limits of this step: quick-spawned/summoned creatures (EntityManager::
handleSpawn, e.g. a player's own summon powers) are NOT synced -- only
creatures placed on the map are. Proxies aren't blocked in the client's map
collider, so a hero can currently walk through one.
*/

#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <enet/enet.h>
#include <string>
#include <vector>
#include <stdint.h>
#include <map>
#include <cstring>

struct TickPacket {
	uint32_t player_id; // 0 = host's own hero; otherwise the sender's connectID
	uint32_t tick;
	float x;
	float y;
};

struct NetPos {
	NetPos() : x(0), y(0) {}
	NetPos(float _x, float _y) : x(_x), y(_y) {}
	float x;
	float y;
};

// Fixed-size on the wire so it can be memcpy'd like TickPacket. Field order
// matches layer_reference_order as built from engine/hero_layers.txt (see
// GameSlotPreview's constructor): main, feet, legs, hands, chest, off, head.
struct AppearancePacket {
	static const size_t NUM_LAYERS = 7;
	static const size_t FIELD_LEN = 32;

	AppearancePacket() : player_id(0) {
		memset(gfx_base, 0, sizeof(gfx_base));
		memset(gfx_head, 0, sizeof(gfx_head));
		memset(layers, 0, sizeof(layers));
	}

	uint32_t player_id;
	char gfx_base[FIELD_LEN];
	char gfx_head[FIELD_LEN];
	char layers[NUM_LAYERS][FIELD_LEN];
};

struct PlayerAppearance {
	std::string gfx_base;
	std::string gfx_head;
	std::vector<std::string> layers;
};

// A one-shot trigger, not continuous state like TickPacket: "this player
// just started animation X" (an attack_anim like "swing"/"cast"/"shoot",
// see Avatar::attack_anim). Reliable + queued (see drainActionEvents())
// so a quick attack can't get lost or overwritten before it's seen.
struct ActionPacket {
	static const size_t FIELD_LEN = 32;

	ActionPacket() : player_id(0) {
		memset(anim, 0, sizeof(anim));
	}

	uint32_t player_id;
	char anim[FIELD_LEN];
};

// A one-shot trigger like ActionPacket, sent alongside it when the power
// used has use_hazard=true: enough for the receiver to call
// PowerManager::activate() itself and get a real Hazard with the power's
// own animation/speed/lifespan, so a fireball (etc.) visibly travels for
// everyone -- not just a body animation. Receivers mark the resulting
// Hazard cosmetic_only=true so it can't deal damage or grant rewards
// twice; the real hit already happened (or will) on the sender's side.
struct PowerVisualPacket {
	PowerVisualPacket() : player_id(0), power_id(0), origin_x(0), origin_y(0), target_x(0), target_y(0) {}

	uint32_t player_id;
	uint32_t power_id; // PowerID is size_t engine-side; truncated to 32 bits on the wire, plenty for any real power table
	float origin_x;
	float origin_y;
	float target_x;
	float target_y;
};

// Sent once (reliable) when the host starts tracking a networked enemy,
// and re-sent to backfill anyone who connects later (see cached_enemy_spawns),
// same rationale as AppearancePacket. type_filename is a path like
// "enemies/human/male_bandit.txt" -- loadable as-is via
// EntityManager::getEntityPrototype() so the client's copy is identical.
struct EnemySpawnPacket {
	static const size_t FIELD_LEN = 64;

	EnemySpawnPacket() : net_id(0) {
		memset(type_filename, 0, sizeof(type_filename));
	}

	uint32_t net_id;
	char type_filename[FIELD_LEN];
};

// Sent every tick (unreliable, like TickPacket) for every networked enemy
// the host still considers alive/relevant. hp_percent is 0-100, rounded --
// exact hp doesn't matter for display and this keeps the packet tiny.
struct EnemyStatePacket {
	static const size_t FIELD_LEN = 24;

	EnemyStatePacket() : net_id(0), x(0), y(0), direction(0), hp_percent(0), alive(0) {
		memset(anim, 0, sizeof(anim));
	}

	uint32_t net_id;
	float x;
	float y;
	uint8_t direction;
	uint8_t hp_percent;
	uint8_t alive;
	char anim[FIELD_LEN];
};

// Sent (reliable) when the host stops tracking an enemy (dead & corpse
// expired, or otherwise removed) so clients don't have to wait for it to
// simply stop appearing in a state broadcast.
struct EnemyDespawnPacket {
	EnemyDespawnPacket() : net_id(0) {}
	uint32_t net_id;
};

// Client -> host only, never relayed: "my own local hit against this
// enemy computed this much damage" (see the big comment above -- the
// damage number itself is already correct, computed against an identical
// enemy definition; this just keeps the host's authoritative hp in sync
// with hits landed by players other than the host).
struct EnemyHitPacket {
	EnemyHitPacket() : net_id(0), damage(0) {}
	uint32_t net_id;
	float damage;
};

// Host -> one client only (reliable, channel 5): "an enemy hazard just hit YOUR
// hero". Carries just enough of the hazard for the client to rebuild it and run
// its own Entity::takeHit(), so avoidance/absorption/resists/effects use the
// client's real hero stats. The sender's damage rolls happen on the client.
struct PlayerHitPacket {
	static const size_t MAX_DAMAGE_TYPES = 8;

	PlayerHitPacket() : power_id(0), pos_x(0), pos_y(0), crit_chance(0), accuracy(0), num_damage(0) {
		memset(dmg_min, 0, sizeof(dmg_min));
		memset(dmg_max, 0, sizeof(dmg_max));
	}

	uint32_t power_id;
	float pos_x;
	float pos_y;
	float crit_chance;
	float accuracy;
	uint32_t num_damage;
	float dmg_min[MAX_DAMAGE_TYPES];
	float dmg_max[MAX_DAMAGE_TYPES];
};

struct RemoteEnemyState {
	RemoteEnemyState() : hp_percent(100), alive(1), x(0), y(0), direction(0) {}
	std::string type_filename;
	std::string anim;
	uint8_t hp_percent;
	uint8_t alive;
	float x;
	float y;
	uint8_t direction;
};

class NetManager {
public:
	enum Role {
		ROLE_NONE,
		ROLE_SERVER,
		ROLE_CLIENT
	};

	NetManager();
	~NetManager();

	bool startServer(uint16_t port);
	bool connectToServer(const std::string& host_str, uint16_t port, uint32_t timeout_ms);
	void shutdown();

	bool isActive() const {
		return role != ROLE_NONE;
	}
	bool isServer() const {
		return role == ROLE_SERVER;
	}

	// Blocking test loops. Ctrl+C to stop. Headless smoke test only
	// (main.cpp --net-server/--net-client), unrelated to real gameplay.
	void runServerTestLoop();
	void runClientTestLoop();

	// Real gameplay path: called once per GameStatePlay logic tick.
	// Non-blocking; pumps ENet events, relays positions (server), and
	// updates remote_positions.
	void pollGame();
	// Sends our own hero's position to the peer(s). Server broadcasts to
	// all connected clients (tagged player_id=0); client sends to the
	// server, which re-tags it with that client's player_id before relaying.
	void sendPosition(float x, float y);
	// Last known position of every OTHER connected player, keyed by
	// player_id. Entries disappear once that player disconnects.
	const std::map<uint32_t, NetPos>& getRemotePositions() const {
		return remote_positions;
	}

	// Sends our own look (body/head option + per-layer equipped item
	// graphics) reliably. Same relay/tagging rules as sendPosition().
	void sendAppearance(const std::string& gfx_base, const std::string& gfx_head, const std::vector<std::string>& layers);
	// Every OTHER connected player's look we've received so far, keyed by
	// player_id. An id can be missing for a bit right after it appears in
	// getRemotePositions() -- appearance arrives separately, once.
	const std::map<uint32_t, PlayerAppearance>& getRemoteAppearances() const {
		return remote_appearances;
	}

	// Sends "I just started this attack/skill animation" (Avatar::attack_anim,
	// e.g. "swing"/"cast"/"shoot"). Fire-and-forget per use, not per tick.
	void sendAction(const std::string& anim_name);
	// Every action event received since the last call, in order, then
	// clears them. Each is (player_id, anim_name); a player_id can repeat
	// if they attacked more than once between polls.
	std::vector<std::pair<uint32_t, std::string> > drainActionEvents();

	// Sends "I just used this hazard-creating power, aimed here" (see
	// PowerVisualPacket). Same relay/tagging/queue rules as sendAction().
	void sendPowerVisual(uint32_t power_id, float origin_x, float origin_y, float target_x, float target_y);
	std::vector<PowerVisualPacket> drainPowerVisualEvents();

	// Host only. Registers a new networked enemy (once, reliable) and
	// caches it so a peer connecting later still gets it.
	void sendEnemySpawn(uint32_t net_id, const std::string& type_filename);
	// Host only. Per-tick position/anim/hp snapshot for one networked enemy.
	void sendEnemyState(uint32_t net_id, float x, float y, uint8_t direction, uint8_t hp_percent, bool alive, const std::string& anim);
	// Host only. Tells clients to stop tracking net_id.
	void sendEnemyDespawn(uint32_t net_id);
	// Every enemy the host currently wants us to render, keyed by net_id.
	// Client-side only (empty on the host, which uses its own real Entities).
	const std::map<uint32_t, RemoteEnemyState>& getRemoteEnemies() const {
		return remote_enemies;
	}
	// Client only, never relayed: report damage our own hazard just dealt
	// to a networked enemy, so the host's authoritative hp stays in sync.
	void sendEnemyHit(uint32_t net_id, float damage);
	// Host only. Every (net_id, damage) reported since the last call, then
	// clears them.
	std::vector<std::pair<uint32_t, float> > drainEnemyHitEvents();

	// Host only: tell one client an enemy hazard hit its hero.
	void sendPlayerHit(uint32_t target_player_id, const PlayerHitPacket& pkt);
	// Client only: every PlayerHitPacket received since the last call, then clears them.
	std::vector<PlayerHitPacket> drainPlayerHitEvents();

private:
	std::vector<PlayerHitPacket> pending_player_hits;
	Role role;
	ENetHost *host;
	ENetPeer *server_peer;

	// Server only: connectID -> peer, so we can relay to "everyone but the sender".
	std::map<uint32_t, ENetPeer*> server_peers;
	std::map<uint32_t, NetPos> remote_positions;
	std::map<uint32_t, PlayerAppearance> remote_appearances;

	// Server only: last appearance packet seen per player_id (0 = host's
	// own). Appearance is only ever *sent* once per connection, so a peer
	// that connects after someone else already sent theirs would otherwise
	// never see them -- this is what a new CONNECT backfills from.
	std::map<uint32_t, AppearancePacket> cached_appearances;

	std::vector<std::pair<uint32_t, std::string> > pending_actions;
	std::vector<PowerVisualPacket> pending_power_visuals;

	void relayPowerVisual(uint32_t sender_id, const PowerVisualPacket& pkt);

	// Server only: every networked enemy spawned so far, for backfilling
	// a peer that connects late (same idea as cached_appearances).
	std::map<uint32_t, EnemySpawnPacket> cached_enemy_spawns;
	// Client only: latest known state of every networked enemy.
	std::map<uint32_t, RemoteEnemyState> remote_enemies;
	// Server only: damage reports awaiting HazardManager::hitEntity-equivalent application.
	std::vector<std::pair<uint32_t, float> > pending_enemy_hits;

	void relayAppearance(uint32_t sender_id, const AppearancePacket& pkt);
	void relayAction(uint32_t sender_id, const ActionPacket& pkt);
};

#endif
