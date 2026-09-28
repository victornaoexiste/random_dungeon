/*
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

#include "CommonIncludes.h"
#include "EngineSettings.h"
#include "FontEngine.h"
#include "GameStateLoad.h"
#include "GameStateMultiplayer.h"
#include "GameStateNew.h"
#include "GameStateTitle.h"
#include "InputState.h"
#include "MessageEngine.h"
#include "NetManager.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"
#include "Utils.h"
#include "UtilsFileSystem.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetInput.h"
#include "WidgetLabel.h"

// Vertical layout, in pixels from the centre of the 640x460 panel
// (images/menus/df_panel_multiplayer.png, drawn by the darkfantasy_gui mod).
namespace {
	const int PANEL_TOP = -230;
	const int TITLE_Y = PANEL_TOP + 14;
	const int HOST_LABEL_Y = -178;
	const int HOST_BTN_Y = -146;
	const int PVP_BTN_Y = -94;
	const int JOIN_LABEL_Y = -60;
	const int INPUT_Y = -6;
	const int SEARCH_BTN_Y = 38;
	const int JOIN_BTN_Y = 90;
	const int STATUS_Y = 122;
	const int BACK_BTN_Y = 178;
}

GameStateMultiplayer::GameStateMultiplayer()
	: GameState()
	, panel(NULL)
	, label_title(new WidgetLabel())
	, label_host(new WidgetLabel())
	, label_join(new WidgetLabel())
	, label_status(new WidgetLabel())
	, button_host(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_pvp(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, input_ip(new WidgetInput(WidgetInput::DEFAULT_FILE))
	, button_search(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_join(new WidgetButton(WidgetButton::DEFAULT_FILE))
	, button_back(new WidgetButton(WidgetButton::DEFAULT_FILE))
{
	Image *graphics = render_device->loadImage("images/menus/df_panel_multiplayer.png", RenderDevice::ERROR_NONE);
	if (graphics) {
		panel = graphics->createSprite();
		graphics->unref();
	}

	const Color normal = font->getColor(FontEngine::COLOR_MENU_NORMAL);

	label_title->setText(msg->get("Multiplayer"));
	label_title->setJustify(FontEngine::JUSTIFY_CENTER);
	label_title->setColor(normal);

	label_host->setText(msg->get("Criar uma partida para jogar com amigos"));
	label_host->setJustify(FontEngine::JUSTIFY_CENTER);
	label_host->setColor(normal);

	label_join->setText(msg->get("Entrar: código da sala (ou IP na rede local)"));
	label_join->setJustify(FontEngine::JUSTIFY_CENTER);
	label_join->setColor(normal);

	label_status->setJustify(FontEngine::JUSTIFY_CENTER);
	setStatus("", false);

	button_host->setLabel(msg->get("Criar partida"));
	button_host->setBasePos(0, HOST_BTN_Y, Utils::ALIGN_CENTER);
	button_host->refresh();

	button_pvp->setBasePos(0, PVP_BTN_Y, Utils::ALIGN_CENTER);
	refreshPvpLabel();

	input_ip->max_length = 40;
	input_ip->setText("");
	input_ip->setBasePos(0, INPUT_Y, Utils::ALIGN_CENTER);

	button_search->setLabel(msg->get("Buscar na rede"));
	button_search->setBasePos(0, SEARCH_BTN_Y, Utils::ALIGN_CENTER);
	button_search->refresh();

	button_join->setLabel(msg->get("Entrar"));
	button_join->setBasePos(0, JOIN_BTN_Y, Utils::ALIGN_CENTER);
	button_join->refresh();

	button_back->setLabel(msg->get("Voltar"));
	button_back->setBasePos(0, BACK_BTN_Y, Utils::ALIGN_CENTER);
	button_back->refresh();

	tablist.add(button_host);
	tablist.add(button_pvp);
	tablist.add(input_ip);
	tablist.add(button_search);
	tablist.add(button_join);
	tablist.add(button_back);

	refreshWidgets();
	force_refresh_background = true;
}

void GameStateMultiplayer::refreshWidgets() {
	if (panel) {
		Rect r;
		r.x = 0;
		r.y = 0;
		r.w = panel->getGraphicsWidth();
		r.h = panel->getGraphicsHeight();
		Utils::alignToScreenEdge(Utils::ALIGN_CENTER, &r);
		panel->setDestFromRect(r);
	}

	button_host->setPos(0, 0);
	button_pvp->setPos(0, 0);
	input_ip->setPos(0, 0);
	button_search->setPos(0, 0);
	button_join->setPos(0, 0);
	button_back->setPos(0, 0);

	const int cx = settings->view_w / 2;
	const int cy = settings->view_h / 2;
	label_title->setPos(cx, cy + TITLE_Y);
	label_host->setPos(cx, cy + HOST_LABEL_Y);
	label_join->setPos(cx, cy + JOIN_LABEL_Y);
	label_status->setPos(cx, cy + STATUS_Y);
}

void GameStateMultiplayer::setStatus(const std::string& text, bool error) {
	label_status->setText(text);
	label_status->setColor(error ? Color(204, 84, 72) : font->getColor(FontEngine::COLOR_MENU_NORMAL));
}

void GameStateMultiplayer::refreshPvpLabel() {
	button_pvp->setLabel(settings->net_pvp ? msg->get("PvP: Ligado") : msg->get("PvP: Desligado"));
	button_pvp->refresh();
}

void GameStateMultiplayer::proceedToNewGame() {
	showLoading();
	if (runEdition())
		settings->game_mode = "run";

	std::vector<std::string> save_dirs;
	Filesystem::getDirList(settings->path_user + "saves/" + eset->misc.save_prefix, save_dirs);
	if (save_dirs.empty()) {
		GameStateNew *newgame = new GameStateNew();
		newgame->game_slot = 1;
		setRequestedGameState(newgame);
	}
	else {
		setRequestedGameState(new GameStateLoad());
	}
}

void GameStateMultiplayer::startAsHost() {
	if (netmgr && netmgr->isActive())
		netmgr->shutdown();
	if (!netmgr)
		netmgr = new NetManager();

	// the LAN port if free, else any port (the online room doesn't need it)
	if (netmgr->startServer(HOST_PORT) || netmgr->startServer(0)) {
		netmgr->setPvp(settings->net_pvp);
		// online room through the relay: the code shows up in the game (and
		// in the pause menu); without a relay, LAN/IP still work
		netmgr->openRoom();
		proceedToNewGame();
	}
	else {
		setStatus(msg->get("Não foi possível hospedar (porta em uso?)"), true);
	}
}

// Accepts "host" or "host:port". Port must be 1..65535.
bool GameStateMultiplayer::parseTarget(const std::string& target, std::string& host_str, uint16_t& port) {
	port = HOST_PORT;
	host_str = target;

	size_t colon = target.find(':');
	if (colon != std::string::npos) {
		host_str = target.substr(0, colon);
		const std::string port_str = target.substr(colon + 1);
		if (port_str.empty() || port_str.find_first_not_of("0123456789") != std::string::npos) {
			setStatus(msg->get("Porta inválida"), true);
			return false;
		}
		long p = atol(port_str.c_str());
		if (p < 1 || p > 65535) {
			setStatus(msg->get("Porta inválida"), true);
			return false;
		}
		port = static_cast<uint16_t>(p);
	}

	if (host_str.empty()) {
		setStatus(msg->get("Digite o código da sala ou o IP do host"), true);
		return false;
	}
	return true;
}

void GameStateMultiplayer::startAsClient() {
	std::string target = input_ip->getText();
	while (!target.empty() && target[0] == ' ') target.erase(0, 1);
	while (!target.empty() && target[target.size() - 1] == ' ') target.erase(target.size() - 1);
	const bool by_code = NetManager::looksLikeRoomCode(target);
	std::string host_str;
	uint16_t port = HOST_PORT;
	if (!by_code && !parseTarget(target, host_str, port))
		return;

	if (netmgr && netmgr->isActive())
		netmgr->shutdown();
	if (!netmgr)
		netmgr = new NetManager();

	// Blocking (see header comment) -- the screen will visibly hang for up
	// to this timeout on a failed connection attempt.
	setStatus(msg->get("Conectando..."), false);
	render();
	render_device->commitFrame();

	const bool ok = by_code ? netmgr->connectWithCode(target, 6000) : netmgr->connectToServer(host_str, port, 5000);
	const std::string err = netmgr->getLastError();
	if (ok) {
		proceedToNewGame();
	}
	else if (err == "version") {
		setStatus(msg->get("O host usa outra versão do jogo"), true);
	}
	else if (err == "nocode") {
		setStatus(msg->get("Sala não encontrada: confira o código"), true);
	}
	else if (err == "full") {
		setStatus(msg->get("A sala está cheia"), true);
	}
	else if (err == "relay" || err == "norelay" || err == "resolve") {
		setStatus(by_code ? msg->get("Servidor de salas fora do ar (tente pela rede local)") : msg->get("Endereço não encontrado"), true);
	}
	else if (by_code) {
		setStatus(msg->get("A sala existe, mas o host não respondeu"), true);
	}
	else if (host_str.compare(0, 4, "127.") == 0) {
		setStatus(msg->get("127.0.0.1 é o seu PC: digite o IP da tela do host"), true);
	}
	else {
		setStatus(msg->get("Host não respondeu. Wi-Fi de visitantes bloqueia: use o Tailscale"), true);
	}
}

// Blocking for a moment (see NetManager::discoverLan), like joining.
void GameStateMultiplayer::searchLan() {
	setStatus(msg->get("Procurando partidas na rede..."), false);
	render();
	render_device->commitFrame();

	std::vector<LanGame> games = NetManager::discoverLan(800);
	if (games.empty()) {
		// most common causes, in order: guest/school Wi-Fi isolating devices,
		// not the same network, host not open yet
		setStatus(msg->get("Nada encontrado. Wi-Fi de visitantes bloqueia: use o Tailscale"), true);
		return;
	}

	const LanGame& g = games[0];
	input_ip->setText(g.address);
	char num[16];
	snprintf(num, sizeof(num), "%u", g.players);
	std::string text = g.name + " - " + g.map + " (" + num + " " + msg->get("jogador(es)") + ")";
	if (games.size() > 1) {
		snprintf(num, sizeof(num), "%u", static_cast<unsigned>(games.size()));
		text = std::string(num) + " " + msg->get("partidas; usando") + " " + text;
	}
	setStatus(text, false);
}

void GameStateMultiplayer::logic() {
	// automated test hook (RD_DEVKIT_SELFTEST=<dir>, --mp-shot): capture this
	// screen once and quit
	static int selftest_frames = 0;
	if (settings->mp_screenshot && getenv("RD_DEVKIT_SELFTEST")) {
		++selftest_frames;
		if (selftest_frames == 60)
			render_device->screenshot_request = std::string(getenv("RD_DEVKIT_SELFTEST")) + "/multiplayer.png";
		else if (selftest_frames == 70)
			exitRequested = true;
	}

	if (inpt->window_resized)
		refreshWidgets();

	if (inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL]) {
		inpt->lock[Input::CANCEL] = true;
		setRequestedGameState(new GameStateTitle());
		return;
	}

	if (!input_ip->edit_mode)
		tablist.logic();

	input_ip->logic();

	if (!input_ip->edit_mode && !inpt->usingMouse() && tablist.getCurrent() == -1) {
		tablist.getNext(!TabList::GET_INNER, TabList::WIDGET_SELECT_AUTO);
	}

	if (button_host->checkClick()) {
		startAsHost();
	}
	else if (button_pvp->checkClick()) {
		settings->net_pvp = !settings->net_pvp;
		refreshPvpLabel();
	}
	else if (button_search->checkClick()) {
		searchLan();
	}
	else if (button_join->checkClick()) {
		startAsClient();
	}
	else if (button_back->checkClick()) {
		setRequestedGameState(new GameStateTitle());
	}
}

void GameStateMultiplayer::render() {
	if (panel)
		render_device->render(panel);

	label_title->render();
	label_join->render();
	label_status->render();

	button_host->render();
	button_pvp->render();
	input_ip->render();
	button_search->render();
	button_join->render();
	button_back->render();
}

GameStateMultiplayer::~GameStateMultiplayer() {
	if (panel) delete panel;
	delete label_title;
	delete label_host;
	delete label_join;
	delete label_status;
	delete button_host;
	delete button_pvp;
	delete input_ip;
	delete button_search;
	delete button_join;
	delete button_back;
}
