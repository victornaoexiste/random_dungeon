#include "NetManager.h"

#include "FileParser.h"
#include "Utils.h"
#include "UtilsParsing.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace {
	void sleepMs(unsigned int ms) {
#ifdef _WIN32
		Sleep(ms);
#else
		usleep(ms * 1000);
#endif
	}

	void copyToField(char *dst, size_t dst_len, const std::string& src) {
		size_t n = src.size();
		if (n >= dst_len)
			n = dst_len - 1;
		memcpy(dst, src.data(), n);
		dst[n] = '\0';
	}

	std::string fieldToString(const char *src, size_t len) {
		return std::string(src, strnlen(src, len));
	}

	// LAN discovery wire format (plain UDP on DISCOVERY_PORT, not ENet)
	const char DISCOVERY_QUERY[4] = {'R', 'D', 'Q', '1'};
	struct DiscoveryReply {
		char magic[4]; // "RDH1"
		uint32_t protocol;
		uint16_t port;
		uint16_t players;
		char name[32];
		char map[64];
	};

	// ENet peer timeouts: notice a vanished peer in seconds, not ~30s
	void setPeerTimeouts(ENetPeer *peer) {
		enet_peer_timeout(peer, 0, 4000, 10000);
	}

	const uint16_t MAX_ENEMY_STATES_PER_PACKET = 28; // 1 + 2 + 28 * 44 bytes, under a typical MTU
}

ENetPacket* NetManager::makePacket(uint8_t type, const void *data, size_t len, uint32_t flags) {
	ENetPacket *packet = enet_packet_create(NULL, len + 1, flags);
	packet->data[0] = type;
	memcpy(packet->data + 1, data, len);
	return packet;
}

bool NetManager::isMsg(const ENetPacket *packet, uint8_t type, size_t len) {
	return packet->dataLength == len + 1 && packet->data[0] == type;
}

void NetManager::pushSnap(std::deque<NetSnap>& hist, float x, float y) {
	NetSnap s;
	s.t = enet_time_get();
	s.x = x;
	s.y = y;
	hist.push_back(s);
	while (hist.size() > 24)
		hist.pop_front();
}

/**
 * Position INTERP_DELAY_MS in the past, linearly interpolated between the two
 * samples around that moment. Holds the newest sample if we ran out (no
 * guessing ahead), and jumps straight to the later sample across a big gap
 * (a teleport shouldn't be drawn as a slide across the map).
 */
bool NetManager::sampleHistory(const std::deque<NetSnap>& hist, float& x, float& y) {
	if (hist.empty())
		return false;

	const uint32_t now = enet_time_get();
	const uint32_t render_t = now - INTERP_DELAY_MS;

	const NetSnap& newest = hist.back();
	if (static_cast<int32_t>(render_t - newest.t) >= 0 || hist.size() == 1) {
		x = newest.x;
		y = newest.y;
		return true;
	}
	if (static_cast<int32_t>(render_t - hist.front().t) <= 0) {
		x = hist.front().x;
		y = hist.front().y;
		return true;
	}

	for (size_t i = hist.size() - 1; i > 0; --i) {
		const NetSnap& a = hist[i - 1];
		const NetSnap& b = hist[i];
		if (static_cast<int32_t>(render_t - a.t) >= 0) {
			float dx = b.x - a.x;
			float dy = b.y - a.y;
			if (dx * dx + dy * dy > 16.0f || b.t == a.t) {
				x = b.x;
				y = b.y;
				return true;
			}
			float f = static_cast<float>(render_t - a.t) / static_cast<float>(b.t - a.t);
			x = a.x + dx * f;
			y = a.y + dy * f;
			return true;
		}
	}
	x = newest.x;
	y = newest.y;
	return true;
}

bool NetManager::samplePlayerPos(uint32_t player_id, float& x, float& y) const {
	std::map<uint32_t, std::deque<NetSnap> >::const_iterator it = player_history.find(player_id);
	return it != player_history.end() && sampleHistory(it->second, x, y);
}

bool NetManager::sampleEnemyPos(uint32_t net_id, float& x, float& y) const {
	std::map<uint32_t, std::deque<NetSnap> >::const_iterator it = enemy_history.find(net_id);
	return it != enemy_history.end() && sampleHistory(it->second, x, y);
}

bool NetManager::takeLostConnection() {
	if (!lost_connection)
		return false;
	lost_connection = false;
	return true;
}

void NetManager::setLocalInfo(const std::string& name, const std::string& map) {
	local_name = name;
	local_map = map;
}

NetManager::NetManager()
	: role(ROLE_NONE)
	, pvp(false)
	, lost_connection(false)
	, discovery_socket(ENET_SOCKET_NULL)
	, server_port(0)
	, host(NULL)
	, server_peer(NULL)
	, has_host_map(false)
	, map_change_pending(false)
{
	room_requested = false;
	room_last_send = 0;
	room_request_start = 0;
	relay_addr.host = 0;
	relay_addr.port = 0;
}

NetManager::~NetManager() {
	shutdown();
}

bool NetManager::startServer(uint16_t port) {
	if (enet_initialize() != 0) {
		Utils::logError("NetManager: could not initialize ENet");
		return false;
	}

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	// 32 max peers, 6 channels (0=position, 1=appearance, 2=action events,
	// 3=enemy spawn/despawn, 4=enemy state, 5=enemy hit reports), no bandwidth limit
	host = enet_host_create(&address, 32, 6, 0, 0);
	if (!host) {
		Utils::logError("NetManager: could not create ENet server host on port %u", static_cast<unsigned>(port));
		enet_deinitialize();
		return false;
	}

	role = ROLE_SERVER;
	Utils::logInfo("NetManager: server listening on port %u (protocol %u)", static_cast<unsigned>(port), static_cast<unsigned>(PROTOCOL_VERSION));

	// LAN discovery responder; optional (e.g. a second host on this machine
	// can't bind it -- it just won't show up in searches)
	discovery_socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (discovery_socket != ENET_SOCKET_NULL) {
		ENetAddress daddr;
		daddr.host = ENET_HOST_ANY;
		daddr.port = DISCOVERY_PORT;
		enet_socket_set_option(discovery_socket, ENET_SOCKOPT_NONBLOCK, 1);
		enet_socket_set_option(discovery_socket, ENET_SOCKOPT_REUSEADDR, 1);
		if (enet_socket_bind(discovery_socket, &daddr) != 0) {
			Utils::logInfo("NetManager: LAN discovery port %u busy, this host won't be listed", static_cast<unsigned>(DISCOVERY_PORT));
			enet_socket_destroy(discovery_socket);
			discovery_socket = ENET_SOCKET_NULL;
		}
	}
	server_port = port;
	if (server_port == 0) {
		// asked for any free port: find out which one we got (LAN discovery
		// tells friends this port)
		ENetAddress bound;
		if (enet_socket_get_address(host->socket, &bound) == 0)
			server_port = bound.port;
		Utils::logInfo("NetManager: server listening on port %u instead", static_cast<unsigned>(server_port));
	}
	return true;
}

bool NetManager::connectToServer(const std::string& host_str, uint16_t port, uint32_t timeout_ms) {
	if (enet_initialize() != 0) {
		Utils::logError("NetManager: could not initialize ENet");
		return false;
	}

	host = enet_host_create(NULL, 1, 6, 0, 0);
	if (!host) {
		Utils::logError("NetManager: could not create ENet client host");
		enet_deinitialize();
		return false;
	}

	ENetAddress address;
	if (enet_address_set_host(&address, host_str.c_str()) != 0) {
		Utils::logError("NetManager: could not resolve host '%s'", host_str.c_str());
		last_error = "resolve";
		enet_host_destroy(host);
		host = NULL;
		enet_deinitialize();
		return false;
	}
	address.port = port;

	char label[128];
	snprintf(label, sizeof(label), "%s:%u", host_str.c_str(), static_cast<unsigned>(port));
	return connectAddress(address, label, timeout_ms);
}

bool NetManager::connectAddress(const ENetAddress& address, const std::string& label, uint32_t timeout_ms) {
	const char *host_label = label.c_str();
	last_error.clear();
	lost_connection = false;
	server_peer = enet_host_connect(host, &address, 6, PROTOCOL_VERSION);
	if (!server_peer) {
		Utils::logError("NetManager: no available peers for connection attempt");
		enet_host_destroy(host);
		host = NULL;
		enet_deinitialize();
		return false;
	}

	ENetEvent event;
	if (enet_host_service(host, &event, timeout_ms) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
		// a host running another protocol version accepts the connection and
		// then drops it right away with DISCONNECT_VERSION: give it a moment.
		// Anything else arriving meanwhile (the host's backfill: appearance,
		// map...) is kept for pollGame(), not lost.
		const uint32_t wait_start = enet_time_get();
		while (enet_time_get() - wait_start < 300) {
			if (enet_host_service(host, &event, 20) <= 0)
				continue;
			if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
				last_error = (event.data == DISCONNECT_VERSION) ? "version" : "refused";
				Utils::logError("NetManager: %s refused the connection (%s)", host_label, last_error.c_str());
				for (size_t i = 0; i < early_events.size(); ++i)
					enet_packet_destroy(early_events[i].packet);
				early_events.clear();
				server_peer = NULL;
				enet_host_destroy(host);
				host = NULL;
				enet_deinitialize();
				return false;
			}
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
				early_events.push_back(event);
		}
		setPeerTimeouts(server_peer);
		role = ROLE_CLIENT;
		Utils::logInfo("NetManager: connected to %s (protocol %u)", host_label, static_cast<unsigned>(PROTOCOL_VERSION));
		return true;
	}

	enet_peer_reset(server_peer);
	server_peer = NULL;
	enet_host_destroy(host);
	host = NULL;
	enet_deinitialize();
	last_error = "timeout";
	Utils::logError("NetManager: connection to %s failed/timed out", host_label);
	return false;
}

void NetManager::shutdown() {
	closeRoom();
	if (relay_owner == this)
		relay_owner = NULL;
	if (server_peer) {
		enet_peer_disconnect_now(server_peer, DISCONNECT_NORMAL);
		server_peer = NULL;
	}
	if (host && role == ROLE_SERVER) {
		// tell clients right away instead of letting them time out
		for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it)
			enet_peer_disconnect_now(it->second, DISCONNECT_HOST_LEFT);
	}
	if (discovery_socket != ENET_SOCKET_NULL) {
		enet_socket_destroy(discovery_socket);
		discovery_socket = ENET_SOCKET_NULL;
	}
	if (host) {
		enet_host_destroy(host);
		host = NULL;
	}
	if (role != ROLE_NONE) {
		enet_deinitialize();
	}
	role = ROLE_NONE;
	pvp = false;
	has_host_map = false;
	map_change_pending = false;
	host_map.clear();
	pending_player_hits.clear();
	server_peers.clear();
	remote_positions.clear();
	remote_appearances.clear();
	cached_appearances.clear();
	pending_actions.clear();
	pending_power_visuals.clear();
	cached_enemy_spawns.clear();
	remote_enemies.clear();
	pending_enemy_hits.clear();
	player_history.clear();
	enemy_history.clear();
	enemy_state_queue.clear();
	for (size_t i = 0; i < early_events.size(); ++i)
		enet_packet_destroy(early_events[i].packet);
	early_events.clear();
}

void NetManager::runServerTestLoop() {
	uint32_t tick = 0;
	ENetEvent event;

	Utils::logInfo("NetManager: server test loop running (Ctrl+C to stop)");

	while (true) {
		while (enet_host_service(host, &event, 0) > 0) {
			switch (event.type) {
				case ENET_EVENT_TYPE_CONNECT:
					Utils::logInfo("NetManager: client connected (peer port %u)", static_cast<unsigned>(event.peer->address.port));
					break;
				case ENET_EVENT_TYPE_RECEIVE:
					Utils::logInfo("NetManager: received %u bytes from client", static_cast<unsigned>(event.packet->dataLength));
					enet_packet_destroy(event.packet);
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					Utils::logInfo("NetManager: client disconnected");
					break;
				default:
					break;
			}
		}

		// Fake authoritative "entity" walking back and forth, standing in
		// for a real StatBlock position until this is wired into GameStatePlay.
		TickPacket pkt;
		pkt.player_id = 0;
		pkt.tick = tick;
		pkt.x = 100.0f + 50.0f * static_cast<float>(tick % 100) / 100.0f;
		pkt.y = 100.0f;

		ENetPacket *packet = makePacket(MSG_TICK, &pkt, sizeof(pkt), 0); // unreliable, matches real movement sync later
		enet_host_broadcast(host, 0, packet);

		if (tick % 20 == 0) {
			Utils::logInfo("NetManager: server tick=%u pos=(%.1f,%.1f)", static_cast<unsigned>(tick), static_cast<double>(pkt.x), static_cast<double>(pkt.y));
		}

		tick++;
		enet_host_flush(host);

		sleepMs(50); // ~20 ticks/sec
	}
}

void NetManager::runClientTestLoop() {
	Utils::logInfo("NetManager: client test loop running (Ctrl+C to stop)");

	ENetEvent event;
	unsigned int received = 0;

	while (true) {
		while (enet_host_service(host, &event, 100) > 0) {
			switch (event.type) {
				case ENET_EVENT_TYPE_RECEIVE: {
					if (isMsg(event.packet, MSG_TICK, sizeof(TickPacket))) {
						TickPacket pkt;
						memcpy(&pkt, event.packet->data + 1, sizeof(TickPacket));
						received++;
						if (received % 20 == 0) {
							Utils::logInfo("NetManager: client received tick=%u pos=(%.1f,%.1f)", static_cast<unsigned>(pkt.tick), static_cast<double>(pkt.x), static_cast<double>(pkt.y));
						}
					}
					enet_packet_destroy(event.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
					Utils::logInfo("NetManager: disconnected from server");
					return;
				default:
					break;
			}
		}
	}
}

void NetManager::pollGame() {
	if (!host)
		return;

	if (role == ROLE_SERVER) {
		pollDiscovery();
		pollRelay();
	}

	ENetEvent event;
	while (host) {
		if (!early_events.empty()) {
			event = early_events.front();
			early_events.pop_front();
		}
		else if (enet_host_service(host, &event, 0) <= 0) {
			break;
		}
		switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT:
				if (role == ROLE_SERVER) {
					if (event.data != PROTOCOL_VERSION) {
						Utils::logInfo("NetManager: refused a player with protocol %u (ours is %u)", static_cast<unsigned>(event.data), static_cast<unsigned>(PROTOCOL_VERSION));
						enet_peer_disconnect_later(event.peer, DISCONNECT_VERSION);
						break;
					}
					setPeerTimeouts(event.peer);
					server_peers[event.peer->connectID] = event.peer;
					Utils::logInfo("NetManager: player %u connected (%u total)", static_cast<unsigned>(event.peer->connectID), static_cast<unsigned>(server_peers.size()));

					// Backfill: everyone's appearance sent so far (including
					// our own, cached under id 0) -- appearance is only ever
					// sent once per connection, so without this a peer that
					// joins after someone else already sent theirs would
					// never see them.
					for (std::map<uint32_t, AppearancePacket>::iterator it = cached_appearances.begin(); it != cached_appearances.end(); ++it) {
						ENetPacket *out = makePacket(MSG_APPEARANCE, &it->second, sizeof(AppearancePacket), ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(event.peer, 1, out);
					}

					// Backfill: the map the party is on, so the new client follows us there.
					if (has_host_map) {
						ENetPacket *out = makePacket(MSG_MAP, &host_map_packet, sizeof(MapPacket), ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(event.peer, 1, out);
					}

					// Backfill: every networked enemy spawned so far, same reasoning.
					for (std::map<uint32_t, EnemySpawnPacket>::iterator it = cached_enemy_spawns.begin(); it != cached_enemy_spawns.end(); ++it) {
						ENetPacket *out = makePacket(MSG_ENEMY_SPAWN, &it->second, sizeof(EnemySpawnPacket), ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(event.peer, 3, out);
					}
				}
				else {
					Utils::logInfo("NetManager: peer connected");
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (isMsg(event.packet, MSG_TICK, sizeof(TickPacket))) {
					TickPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(TickPacket));

					if (role == ROLE_SERVER) {
						// The sender is identified by the ENet connection itself,
						// not by whatever it put in pkt.player_id.
						uint32_t sender_id = event.peer->connectID;
						remote_positions[sender_id] = NetPos(pkt);
						pushSnap(player_history[sender_id], pkt.x, pkt.y);

						// Relay to everyone else, tagged with the true sender id.
						TickPacket relay = pkt;
						relay.player_id = sender_id;
						for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
							if (it->first == sender_id)
								continue;
							// UNSEQUENCED, not plain unreliable: with 2+ senders relayed
							// on the same channel, ENet's per-channel unreliable
							// sequencing can silently drop one sender's packet because
							// it looks "older" relative to a different sender's more
							// recently-processed one -- they're unrelated streams, not
							// a single ordered one. Cost the same, since we already
							// don't care about ordering, just about not losing updates
							// to unrelated cross-talk.
							ENetPacket *out = makePacket(MSG_TICK, &relay, sizeof(relay), ENET_PACKET_FLAG_UNSEQUENCED);
							enet_peer_send(it->second, 0, out);
						}
					}
					else {
						remote_positions[pkt.player_id] = NetPos(pkt);
						pushSnap(player_history[pkt.player_id], pkt.x, pkt.y);
					}
				}
				else if (isMsg(event.packet, MSG_APPEARANCE, sizeof(AppearancePacket))) {
					AppearancePacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(AppearancePacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;

					PlayerAppearance app;
					app.name = fieldToString(pkt.name, AppearancePacket::FIELD_LEN);
					app.color_skin = fieldToString(pkt.colors[0], sizeof(pkt.colors[0]));
					app.color_hair = fieldToString(pkt.colors[1], sizeof(pkt.colors[1]));
					app.color_cloth = fieldToString(pkt.colors[2], sizeof(pkt.colors[2]));
					app.gfx_base.assign(pkt.gfx_base, strnlen(pkt.gfx_base, AppearancePacket::FIELD_LEN));
					app.gfx_head.assign(pkt.gfx_head, strnlen(pkt.gfx_head, AppearancePacket::FIELD_LEN));
					for (size_t i = 0; i < AppearancePacket::NUM_LAYERS; i++) {
						app.layers.push_back(std::string(pkt.layers[i], strnlen(pkt.layers[i], AppearancePacket::FIELD_LEN)));
					}
					remote_appearances[owner_id] = app;
					Utils::logInfo("NetManager: received appearance for player %u '%s' (gfx_base=%s)", static_cast<unsigned>(owner_id), app.name.c_str(), app.gfx_base.c_str());

					if (role == ROLE_SERVER) {
						AppearancePacket cached = pkt;
						cached.player_id = owner_id;
						cached_appearances[owner_id] = cached;
						relayAppearance(owner_id, pkt);
					}
				}
				else if (isMsg(event.packet, MSG_ACTION, sizeof(ActionPacket))) {
					ActionPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(ActionPacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;
					std::string anim_name(pkt.anim, strnlen(pkt.anim, ActionPacket::FIELD_LEN));
					Utils::logInfo("NetManager: player %u action '%s'", static_cast<unsigned>(owner_id), anim_name.c_str());
					pending_actions.push_back(std::make_pair(owner_id, anim_name));

					if (role == ROLE_SERVER) {
						relayAction(owner_id, pkt);
					}
				}
				else if (isMsg(event.packet, MSG_POWER_VISUAL, sizeof(PowerVisualPacket))) {
					PowerVisualPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(PowerVisualPacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;
					pkt.player_id = owner_id;
					Utils::logInfo("NetManager: player %u power_id=%u visual (%.1f,%.1f)->(%.1f,%.1f)", static_cast<unsigned>(owner_id), static_cast<unsigned>(pkt.power_id), static_cast<double>(pkt.origin_x), static_cast<double>(pkt.origin_y), static_cast<double>(pkt.target_x), static_cast<double>(pkt.target_y));
					pending_power_visuals.push_back(pkt);

					if (role == ROLE_SERVER) {
						relayPowerVisual(owner_id, pkt);
					}
				}
				else if (isMsg(event.packet, MSG_ALLY_POWER, sizeof(PowerVisualPacket))) {
					PowerVisualPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(PowerVisualPacket));
					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;
					pkt.player_id = owner_id;
					pending_ally_powers.push_back(pkt);
					if (role == ROLE_SERVER) {
						PowerVisualPacket relay = pkt;
						for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
							if (it->first == owner_id)
								continue;
							enet_peer_send(it->second, 2, makePacket(MSG_ALLY_POWER, &relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE));
						}
					}
				}
				else if (isMsg(event.packet, MSG_ENEMY_SPAWN, sizeof(EnemySpawnPacket))) {
					// Host -> client only; a client never sends this.
					if (role == ROLE_CLIENT) {
						EnemySpawnPacket pkt;
						memcpy(&pkt, event.packet->data + 1, sizeof(EnemySpawnPacket));
						RemoteEnemyState& re = remote_enemies[pkt.net_id];
						re.type_filename.assign(pkt.type_filename, strnlen(pkt.type_filename, EnemySpawnPacket::FIELD_LEN));
						Utils::logInfo("NetManager: enemy net_id=%u spawned (%s)", static_cast<unsigned>(pkt.net_id), re.type_filename.c_str());
					}
				}
				else if (event.packet->dataLength >= 3 && event.packet->data[0] == MSG_ENEMY_STATES) {
					// Host -> client only: uint16 count, then count states.
					uint16_t count = 0;
					memcpy(&count, event.packet->data + 1, sizeof(count));
					if (role == ROLE_CLIENT && event.packet->dataLength == 3 + count * sizeof(EnemyStatePacket)) {
						for (uint16_t n = 0; n < count; ++n) {
							EnemyStatePacket pkt;
							memcpy(&pkt, event.packet->data + 3 + n * sizeof(EnemyStatePacket), sizeof(EnemyStatePacket));
							std::map<uint32_t, RemoteEnemyState>::iterator found = remote_enemies.find(pkt.net_id);
							if (found != remote_enemies.end()) {
								RemoteEnemyState& re = found->second;
								re.x = pkt.x;
								re.y = pkt.y;
								re.direction = pkt.direction;
								re.hp_percent = pkt.hp_percent;
								re.alive = pkt.alive;
								re.anim.assign(pkt.anim, strnlen(pkt.anim, EnemyStatePacket::FIELD_LEN));
								pushSnap(enemy_history[pkt.net_id], pkt.x, pkt.y);
							}
						}
					}
				}
				else if (isMsg(event.packet, MSG_ENEMY_DESPAWN, sizeof(EnemyDespawnPacket))) {
					// Host -> client only.
					if (role == ROLE_CLIENT) {
						EnemyDespawnPacket pkt;
						memcpy(&pkt, event.packet->data + 1, sizeof(EnemyDespawnPacket));
						Utils::logInfo("NetManager: enemy net_id=%u despawned", static_cast<unsigned>(pkt.net_id));
						remote_enemies.erase(pkt.net_id);
						enemy_history.erase(pkt.net_id);
					}
				}
				else if (isMsg(event.packet, MSG_ENEMY_HIT, sizeof(EnemyHitPacket))) {
					// Client -> host only, never relayed.
					if (role == ROLE_SERVER) {
						EnemyHitPacket pkt;
						memcpy(&pkt, event.packet->data + 1, sizeof(EnemyHitPacket));
						Utils::logInfo("NetManager: received hit report net_id=%u damage=%.1f", static_cast<unsigned>(pkt.net_id), static_cast<double>(pkt.damage));
						pending_enemy_hits.push_back(std::make_pair(pkt.net_id, pkt.damage));
					}
				}
				else if (isMsg(event.packet, MSG_MAP, sizeof(MapPacket))) {
					// Host -> client only.
					MapPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(MapPacket));
					if (role == ROLE_CLIENT && pkt.magic == 0x524d4150) {
						pvp = (pkt.pvp != 0);
						// same map + spot = only the rules changed (see setPvp), not a map change
						std::string new_map(pkt.map, strnlen(pkt.map, MapPacket::FIELD_LEN));
						bool rules_only = has_host_map && new_map == host_map && pkt.x == host_map_packet.x && pkt.y == host_map_packet.y;
						host_map_packet = pkt;
						host_map = new_map;
						has_host_map = true;
						if (!rules_only) {
							map_change_pending = true;
							// the host left the old map: its enemies there are gone
							remote_enemies.clear();
							enemy_history.clear();
						}
						Utils::logInfo("NetManager: host is on map %s (%.1f,%.1f), pvp=%d", host_map.c_str(), static_cast<double>(pkt.x), static_cast<double>(pkt.y), pvp ? 1 : 0);
					}
				}
				else if (isMsg(event.packet, MSG_PLAYER_HIT, sizeof(PlayerHitPacket))) {
					PlayerHitPacket pkt;
					memcpy(&pkt, event.packet->data + 1, sizeof(PlayerHitPacket));
					if (role == ROLE_CLIENT) {
						Utils::logInfo("NetManager: received player hit power_id=%u", static_cast<unsigned>(pkt.power_id));
						pending_player_hits.push_back(pkt);
					}
					else if (pvp) {
						// a client's PvP hit: ours to apply, or relay it to the victim
						uint32_t attacker_id = event.peer->connectID;
						if (pkt.target_player_id == attacker_id) {
							// can't hit yourself
						}
						else if (pkt.target_player_id == 0) {
							Utils::logInfo("NetManager: player %u hit the host (pvp) power_id=%u", static_cast<unsigned>(attacker_id), static_cast<unsigned>(pkt.power_id));
							pending_player_hits.push_back(pkt);
						}
						else {
							Utils::logInfo("NetManager: player %u hit player %u (pvp) power_id=%u", static_cast<unsigned>(attacker_id), static_cast<unsigned>(pkt.target_player_id), static_cast<unsigned>(pkt.power_id));
							sendPlayerHit(pkt.target_player_id, pkt);
						}
					}
				}
				else {
					Utils::logError("NetManager: unknown message type %u (%u bytes)", event.packet->dataLength ? static_cast<unsigned>(event.packet->data[0]) : 0u, static_cast<unsigned>(event.packet->dataLength));
				}
				enet_packet_destroy(event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				if (role == ROLE_SERVER) {
					uint32_t gone_id = event.peer->connectID;
					server_peers.erase(gone_id);
					remote_positions.erase(gone_id);
					remote_appearances.erase(gone_id);
					cached_appearances.erase(gone_id);
					player_history.erase(gone_id);
					Utils::logInfo("NetManager: player %u disconnected (%u total)", static_cast<unsigned>(gone_id), static_cast<unsigned>(server_peers.size()));
				}
				else {
					Utils::logInfo("NetManager: disconnected from server (reason %u)", static_cast<unsigned>(event.data));
					last_error = (event.data == DISCONNECT_HOST_LEFT) ? "host_left" : "lost";
					lost_connection = true;
					// the server peer is gone: drop the whole session, we're
					// back to playing alone (see GameStatePlay::checkLostConnection)
					server_peer = NULL;
					std::string err = last_error;
					shutdown();
					last_error = err;
					lost_connection = true;
					return;
				}
				break;
			default:
				break;
		}
	}
}

void NetManager::sendPosition(float x, float y, float hp, float hp_max, bool alive, int wave, int theme, int family) {
	if (!host)
		return;

	TickPacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	pkt.tick = 0;
	pkt.x = x;
	pkt.y = y;
	pkt.hp = hp;
	pkt.hp_max = hp_max;
	pkt.alive = alive ? 1 : 0;
	pkt.wave = static_cast<uint8_t>(std::max(0, std::min(255, wave)));
	pkt.theme = static_cast<uint8_t>(std::max(0, std::min(255, theme)));
	pkt.family = static_cast<uint8_t>(std::max(0, std::min(255, family)));

	// UNSEQUENCED (see the relay comment above for why plain unreliable is
	// the wrong choice once more than one sender shares a channel).
	ENetPacket *packet = makePacket(MSG_TICK, &pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);

	if (role == ROLE_SERVER) {
		enet_host_broadcast(host, 0, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		enet_peer_send(server_peer, 0, packet);
	}
	else {
		enet_packet_destroy(packet);
	}
}

void NetManager::sendAppearance(const std::string& name, const std::string& gfx_base, const std::string& gfx_head, const std::vector<std::string>& layers, const std::string colors[3]) {
	if (!host)
		return;

	AppearancePacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	copyToField(pkt.name, AppearancePacket::FIELD_LEN, name);
	for (int k = 0; k < 3; ++k)
		copyToField(pkt.colors[k], sizeof(pkt.colors[k]), colors[k]);
	copyToField(pkt.gfx_base, AppearancePacket::FIELD_LEN, gfx_base);
	copyToField(pkt.gfx_head, AppearancePacket::FIELD_LEN, gfx_head);
	for (size_t i = 0; i < AppearancePacket::NUM_LAYERS; i++) {
		if (i < layers.size())
			copyToField(pkt.layers[i], AppearancePacket::FIELD_LEN, layers[i]);
	}

	Utils::logInfo("NetManager: sending our appearance ('%s', %s)", name.c_str(), gfx_base.c_str());
	if (role == ROLE_SERVER) {
		cached_appearances[0] = pkt; // so peers who join later still get it, see ENET_EVENT_TYPE_CONNECT
		ENetPacket *packet = makePacket(MSG_APPEARANCE, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 1, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = makePacket(MSG_APPEARANCE, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 1, packet);
	}
}

void NetManager::relayAppearance(uint32_t sender_id, const AppearancePacket& pkt) {
	AppearancePacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = makePacket(MSG_APPEARANCE, &relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(it->second, 1, out);
	}
}

void NetManager::sendAction(const std::string& anim_name) {
	if (!host)
		return;

	ActionPacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	copyToField(pkt.anim, ActionPacket::FIELD_LEN, anim_name);

	if (role == ROLE_SERVER) {
		ENetPacket *packet = makePacket(MSG_ACTION, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 2, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = makePacket(MSG_ACTION, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 2, packet);
	}
}

void NetManager::relayAction(uint32_t sender_id, const ActionPacket& pkt) {
	ActionPacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = makePacket(MSG_ACTION, &relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(it->second, 2, out);
	}
}

void NetManager::sendPowerVisual(uint32_t power_id, float origin_x, float origin_y, float target_x, float target_y) {
	if (!host)
		return;

	PowerVisualPacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	pkt.power_id = power_id;
	pkt.origin_x = origin_x;
	pkt.origin_y = origin_y;
	pkt.target_x = target_x;
	pkt.target_y = target_y;

	if (role == ROLE_SERVER) {
		ENetPacket *packet = makePacket(MSG_POWER_VISUAL, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 2, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = makePacket(MSG_POWER_VISUAL, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 2, packet);
	}
}

void NetManager::relayPowerVisual(uint32_t sender_id, const PowerVisualPacket& pkt) {
	PowerVisualPacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = makePacket(MSG_POWER_VISUAL, &relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(it->second, 2, out);
	}
}

void NetManager::sendAllyPower(uint32_t power_id, float x, float y, float radius) {
	if (!host)
		return;
	PowerVisualPacket pkt;
	pkt.player_id = 0;
	pkt.power_id = power_id;
	pkt.origin_x = x;
	pkt.origin_y = y;
	pkt.target_x = radius;
	if (role == ROLE_SERVER)
		enet_host_broadcast(host, 2, makePacket(MSG_ALLY_POWER, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE));
	else if (role == ROLE_CLIENT && server_peer)
		enet_peer_send(server_peer, 2, makePacket(MSG_ALLY_POWER, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE));
}

std::vector<PowerVisualPacket> NetManager::drainAllyPowers() {
	std::vector<PowerVisualPacket> out;
	out.swap(pending_ally_powers);
	return out;
}

std::vector<PowerVisualPacket> NetManager::drainPowerVisualEvents() {
	std::vector<PowerVisualPacket> out;
	out.swap(pending_power_visuals);
	return out;
}

std::vector<std::pair<uint32_t, std::string> > NetManager::drainActionEvents() {
	std::vector<std::pair<uint32_t, std::string> > out;
	out.swap(pending_actions);
	return out;
}

void NetManager::sendEnemySpawn(uint32_t net_id, const std::string& type_filename) {
	if (!host || role != ROLE_SERVER)
		return;

	EnemySpawnPacket pkt;
	pkt.net_id = net_id;
	copyToField(pkt.type_filename, EnemySpawnPacket::FIELD_LEN, type_filename);

	cached_enemy_spawns[net_id] = pkt;

	ENetPacket *packet = makePacket(MSG_ENEMY_SPAWN, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(host, 3, packet);
}

void NetManager::sendEnemyState(uint32_t net_id, float x, float y, uint8_t direction, uint8_t hp_percent, bool alive, const std::string& anim) {
	if (!host || role != ROLE_SERVER)
		return;

	EnemyStatePacket pkt;
	pkt.net_id = net_id;
	pkt.x = x;
	pkt.y = y;
	pkt.direction = direction;
	pkt.hp_percent = hp_percent;
	pkt.alive = alive ? 1 : 0;
	copyToField(pkt.anim, EnemyStatePacket::FIELD_LEN, anim);
	enemy_state_queue.push_back(pkt);
}

void NetManager::flushEnemyStates() {
	if (!host || role != ROLE_SERVER || enemy_state_queue.empty() || server_peers.empty()) {
		enemy_state_queue.clear();
		return;
	}

	// UNSEQUENCED: each batch is a full snapshot of its enemies, and plain
	// unreliable (per-channel sequenced) could drop a fresh batch for
	// looking "older" than another one of the same tick.
	for (size_t start = 0; start < enemy_state_queue.size(); start += MAX_ENEMY_STATES_PER_PACKET) {
		uint16_t count = static_cast<uint16_t>(std::min<size_t>(MAX_ENEMY_STATES_PER_PACKET, enemy_state_queue.size() - start));
		ENetPacket *packet = enet_packet_create(NULL, 3 + count * sizeof(EnemyStatePacket), ENET_PACKET_FLAG_UNSEQUENCED);
		packet->data[0] = MSG_ENEMY_STATES;
		memcpy(packet->data + 1, &count, sizeof(count));
		memcpy(packet->data + 3, &enemy_state_queue[start], count * sizeof(EnemyStatePacket));
		enet_host_broadcast(host, 4, packet);
	}
	enemy_state_queue.clear();
}

void NetManager::pollDiscovery() {
	if (discovery_socket == ENET_SOCKET_NULL)
		return;

	char buf[16];
	for (int guard = 0; guard < 16; ++guard) {
		ENetAddress from;
		ENetBuffer in;
		in.data = buf;
		in.dataLength = sizeof(buf);
		int got = enet_socket_receive(discovery_socket, &from, &in, 1);
		if (got <= 0)
			break;
		if (got != sizeof(DISCOVERY_QUERY) || memcmp(buf, DISCOVERY_QUERY, sizeof(DISCOVERY_QUERY)) != 0)
			continue;

		DiscoveryReply reply;
		memset(&reply, 0, sizeof(reply));
		memcpy(reply.magic, "RDH1", 4);
		reply.protocol = PROTOCOL_VERSION;
		reply.port = server_port;
		reply.players = static_cast<uint16_t>(server_peers.size() + 1);
		copyToField(reply.name, sizeof(reply.name), local_name);
		copyToField(reply.map, sizeof(reply.map), local_map);

		ENetBuffer out;
		out.data = &reply;
		out.dataLength = sizeof(reply);
		enet_socket_send(discovery_socket, &from, &out, 1);
	}
}

std::vector<LanGame> NetManager::discoverLan(uint32_t wait_ms) {
	std::vector<LanGame> found;
	if (enet_initialize() != 0)
		return found;

	ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (sock == ENET_SOCKET_NULL) {
		enet_deinitialize();
		return found;
	}
	enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
	enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

	// 255.255.255.255 only leaves through the default route -- with a VPN
	// (Cloudflare WARP...) or virtual adapters (VirtualBox, Hyper-V) that's
	// not the home network. So also each adapter's own subnet broadcast
	// (192.168.0.255...), plus loopback for a host on this same machine.
	std::vector<ENetAddress> targets;
	ENetAddress t;
	t.host = ENET_HOST_BROADCAST;
	t.port = DISCOVERY_PORT;
	targets.push_back(t);
	std::vector<LocalAddr> locals = localAddresses();
	for (size_t i = 0; i < locals.size(); ++i) {
		t.host = locals[i].broadcast_be;
		targets.push_back(t);
	}
	enet_address_set_host(&t, "127.0.0.1");
	t.port = DISCOVERY_PORT;
	targets.push_back(t);
	for (int round = 0; round < 2; ++round) {   // UDP may drop one
		for (size_t i = 0; i < targets.size(); ++i) {
			ENetBuffer out;
			out.data = const_cast<char*>(DISCOVERY_QUERY);
			out.dataLength = sizeof(DISCOVERY_QUERY);
			enet_socket_send(sock, &targets[i], &out, 1);
		}
	}

	const uint32_t start = enet_time_get();
	while (enet_time_get() - start < wait_ms) {
		enet_uint32 cond = ENET_SOCKET_WAIT_RECEIVE;
		if (enet_socket_wait(sock, &cond, 50) != 0 || !(cond & ENET_SOCKET_WAIT_RECEIVE))
			continue;

		DiscoveryReply reply;
		ENetAddress from;
		ENetBuffer in;
		in.data = &reply;
		in.dataLength = sizeof(reply);
		int got = enet_socket_receive(sock, &from, &in, 1);
		if (got != static_cast<int>(sizeof(reply)) || memcmp(reply.magic, "RDH1", 4) != 0 || reply.protocol != PROTOCOL_VERSION)
			continue;

		char ip[64];
		if (enet_address_get_host_ip(&from, ip, sizeof(ip)) != 0)
			continue;

		LanGame g;
		char addr[96];
		snprintf(addr, sizeof(addr), "%s:%u", ip, static_cast<unsigned>(reply.port));
		g.address = addr;
		g.name = fieldToString(reply.name, sizeof(reply.name));
		g.map = fieldToString(reply.map, sizeof(reply.map));
		g.players = reply.players;

		// the same host can answer on both the broadcast and loopback query
		bool dup = false;
		for (size_t i = 0; i < found.size(); ++i) {
			if (found[i].address == g.address || (found[i].name == g.name && found[i].map == g.map && g.address.compare(0, 4, "127.") == 0))
				dup = true;
		}
		if (!dup)
			found.push_back(g);
	}

	enet_socket_destroy(sock);
	enet_deinitialize();
	return found;
}

void NetManager::sendEnemyDespawn(uint32_t net_id) {
	if (!host || role != ROLE_SERVER)
		return;

	cached_enemy_spawns.erase(net_id);

	EnemyDespawnPacket pkt;
	pkt.net_id = net_id;

	ENetPacket *packet = makePacket(MSG_ENEMY_DESPAWN, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(host, 3, packet);
}

void NetManager::sendEnemyHit(uint32_t net_id, float damage) {
	if (!host || role != ROLE_CLIENT || !server_peer)
		return;

	EnemyHitPacket pkt;
	pkt.net_id = net_id;
	pkt.damage = damage;

	ENetPacket *packet = makePacket(MSG_ENEMY_HIT, &pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(server_peer, 5, packet);
}

std::vector<std::pair<uint32_t, float> > NetManager::drainEnemyHitEvents() {
	std::vector<std::pair<uint32_t, float> > out;
	out.swap(pending_enemy_hits);
	return out;
}

void NetManager::sendPlayerHit(uint32_t target_player_id, const PlayerHitPacket& pkt) {
	if (!host)
		return;

	PlayerHitPacket out_pkt = pkt;
	out_pkt.target_player_id = target_player_id;

	if (role == ROLE_CLIENT) {
		// PvP only; the server checks its own rule and forwards to the victim
		if (!server_peer || !pvp)
			return;
		ENetPacket *out = makePacket(MSG_PLAYER_HIT, &out_pkt, sizeof(out_pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 5, out);
		return;
	}
	if (role != ROLE_SERVER)
		return;

	std::map<uint32_t, ENetPeer*>::iterator it = server_peers.find(target_player_id);
	if (it == server_peers.end())
		return;

	ENetPacket *out = makePacket(MSG_PLAYER_HIT, &out_pkt, sizeof(out_pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(it->second, 5, out);
}

void NetManager::setPvp(bool enabled) {
	pvp = enabled;
	if (role != ROLE_SERVER)
		return;

	Utils::logInfo("NetManager: pvp=%d", pvp ? 1 : 0);
	host_map_packet.pvp = pvp ? 1 : 0;
	// clients learn the rule from MapPacket: resend it if we're already on a map
	if (has_host_map && host) {
		for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
			ENetPacket *out = makePacket(MSG_MAP, &host_map_packet, sizeof(MapPacket), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(it->second, 1, out);
		}
	}
}

std::vector<PlayerHitPacket> NetManager::drainPlayerHitEvents() {
	std::vector<PlayerHitPacket> out;
	out.swap(pending_player_hits);
	return out;
}

void NetManager::sendMapChange(const std::string& map, float x, float y) {
	if (role != ROLE_SERVER || !host)
		return;
	MapPacket pkt;
	strncpy(pkt.map, map.c_str(), MapPacket::FIELD_LEN - 1);
	pkt.x = x;
	pkt.y = y;
	pkt.pvp = pvp ? 1 : 0;
	host_map_packet = pkt;
	has_host_map = true;
	// enemies of the previous map must not be backfilled to newcomers anymore
	cached_enemy_spawns.clear();
	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		ENetPacket *out = makePacket(MSG_MAP, &pkt, sizeof(MapPacket), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(it->second, 1, out);
	}
	Utils::logInfo("NetManager: party map is now %s", map.c_str());
}

bool NetManager::takeMapChange(std::string& map, float& x, float& y) {
	if (!map_change_pending)
		return false;
	map_change_pending = false;
	map = host_map;
	x = host_map_packet.x;
	y = host_map_packet.y;
	return true;
}

int NetManager::getHostWave() const {
	std::map<uint32_t, NetPos>::const_iterator it = remote_positions.find(0);
	return it != remote_positions.end() ? it->second.wave : 0;
}

int NetManager::getHostTheme() const {
	std::map<uint32_t, NetPos>::const_iterator it = remote_positions.find(0);
	return it != remote_positions.end() ? it->second.theme : 0;
}

int NetManager::getHostFamily() const {
	std::map<uint32_t, NetPos>::const_iterator it = remote_positions.find(0);
	return it != remote_positions.end() ? it->second.family : 0;
}


// ------------------------------------------------------------------ relay rooms

NetManager *NetManager::relay_owner = NULL;

static const char RELAY_MAGIC[] = "RDRL";

bool NetManager::looksLikeRoomCode(const std::string& text) {
	if (text.size() != 5)
		return false;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
			return false;
	}
	return true;
}

/**
 * engine/online.txt: relay=host:port (default port 4650). Resolved again on
 * each use, so a relay behind a changing DNS name keeps working.
 */
bool NetManager::resolveRelay() {
	std::string relay;
	FileParser infile;
	if (infile.open("engine/online.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE)) {
		while (infile.next()) {
			if (infile.key == "relay")
				relay = infile.val;
		}
		infile.close();
	}
	if (relay.empty()) {
		room_error = "norelay";
		Utils::logError("NetManager: no relay configured (engine/online.txt relay=host:port)");
		return false;
	}
	uint16_t port = 4650;
	std::string name = relay;
	size_t colon = relay.rfind(':');
	if (colon != std::string::npos) {
		name = relay.substr(0, colon);
		port = static_cast<uint16_t>(Parse::toInt(relay.substr(colon + 1), 4650));
	}
	if (enet_address_set_host(&relay_addr, name.c_str()) != 0) {
		room_error = "resolve";
		Utils::logError("NetManager: could not resolve relay '%s'", name.c_str());
		return false;
	}
	relay_addr.port = port;
	return true;
}

void NetManager::sendRelay(const ENetAddress& to, const std::string& text) {
	if (!host)
		return;
	std::string msg = std::string(RELAY_MAGIC) + " " + text;
	ENetBuffer buf;
	buf.data = const_cast<char*>(msg.c_str());
	buf.dataLength = msg.size();
	enet_socket_send(host->socket, &to, &buf, 1);
}

// Raw relay messages share the ENet socket; this pulls them out before ENet
// tries to parse them as its own protocol.
int ENET_CALLBACK NetManager::interceptRelay(ENetHost *h, ENetEvent *event) {
	(void)event;
	if (!relay_owner || h->receivedDataLength < 4 || memcmp(h->receivedData, RELAY_MAGIC, 4) != 0)
		return 0;
	std::string text(reinterpret_cast<const char*>(h->receivedData), h->receivedDataLength);
	relay_owner->handleRelayMessage(text, h->receivedAddress);
	return 1;
}

void NetManager::handleRelayMessage(const std::string& text, const ENetAddress& from) {
	// "RDRL CODE ABCDE" / "RDRL PEER 51234" / "RDRL OK" / "RDRL ERR why"
	std::string rest = text.substr(4);
	std::string cmd = Parse::popFirstString(rest, ' ');
	if (cmd.empty())
		cmd = Parse::popFirstString(rest, ' ');
	if (role == ROLE_SERVER) {
		if (cmd == "CODE") {
			std::string code = Parse::popFirstString(rest, ' ');
			if (code != room_code)
				Utils::logInfo("NetManager: room open, code %s", code.c_str());
			room_code = code;
			room_error.clear();
		}
		else if (cmd == "PEER") {
			// a friend is coming in through this relay port: open our NAT towards it
			ENetAddress to = from;
			to.port = static_cast<uint16_t>(Parse::toInt(Parse::popFirstString(rest, ' ')));
			for (int i = 0; i < 3; ++i)
				sendRelay(to, "PUNCH");
			Utils::logInfo("NetManager: relay peer on port %u", static_cast<unsigned>(to.port));
		}
		else if (cmd == "ERR") {
			room_error = Parse::popFirstString(rest, ' ');
			Utils::logError("NetManager: relay refused the room (%s)", room_error.c_str());
		}
	}
	else {
		join_reply = cmd == "ERR" ? "ERR " + Parse::popFirstString(rest, ' ') : cmd;
	}
}

bool NetManager::openRoom() {
	if (role != ROLE_SERVER || !host)
		return false;
	room_error.clear();
	if (!resolveRelay())
		return false;
	relay_owner = this;
	host->intercept = interceptRelay;
	room_requested = true;
	room_code.clear();
	room_last_send = 0;
	room_request_start = enet_time_get();
	pollRelay();
	return true;
}

void NetManager::closeRoom() {
	if (room_requested && host)
		sendRelay(relay_addr, "CLOSE");
	room_requested = false;
	room_code.clear();
}

// Host: (re)register the room -- quickly until the code arrives, then every
// 10 s as a keepalive (the relay drops rooms silent for 45 s).
void NetManager::pollRelay() {
	if (!room_requested || !host)
		return;
	const uint32_t now = enet_time_get();
	const uint32_t every = room_code.empty() ? 1000 : 10000;
	if (room_last_send == 0 || now - room_last_send >= every) {
		room_last_send = now ? now : 1;
		char text[64];
		snprintf(text, sizeof(text), "HOST %u %s", static_cast<unsigned>(PROTOCOL_VERSION), room_code.c_str());
		sendRelay(relay_addr, text);
	}
	if (room_code.empty() && room_error.empty() && now - room_request_start > 8000)
		room_error = "timeout";
}

bool NetManager::connectWithCode(const std::string& code_in, uint32_t timeout_ms) {
	std::string code = code_in;
	for (size_t i = 0; i < code.size(); ++i)
		code[i] = static_cast<char>(toupper(code[i]));

	if (enet_initialize() != 0) {
		Utils::logError("NetManager: could not initialize ENet");
		return false;
	}
	host = enet_host_create(NULL, 1, 6, 0, 0);
	if (!host) {
		Utils::logError("NetManager: could not create ENet client host");
		enet_deinitialize();
		return false;
	}
	if (!resolveRelay()) {
		last_error = room_error;
		enet_host_destroy(host);
		host = NULL;
		enet_deinitialize();
		return false;
	}
	relay_owner = this;
	host->intercept = interceptRelay;
	join_reply.clear();

	// ask the relay to put us in that room; resend in case a datagram is lost
	char text[64];
	snprintf(text, sizeof(text), "JOIN %u %s", static_cast<unsigned>(PROTOCOL_VERSION), code.c_str());
	const uint32_t start = enet_time_get();
	uint32_t last_send = 0;
	ENetEvent event;
	while (join_reply.empty() && enet_time_get() - start < timeout_ms) {
		if (last_send == 0 || enet_time_get() - last_send > 400) {
			sendRelay(relay_addr, text);
			last_send = enet_time_get();
			if (last_send == 0) last_send = 1;
		}
		enet_host_service(host, &event, 50);
	}
	if (join_reply != "OK") {
		if (join_reply.empty()) last_error = "relay";
		else if (join_reply == "ERR nocode") last_error = "nocode";
		else if (join_reply == "ERR version") last_error = "version";
		else if (join_reply == "ERR full") last_error = "full";
		else last_error = "refused";
		Utils::logError("NetManager: relay join %s failed (%s)", code.c_str(), last_error.c_str());
		relay_owner = NULL;
		enet_host_destroy(host);
		host = NULL;
		enet_deinitialize();
		return false;
	}
	Utils::logInfo("NetManager: relay accepted us into room %s", code.c_str());
	return connectAddress(relay_addr, "room " + code, timeout_ms);
}


// ------------------------------------------------------------------ local addresses

namespace {
	// home networks first; VPN / container / virtual adapters last
	std::string vpnName(const std::string& name, uint32_t ip_host_order) {
		std::string n = name;
		for (size_t i = 0; i < n.size(); ++i) n[i] = static_cast<char>(tolower(n[i]));
		if (n.find("tailscale") != std::string::npos) return "Tailscale";
		if (n.find("zerotier") != std::string::npos || n.compare(0, 2, "zt") == 0) return "ZeroTier";
		if (n.find("radmin") != std::string::npos) return "Radmin VPN";
		if (n.find("hamachi") != std::string::npos || n.compare(0, 3, "ham") == 0) return "Hamachi";
		// Tailscale's own range, whatever the adapter is called
		if ((ip_host_order >> 24) == 100 && ((ip_host_order >> 16) & 0xff) >= 64 && ((ip_host_order >> 16) & 0xff) < 128 && n.find("warp") == std::string::npos)
			return "Tailscale";
		return "";
	}

	int rankAdapter(const std::string& name, uint32_t ip_host_order) {
		std::string n = name;
		for (size_t i = 0; i < n.size(); ++i) n[i] = static_cast<char>(tolower(n[i]));
		const char *virt[] = { "warp", "cloudflare", "tailscale", "zerotier", "wireguard", "wg", "tun", "tap",
		                       "docker", "br-", "virbr", "veth", "vmware", "virtualbox", "vbox", "hyper-v", "vethernet", "radmin", "hamachi" };
		for (size_t i = 0; i < sizeof(virt) / sizeof(virt[0]); ++i)
			if (n.find(virt[i]) != std::string::npos)
				return 3;
		const uint32_t a = ip_host_order >> 24, b = (ip_host_order >> 16) & 0xff;
		if (a == 192 && b == 168) return 0;
		if (a == 10) return 1;
		if (a == 172 && b >= 16 && b < 32) return 1;
		if (a == 100 && b >= 64 && b < 128) return 3; // CGNAT / Tailscale range
		return 2;
	}
}

std::vector<NetManager::LocalAddr> NetManager::localAddresses() {
	std::vector<LocalAddr> out;
#ifdef _WIN32
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buf(size);
	IP_ADAPTER_ADDRESSES *list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&buf[0]);
	ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size);
	if (rc == ERROR_BUFFER_OVERFLOW) {
		buf.resize(size);
		list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&buf[0]);
		rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size);
	}
	if (rc != NO_ERROR)
		return out;
	for (IP_ADAPTER_ADDRESSES *ad = list; ad; ad = ad->Next) {
		if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		char name[256];
		WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, name, sizeof(name), NULL, NULL);
		std::string desc_name = name;
		char desc[256];
		WideCharToMultiByte(CP_UTF8, 0, ad->Description, -1, desc, sizeof(desc), NULL, NULL);
		desc_name += " ";
		desc_name += desc;
		for (IP_ADAPTER_UNICAST_ADDRESS *ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
			sockaddr_in *sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
			if (sin->sin_family != AF_INET)
				continue;
			uint32_t ip = ntohl(sin->sin_addr.s_addr);
			if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE) // loopback, 169.254 link-local
				continue;
			uint32_t mask = ua->OnLinkPrefixLength >= 32 ? 0xffffffffu : ~(0xffffffffu >> ua->OnLinkPrefixLength);
			LocalAddr la;
			la.ip_be = htonl(ip);
			la.broadcast_be = htonl(ip | ~mask);
			char txt[32];
			snprintf(txt, sizeof(txt), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
			la.ip = txt;
			la.rank = rankAdapter(desc_name, ip);
			la.vpn = vpnName(desc_name, ip);
			out.push_back(la);
		}
	}
#else
	struct ifaddrs *ifs = NULL;
	if (getifaddrs(&ifs) != 0)
		return out;
	for (struct ifaddrs *it = ifs; it; it = it->ifa_next) {
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
			continue;
		if ((it->ifa_flags & IFF_LOOPBACK) || !(it->ifa_flags & IFF_UP))
			continue;
		uint32_t ip = ntohl(reinterpret_cast<struct sockaddr_in*>(it->ifa_addr)->sin_addr.s_addr);
		uint32_t mask = it->ifa_netmask ? ntohl(reinterpret_cast<struct sockaddr_in*>(it->ifa_netmask)->sin_addr.s_addr) : 0xffffff00u;
		if ((ip >> 16) == 0xA9FE)
			continue;
		LocalAddr la;
		la.ip_be = htonl(ip);
		la.broadcast_be = htonl(ip | ~mask);
		char txt[32];
		snprintf(txt, sizeof(txt), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
		la.ip = txt;
		la.rank = rankAdapter(it->ifa_name ? it->ifa_name : "", ip);
		la.vpn = vpnName(it->ifa_name ? it->ifa_name : "", ip);
		out.push_back(la);
	}
	freeifaddrs(ifs);
#endif
	for (size_t i = 1; i < out.size(); ++i)   // stable sort by rank
		for (size_t j = i; j > 0 && out[j].rank < out[j - 1].rank; --j)
			std::swap(out[j], out[j - 1]);
	return out;
}

std::string NetManager::vpnAddressText(uint16_t port) {
	std::vector<LocalAddr> locals = localAddresses();
	for (size_t i = 0; i < locals.size(); ++i) {
		if (locals[i].vpn.empty())
			continue;
		std::string text = locals[i].vpn + ": " + locals[i].ip;
		if (port != 0 && port != 4650) {
			char p[16];
			snprintf(p, sizeof(p), ":%u", static_cast<unsigned>(port));
			text += p;
		}
		return text;
	}
	return "";
}

std::string NetManager::lanAddressText(uint16_t port) {
	std::vector<LocalAddr> locals = localAddresses();
	for (size_t i = 0; i < locals.size();) {   // virtual LANs are shown separately
		if (!locals[i].vpn.empty()) locals.erase(locals.begin() + i);
		else ++i;
	}
	if (locals.empty())
		return "";
	std::string text = locals[0].ip;
	if (port != 0 && port != 4650) {
		char p[16];
		snprintf(p, sizeof(p), ":%u", static_cast<unsigned>(port));
		text += p;
	}
	return text;
}
