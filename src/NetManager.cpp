#include "NetManager.h"

#include "Utils.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
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
}

NetManager::NetManager()
	: role(ROLE_NONE)
	, host(NULL)
	, server_peer(NULL) {
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
	Utils::logInfo("NetManager: server listening on port %u", static_cast<unsigned>(port));
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
		return false;
	}
	address.port = port;

	server_peer = enet_host_connect(host, &address, 6, 0);
	if (!server_peer) {
		Utils::logError("NetManager: no available peers for connection attempt");
		return false;
	}

	ENetEvent event;
	if (enet_host_service(host, &event, timeout_ms) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
		role = ROLE_CLIENT;
		Utils::logInfo("NetManager: connected to %s:%u", host_str.c_str(), static_cast<unsigned>(port));
		return true;
	}

	enet_peer_reset(server_peer);
	server_peer = NULL;
	Utils::logError("NetManager: connection to %s:%u failed/timed out", host_str.c_str(), static_cast<unsigned>(port));
	return false;
}

void NetManager::shutdown() {
	if (server_peer) {
		enet_peer_disconnect_now(server_peer, 0);
		server_peer = NULL;
	}
	if (host) {
		enet_host_destroy(host);
		host = NULL;
	}
	if (role != ROLE_NONE) {
		enet_deinitialize();
	}
	role = ROLE_NONE;
	server_peers.clear();
	remote_positions.clear();
	remote_appearances.clear();
	cached_appearances.clear();
	pending_actions.clear();
	pending_power_visuals.clear();
	cached_enemy_spawns.clear();
	remote_enemies.clear();
	pending_enemy_hits.clear();
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

		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), 0); // unreliable, matches real movement sync later
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
					if (event.packet->dataLength == sizeof(TickPacket)) {
						TickPacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(TickPacket));
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

	ENetEvent event;
	while (enet_host_service(host, &event, 0) > 0) {
		switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT:
				if (role == ROLE_SERVER) {
					server_peers[event.peer->connectID] = event.peer;
					Utils::logInfo("NetManager: player %u connected (%u total)", static_cast<unsigned>(event.peer->connectID), static_cast<unsigned>(server_peers.size()));

					// Backfill: everyone's appearance sent so far (including
					// our own, cached under id 0) -- appearance is only ever
					// sent once per connection, so without this a peer that
					// joins after someone else already sent theirs would
					// never see them.
					for (std::map<uint32_t, AppearancePacket>::iterator it = cached_appearances.begin(); it != cached_appearances.end(); ++it) {
						ENetPacket *out = enet_packet_create(&it->second, sizeof(AppearancePacket), ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(event.peer, 1, out);
					}

					// Backfill: every networked enemy spawned so far, same reasoning.
					for (std::map<uint32_t, EnemySpawnPacket>::iterator it = cached_enemy_spawns.begin(); it != cached_enemy_spawns.end(); ++it) {
						ENetPacket *out = enet_packet_create(&it->second, sizeof(EnemySpawnPacket), ENET_PACKET_FLAG_RELIABLE);
						enet_peer_send(event.peer, 3, out);
					}
				}
				else {
					Utils::logInfo("NetManager: peer connected");
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (event.packet->dataLength == sizeof(TickPacket)) {
					TickPacket pkt;
					memcpy(&pkt, event.packet->data, sizeof(TickPacket));

					if (role == ROLE_SERVER) {
						// The sender is identified by the ENet connection itself,
						// not by whatever it put in pkt.player_id.
						uint32_t sender_id = event.peer->connectID;
						remote_positions[sender_id] = NetPos(pkt.x, pkt.y);

						// Relay to everyone else, tagged with the true sender id.
						TickPacket relay;
						relay.player_id = sender_id;
						relay.tick = pkt.tick;
						relay.x = pkt.x;
						relay.y = pkt.y;
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
							ENetPacket *out = enet_packet_create(&relay, sizeof(relay), ENET_PACKET_FLAG_UNSEQUENCED);
							enet_peer_send(it->second, 0, out);
						}
					}
					else {
						remote_positions[pkt.player_id] = NetPos(pkt.x, pkt.y);
					}
				}
				else if (event.packet->dataLength == sizeof(AppearancePacket)) {
					AppearancePacket pkt;
					memcpy(&pkt, event.packet->data, sizeof(AppearancePacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;

					PlayerAppearance app;
					app.gfx_base.assign(pkt.gfx_base, strnlen(pkt.gfx_base, AppearancePacket::FIELD_LEN));
					app.gfx_head.assign(pkt.gfx_head, strnlen(pkt.gfx_head, AppearancePacket::FIELD_LEN));
					for (size_t i = 0; i < AppearancePacket::NUM_LAYERS; i++) {
						app.layers.push_back(std::string(pkt.layers[i], strnlen(pkt.layers[i], AppearancePacket::FIELD_LEN)));
					}
					remote_appearances[owner_id] = app;
					Utils::logInfo("NetManager: received appearance for player %u (gfx_base=%s)", static_cast<unsigned>(owner_id), app.gfx_base.c_str());

					if (role == ROLE_SERVER) {
						AppearancePacket cached = pkt;
						cached.player_id = owner_id;
						cached_appearances[owner_id] = cached;
						relayAppearance(owner_id, pkt);
					}
				}
				else if (event.packet->dataLength == sizeof(ActionPacket)) {
					ActionPacket pkt;
					memcpy(&pkt, event.packet->data, sizeof(ActionPacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;
					std::string anim_name(pkt.anim, strnlen(pkt.anim, ActionPacket::FIELD_LEN));
					Utils::logInfo("NetManager: player %u action '%s'", static_cast<unsigned>(owner_id), anim_name.c_str());
					pending_actions.push_back(std::make_pair(owner_id, anim_name));

					if (role == ROLE_SERVER) {
						relayAction(owner_id, pkt);
					}
				}
				else if (event.packet->dataLength == sizeof(PowerVisualPacket)) {
					PowerVisualPacket pkt;
					memcpy(&pkt, event.packet->data, sizeof(PowerVisualPacket));

					uint32_t owner_id = (role == ROLE_SERVER) ? event.peer->connectID : pkt.player_id;
					pkt.player_id = owner_id;
					Utils::logInfo("NetManager: player %u power_id=%u visual (%.1f,%.1f)->(%.1f,%.1f)", static_cast<unsigned>(owner_id), static_cast<unsigned>(pkt.power_id), static_cast<double>(pkt.origin_x), static_cast<double>(pkt.origin_y), static_cast<double>(pkt.target_x), static_cast<double>(pkt.target_y));
					pending_power_visuals.push_back(pkt);

					if (role == ROLE_SERVER) {
						relayPowerVisual(owner_id, pkt);
					}
				}
				else if (event.packet->dataLength == sizeof(EnemySpawnPacket)) {
					// Host -> client only; a client never sends this.
					if (role == ROLE_CLIENT) {
						EnemySpawnPacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(EnemySpawnPacket));
						RemoteEnemyState& re = remote_enemies[pkt.net_id];
						re.type_filename.assign(pkt.type_filename, strnlen(pkt.type_filename, EnemySpawnPacket::FIELD_LEN));
						Utils::logInfo("NetManager: enemy net_id=%u spawned (%s)", static_cast<unsigned>(pkt.net_id), re.type_filename.c_str());
					}
				}
				else if (event.packet->dataLength == sizeof(EnemyStatePacket)) {
					// Host -> client only.
					if (role == ROLE_CLIENT) {
						EnemyStatePacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(EnemyStatePacket));
						std::map<uint32_t, RemoteEnemyState>::iterator found = remote_enemies.find(pkt.net_id);
						if (found != remote_enemies.end()) {
							RemoteEnemyState& re = found->second;
							re.x = pkt.x;
							re.y = pkt.y;
							re.direction = pkt.direction;
							re.hp_percent = pkt.hp_percent;
							re.alive = pkt.alive;
							re.anim.assign(pkt.anim, strnlen(pkt.anim, EnemyStatePacket::FIELD_LEN));
						}
					}
				}
				else if (event.packet->dataLength == sizeof(EnemyDespawnPacket)) {
					// Host -> client only.
					if (role == ROLE_CLIENT) {
						EnemyDespawnPacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(EnemyDespawnPacket));
						Utils::logInfo("NetManager: enemy net_id=%u despawned", static_cast<unsigned>(pkt.net_id));
						remote_enemies.erase(pkt.net_id);
					}
				}
				else if (event.packet->dataLength == sizeof(EnemyHitPacket)) {
					// Client -> host only, never relayed.
					if (role == ROLE_SERVER) {
						EnemyHitPacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(EnemyHitPacket));
						Utils::logInfo("NetManager: received hit report net_id=%u damage=%.1f", static_cast<unsigned>(pkt.net_id), static_cast<double>(pkt.damage));
						pending_enemy_hits.push_back(std::make_pair(pkt.net_id, pkt.damage));
					}
				}
				else if (event.packet->dataLength == sizeof(PlayerHitPacket)) {
					// Host -> client only.
					if (role == ROLE_CLIENT) {
						PlayerHitPacket pkt;
						memcpy(&pkt, event.packet->data, sizeof(PlayerHitPacket));
						Utils::logInfo("NetManager: received player hit power_id=%u", static_cast<unsigned>(pkt.power_id));
						pending_player_hits.push_back(pkt);
					}
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
					Utils::logInfo("NetManager: player %u disconnected (%u total)", static_cast<unsigned>(gone_id), static_cast<unsigned>(server_peers.size()));
				}
				else {
					Utils::logInfo("NetManager: disconnected from server");
					remote_positions.clear();
					remote_appearances.clear();
					remote_enemies.clear();
				}
				break;
			default:
				break;
		}
	}
}

void NetManager::sendPosition(float x, float y) {
	if (!host)
		return;

	TickPacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	pkt.tick = 0;
	pkt.x = x;
	pkt.y = y;

	// UNSEQUENCED (see the relay comment above for why plain unreliable is
	// the wrong choice once more than one sender shares a channel).
	ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);

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

void NetManager::sendAppearance(const std::string& gfx_base, const std::string& gfx_head, const std::vector<std::string>& layers) {
	if (!host)
		return;

	AppearancePacket pkt;
	pkt.player_id = 0; // host's own hero; clients' player_id is set by the server on relay
	copyToField(pkt.gfx_base, AppearancePacket::FIELD_LEN, gfx_base);
	copyToField(pkt.gfx_head, AppearancePacket::FIELD_LEN, gfx_head);
	for (size_t i = 0; i < AppearancePacket::NUM_LAYERS; i++) {
		if (i < layers.size())
			copyToField(pkt.layers[i], AppearancePacket::FIELD_LEN, layers[i]);
	}

	if (role == ROLE_SERVER) {
		cached_appearances[0] = pkt; // so peers who join later still get it, see ENET_EVENT_TYPE_CONNECT
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 1, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 1, packet);
	}
}

void NetManager::relayAppearance(uint32_t sender_id, const AppearancePacket& pkt) {
	AppearancePacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = enet_packet_create(&relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
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
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 2, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 2, packet);
	}
}

void NetManager::relayAction(uint32_t sender_id, const ActionPacket& pkt) {
	ActionPacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = enet_packet_create(&relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
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
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(host, 2, packet);
	}
	else if (role == ROLE_CLIENT && server_peer) {
		ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(server_peer, 2, packet);
	}
}

void NetManager::relayPowerVisual(uint32_t sender_id, const PowerVisualPacket& pkt) {
	PowerVisualPacket relay = pkt;
	relay.player_id = sender_id;

	for (std::map<uint32_t, ENetPeer*>::iterator it = server_peers.begin(); it != server_peers.end(); ++it) {
		if (it->first == sender_id)
			continue;
		ENetPacket *out = enet_packet_create(&relay, sizeof(relay), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(it->second, 2, out);
	}
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

	ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
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

	// UNSEQUENCED: this is called once per networked enemy per tick, all on
	// the same channel. Plain unreliable (flag 0) is per-channel sequenced
	// in ENet, so enemy A's and enemy B's packets look like one ordered
	// stream to it -- under any jitter, a genuinely fresh update for one
	// enemy can get silently dropped for looking "older" than an unrelated
	// enemy's packet that happened to arrive first. That's exactly what
	// was causing one of two test skeletons to never show as dead client-
	// side while the other did, consistently, on localhost. UNSEQUENCED
	// makes every packet independent instead, which is what we want since
	// each carries a full current-state snapshot anyway.
	ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);
	enet_host_broadcast(host, 4, packet);
}

void NetManager::sendEnemyDespawn(uint32_t net_id) {
	if (!host || role != ROLE_SERVER)
		return;

	cached_enemy_spawns.erase(net_id);

	EnemyDespawnPacket pkt;
	pkt.net_id = net_id;

	ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(host, 3, packet);
}

void NetManager::sendEnemyHit(uint32_t net_id, float damage) {
	if (!host || role != ROLE_CLIENT || !server_peer)
		return;

	EnemyHitPacket pkt;
	pkt.net_id = net_id;
	pkt.damage = damage;

	ENetPacket *packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(server_peer, 5, packet);
}

std::vector<std::pair<uint32_t, float> > NetManager::drainEnemyHitEvents() {
	std::vector<std::pair<uint32_t, float> > out;
	out.swap(pending_enemy_hits);
	return out;
}

void NetManager::sendPlayerHit(uint32_t target_player_id, const PlayerHitPacket& pkt) {
	if (role != ROLE_SERVER)
		return;

	std::map<uint32_t, ENetPeer*>::iterator it = server_peers.find(target_player_id);
	if (it == server_peers.end())
		return;

	ENetPacket *out = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(it->second, 5, out);
}

std::vector<PlayerHitPacket> NetManager::drainPlayerHitEvents() {
	std::vector<PlayerHitPacket> out;
	out.swap(pending_player_hits);
	return out;
}
