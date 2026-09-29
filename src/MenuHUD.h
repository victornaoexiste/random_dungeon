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

/**
 * class MenuHUD
 *
 * Random Dungeon: pecas do HUD que o Flare nao tem (menus/hud.txt).
 * - retrato do heroi com o nivel no canto; clicar/tocar nele abre o inventario
 * - quadro do topo com a onda e o tempo do modo hordas
 * - botao de configuracao (substitui o do minimapa)
 * Barras de vida/mana/xp, efeitos, alvo e action bar continuam nos menus
 * proprios do Flare, so reposicionados pelos .txt de menus/.
 */

#ifndef MENU_HUD_H
#define MENU_HUD_H

#include "CommonIncludes.h"
#include "Menu.h"
#include "Utils.h"

class Sprite;
class WidgetButton;
class WidgetLabel;

class MenuHUD : public Menu {
public:
	MenuHUD();
	~MenuHUD();

	void align();
	void logic();
	void render();

	// chamado todo frame pelo GameStatePlay
	void setHorde(bool active, int wave, float seconds);

	bool isWithin(const Point& mouse);

	bool clicked_portrait;
	bool clicked_config;

private:
	void loadPortrait();

	// retrato: a imagem vem de pc->stats.gfx_portrait, redimensionada pra portrait_size
	Rect portrait_rect;
	Point portrait_base;
	int portrait_size;
	int portrait_align;
	Point portrait_offset; // posicao do retrato dentro da moldura
	std::string portrait_loaded;
	Sprite *portrait;
	Sprite *portrait_frame;

	Point level_pos; // relativo ao retrato
	Sprite *level_badge;
	WidgetLabel *label_level;

	Rect horde_rect;
	Rect horde_base;
	int horde_align;
	Sprite *horde_box;
	WidgetLabel *label_horde;
	bool horde_active;
	int horde_wave;
	float horde_seconds;

	WidgetButton *button_config;
};

#endif
