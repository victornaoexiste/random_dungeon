/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2012 Stefan Beller
Copyright © 2013 Kurt Rinnert
Copyright © 2014 Henrik Andersson
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
 * GameStateNew
 *
 * Handle player choices when starting a new game
 * (e.g. character appearance)
 */

#include "Avatar.h"
#include "EngineSettings.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "GameSlotPreview.h"
#include "GameStateNew.h"
#include "HeroColors.h"
#include "StatBlock.h"
#include "UtilsFileSystem.h"
#include "ModManager.h"
#include "GameStateLoad.h"
#include "GameStatePlay.h"
#include "InputState.h"
#include "ItemManager.h"
#include "MessageEngine.h"
#include "RenderDevice.h"
#include "SaveLoad.h"
#include "Settings.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "Utils.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetCheckBox.h"
#include "WidgetInput.h"
#include "WidgetLabel.h"
#include "WidgetListBox.h"
#include "WidgetTooltip.h"

GameStateNew::GameStateNew()
	: GameState()
	, current_option(0)
	, portrait_image(NULL)
	, portrait_border(NULL)
	, class_tip_align(Utils::ALIGN_FRAME_TOPLEFT)
	, show_classlist(true)
	, show_class_tip(false)
	, show_randomize(true)
	, show_permadeath(true)
	, modified_name(false)
	, delete_items(true)
	, random_option(false)
	, random_class(false)
	, game_slot(0)
{
	// set up buttons
	button_exit = new WidgetButton(WidgetButton::DEFAULT_FILE);
	button_exit->setLabel(msg->get("Cancel"));

	button_create = new WidgetButton(WidgetButton::DEFAULT_FILE);
	button_create->setLabel(msg->get("Create"));
	button_create->enabled = false;
	button_create->refresh();

	button_prev = new WidgetButton(WidgetButton::DIR_LEFT_FILE);
	button_next = new WidgetButton(WidgetButton::DIR_RIGHT_FILE);

	// colour selectors: rows of [<] swatch name [>] in the middle column
	{
		const char *names[3] = { "Skin", "Hair", "Clothes" };
		const char *env = getenv("RD_NEW_COLORS"); // test hook: preselect "skin,hair,cloth" indices
		std::string preset = env ? env : "";
		for (int k = 0; k < 3; ++k) {
			button_color_prev[k] = new WidgetButton("images/menus/buttons/left.png");
			button_color_next[k] = new WidgetButton("images/menus/buttons/right.png");
			button_color_prev[k]->setBasePos(612, 548 + k * 56, Utils::ALIGN_FRAME_TOPLEFT);
			button_color_next[k]->setBasePos(912, 548 + k * 56, Utils::ALIGN_FRAME_TOPLEFT);
			label_color[k] = new WidgetLabel();
			label_color[k]->setFont("font_small");
			label_color[k]->setJustify(FontEngine::JUSTIFY_LEFT);
			label_color[k]->setVAlign(LabelInfo::VALIGN_CENTER);
			label_color[k]->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));
			label_color[k]->setText(msg->get(names[k]));
			color_index[k] = preset.empty() ? 0 : std::max(0, Parse::popFirstInt(preset));
			if (color_index[k] >= static_cast<int>(HeroColors::options(k).size()))
				color_index[k] = 0;
		}
	}
	preview_stats = new StatBlock();
	preview = new GameSlotPreview();
	preview->setStatBlock(preview_stats);
	preview_turn.setDuration(settings->max_frames_per_sec * 2);

	button_randomize = new WidgetButton(WidgetButton::DEFAULT_FILE);
	button_randomize->setLabel(msg->get("Randomize"));

	input_name = new WidgetInput(WidgetInput::DEFAULT_FILE);
	input_name->max_length = 20;

	button_permadeath = new WidgetCheckBox(WidgetCheckBox::DEFAULT_FILE);
	if (eset->death_penalty.permadeath) {
		button_permadeath->enabled = false;
		button_permadeath->setChecked(true);
	}

	class_list = new WidgetListBox(12, WidgetListBox::DEFAULT_FILE);
	class_list->can_deselect = false;

	class_tip = new WidgetTooltip();

	// set up labels
	label_portrait = new WidgetLabel();
	label_portrait->setText(msg->get("Choose a Portrait"));
	label_portrait->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_name = new WidgetLabel();
	label_name->setText(msg->get("Choose a Name"));
	label_name->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_permadeath = new WidgetLabel();
	label_permadeath->setText(msg->get("Permadeath?"));
	label_permadeath->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	label_classlist = new WidgetLabel();
	label_classlist->setText(msg->get("Choose a Class"));
	label_classlist->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	// Some widgets default to being aligned to the menu frame
	button_prev->alignment = Utils::ALIGN_FRAME_TOPLEFT;
	button_next->alignment = Utils::ALIGN_FRAME_TOPLEFT;
	button_permadeath->alignment = Utils::ALIGN_FRAME_TOPLEFT;
	button_randomize->alignment = Utils::ALIGN_FRAME_TOPLEFT;
	input_name->alignment = Utils::ALIGN_FRAME_TOPLEFT;
	class_list->alignment = Utils::ALIGN_FRAME_TOPLEFT;

	// Read positions from config file
	FileParser infile;

	// @CLASS GameStateNew: Layout|Description of menus/gamenew.txt
	if (infile.open("menus/gamenew.txt", FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) {
		while (infile.next()) {
			// @ATTR button_prev|int, int, alignment : X, Y, Alignment|Position of button to choose the previous preset hero.
			if (infile.key == "button_prev") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				button_prev->setBasePos(x, y, a);
			}
			// @ATTR button_next|int, int, alignment : X, Y, Alignment|Position of button to choose the next preset hero.
			else if (infile.key == "button_next") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				button_next->setBasePos(x, y, a);
			}
			// @ATTR button_exit|int, int, alignment : X, Y, Alignment|Position of "Cancel" button.
			else if (infile.key == "button_exit") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_exit->setBasePos(x, y, a);
			}
			// @ATTR button_create|int, int, alignment : X, Y, Alignment|Position of "Create" button.
			else if (infile.key == "button_create") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val));
				button_create->setBasePos(x, y, a);
			}
			// @ATTR button_permadeath|int, int, alignment : X, Y, Alignment|Position of checkbox for toggling permadeath.
			else if (infile.key == "button_permadeath") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				button_permadeath->setBasePos(x, y, a);
			}
			// @ATTR button_randomize|int, int, alignment : X, Y, Alignment|Position of the "Randomize" button.
			else if (infile.key == "button_randomize") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				button_randomize->setBasePos(x, y, a);
			}
			// @ATTR name_input|int, int, alignment : X, Y, Alignment|Position of the hero name textbox.
			else if (infile.key == "name_input") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				input_name->setBasePos(x, y, a);
			}
			// @ATTR portrait_label|label|Label for the "Choose a Portrait" text.
			else if (infile.key == "portrait_label") {
				label_portrait->setFromLabelInfo(Parse::popLabelInfo(infile.val));
			}
			// @ATTR name_label|label|Label for the "Choose a Name" text.
			else if (infile.key == "name_label") {
				label_name->setFromLabelInfo(Parse::popLabelInfo(infile.val));
			}
			// @ATTR permadeath_label|label|Label for the "Permadeath?" text.
			else if (infile.key == "permadeath_label") {
				label_permadeath->setFromLabelInfo(Parse::popLabelInfo(infile.val));
			}
			// @ATTR classlist_label|label|Label for the "Choose a Class" text.
			else if (infile.key == "classlist_label") {
				label_classlist->setFromLabelInfo(Parse::popLabelInfo(infile.val));
			}
			// @ATTR classlist_height|int|Number of visible rows for the class list widget.
			else if (infile.key == "classlist_height") {
				class_list->setHeight(Parse::popFirstInt(infile.val));
			}
			// @ATTR portrait|rectangle|Position and dimensions of the portrait image.
			else if (infile.key == "portrait") {
				portrait_pos = Parse::toRect(infile.val);
			}
			// @ATTR class_list|int, int, alignment : X, Y, Alignment|Position of the class list.
			else if (infile.key == "class_list") {
				int x = Parse::popFirstInt(infile.val);
				int y = Parse::popFirstInt(infile.val);
				int a = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
				class_list->setBasePos(x, y, a);
			}
			// @ATTR show_classlist|bool|Allows hiding the class list.
			else if (infile.key == "show_classlist") {
				show_classlist = Parse::toBool(infile.val);
			}
			// @ATTR class_tip|int, int, alignment : X, Y, Alignment|Position of the class description tooltip.
			else if (infile.key == "class_tip") {
				class_tip_pos.x = Parse::popFirstInt(infile.val);
				class_tip_pos.y = Parse::popFirstInt(infile.val);
				class_tip_align = Parse::toAlignment(Parse::popFirstString(infile.val), Utils::ALIGN_FRAME_TOPLEFT);
			}
			// @ATTR show_class_tip|bool|When true, shows a persistent tooltip with the description of the selected class. Defaults to false.
			else if (infile.key == "show_class_tip") {
				show_class_tip = Parse::toBool(infile.val);
			}
			// @ATTR show_randomize|bool|Toggles the visibility of the "Randomize" button.
			else if (infile.key == "show_randomize") {
				show_randomize = Parse::toBool(infile.val);
			}
			// @ATTR show_permadeath|bool|Toggles the visibility of the "Permadeath?" checkbox.
			else if (infile.key == "show_permadeath") {
				show_permadeath = Parse::toBool(infile.val);
			}
			// @ATTR random_option|bool|Initially picks a random character option (aka portrait/name).
			else if (infile.key == "random_option") {
				random_option = Parse::toBool(infile.val);
			}
			// @ATTR random_class|bool|Initially picks a random character class.
			else if (infile.key == "random_class") {
				random_class = Parse::toBool(infile.val);
			}
			// @ATTR show_frame_background|bool|If true, the frame background image is drawn behind the menu.
			else if (infile.key == "show_frame_background") {
				has_frame_background = Parse::toBool(infile.val);
			}
			else {
				infile.error("GameStateNew: '%s' is not a valid key.", infile.key.c_str());
			}
		}
		infile.close();
	}

	// set up class list
	for (unsigned i = 0; i < eset->hero_classes.list.size(); i++) {
		if (show_class_tip)
			class_list->append(msg->get(eset->hero_classes.list[i].name), "");
		else
			class_list->append(msg->get(eset->hero_classes.list[i].name), getClassTooltip(i));
	}

	if (!eset->hero_classes.list.empty()) {
		int class_index = 0;
		if (random_class)
			class_index = static_cast<int>(rand() % eset->hero_classes.list.size());

		class_list->select(class_index);

		if (show_class_tip) {
			class_tip_data.clear();
			class_tip_data.addText(getClassTooltip(class_index));
		}
	}

	loadGraphics();
	loadOptions("hero_options.txt");

	if (random_option)
		setHeroOption(OPTION_RANDOM);
	else
		setHeroOption(OPTION_CURRENT);

	// Set up tab list
	tablist.add(button_exit);
	tablist.add(button_create);
	tablist.add(input_name);

	if (show_permadeath) {
		tablist.add(button_permadeath);
	}

	if (show_randomize) {
		tablist.add(button_randomize);
	}

	tablist.add(button_prev);
	tablist.add(button_next);
	if (show_classlist) {
		tablist.add(class_list);
	}

	refreshWidgets();

	render_device->setBackgroundColor(Color(0,0,0,0));
}

void GameStateNew::loadGraphics() {
	Image *graphics;

	graphics = render_device->loadImage("images/menus/portrait_border.png", RenderDevice::ERROR_NORMAL);
	if (graphics) {
		portrait_border = graphics->createSprite();
		graphics->unref();
	}
}

void GameStateNew::loadPortrait(const std::string& portrait_filename) {

	Image *graphics;

	if (portrait_image)
		delete portrait_image;

	portrait_image = NULL;
	graphics = render_device->loadImage(portrait_filename, RenderDevice::ERROR_NORMAL);
	if (graphics) {
		portrait_image = graphics->createSprite();
		portrait_image->setDestFromRect(portrait_pos);
		graphics->unref();
	}
}

/**
 * Load body type "base" and portrait/head "portrait" options from a config file
 *
 * @param filename File containing entries for option=base,look
 */
void GameStateNew::loadOptions(const std::string& filename) {
	FileParser fin;
	// @CLASS GameStateNew: Hero options|Description of engine/hero_options.txt
	if (!fin.open("engine/" + filename, FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) return;

	int cur_index;
	while (fin.next()) {
		// @ATTR option|int, string, string, filename, string : Index, Base, Head, Portrait, Name|A default body, head, portrait, and name for a hero.
		if (fin.key == "option") {
			cur_index = std::max(0, Parse::popFirstInt(fin.val));

			if (static_cast<size_t>(cur_index + 1) > hero_options.size()) {
				hero_options.resize(cur_index + 1);
				all_options.push_back(cur_index);
			}

			hero_options[cur_index].base = Parse::popFirstString(fin.val);
			hero_options[cur_index].head = Parse::popFirstString(fin.val);
			hero_options[cur_index].portrait = Parse::popFirstString(fin.val);
			hero_options[cur_index].name = msg->get(Parse::popFirstString(fin.val));
		}
	}
	fin.close();

	if (hero_options.empty()) {
		hero_options.resize(1);
	}

	std::sort(all_options.begin(), all_options.end());
}

/**
 * If the name text box is empty or hasn't been user-modified, set the name
 *
 * @param default_name The name we want to use for the hero
 */
void GameStateNew::setName(const std::string& default_name) {
	if (input_name->getText() == "" || !modified_name) {
		input_name->setText(default_name);
		modified_name = false;
	}
}

void GameStateNew::setHeroOption(int dir) {
	std::vector<int> *available_options = &all_options;

	// get the available options from the currently selected class
	int class_index;
	if ( (class_index = class_list->getSelected()) != -1) {
		if (static_cast<size_t>(class_index) < eset->hero_classes.list.size() && !eset->hero_classes.list[class_index].options.empty()) {
			available_options = &(eset->hero_classes.list[class_index].options);
		}
	}

	if (dir == OPTION_CURRENT) {
		// don't change current_option unless required
		if (std::find(available_options->begin(), available_options->end(), current_option) == available_options->end()) {
			if (random_option && available_options != &all_options) {
				size_t rand_index = rand() % available_options->size();
				current_option = available_options->at(rand_index);
			}
			else {
				current_option = available_options->front();
			}
		}
	}
	else if (dir == OPTION_NEXT) {
		// increment current_option
		std::vector<int>::iterator it = std::find(available_options->begin(), available_options->end(), current_option);
		if (it == available_options->end()) {
			current_option = available_options->front();
		}
		else {
			++it;
			if (it != available_options->end())
				current_option = (*it);
			else
				current_option = available_options->front();
		}
	}
	else if (dir == OPTION_PREV) {
		// decrement current_option
		std::vector<int>::iterator it = std::find(available_options->begin(), available_options->end(), current_option);
		if (it == available_options->begin()) {
			current_option = available_options->back();
		}
		else {
			--it;
			current_option = (*it);
		}
	}
	else if (dir == OPTION_RANDOM && !available_options->empty()) {
		size_t rand_index = rand() % available_options->size();
		current_option = available_options->at(rand_index);
	}

	loadPortrait(hero_options[current_option].portrait);
	setName(hero_options[current_option].name);
}

void GameStateNew::logic() {

	if (inpt->window_resized)
		refreshWidgets();

	if (!input_name->edit_mode)
		tablist.logic();

	input_name->logic();

	if (show_permadeath) {
		button_permadeath->checkClick();
	}

	if (show_classlist && class_list->checkClick()) {
		setHeroOption(OPTION_CURRENT);

		if (show_class_tip) {
			class_tip_data.clear();
			class_tip_data.addText(getClassTooltip(class_list->getSelected()));
		}
	}

	// require character name
	if (input_name->getText() == "") {
		if (button_create->enabled) {
			button_create->enabled = false;
			button_create->refresh();
		}
	}
	else {
		if (!button_create->enabled) {
			button_create->enabled = true;
			button_create->refresh();
		}
	}

	if (!input_name->edit_mode && !inpt->usingMouse() && tablist.getCurrent() == -1) {
		if (button_create->enabled)
			tablist.setCurrent(button_create);
		else
			tablist.setCurrent(button_exit);
	}

	if ((inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL]) || button_exit->checkClick()) {
		if (inpt->pressing[Input::CANCEL])
			inpt->lock[Input::CANCEL] = true;
		delete_items = false;
		showLoading();
		setRequestedGameState(new GameStateLoad());
	}

	// test hook (see GameSwitcher): pick class/option/colours/mode, then create
	bool auto_create = false;
	if (getenv("RD_AUTO_NEW")) {
		static int frames = 0;
		if (++frames == 20) {
			std::string spec = getenv("RD_AUTO_NEW");
			Parse::popFirstInt(spec); // slot
			int cls = Parse::popFirstInt(spec);
			int opt = Parse::popFirstInt(spec);
			class_list->select(cls);
			if (opt >= 0 && static_cast<size_t>(opt) < hero_options.size()) {
				current_option = opt;
				loadPortrait(hero_options[current_option].portrait);
				setName(hero_options[current_option].name);
			}
			for (int k = 0; k < 3; ++k) {
				int c = Parse::popFirstInt(spec);
				if (c >= 0 && c < static_cast<int>(HeroColors::options(k).size()))
					color_index[k] = c;
			}
			settings->game_mode = Parse::popFirstString(spec);
			if (settings->game_mode == "world") settings->game_mode = "";
			Utils::logInfo("AutoNew: class %d option %d colours %d,%d,%d mode '%s'", cls, opt, color_index[0], color_index[1], color_index[2], settings->game_mode.c_str());
		}
		if (frames == 60) {
			render_device->screenshot_request = std::string(getenv("RD_UI_SHOTS") ? getenv("RD_UI_SHOTS") : ".") + "/new_character.png";
		}
		auto_create = (frames == 70);
	}

	if (button_create->checkClick() || auto_create) {
		// start the new game
		inpt->lock_all = true;
		delete_items = false;
		showLoading();
		GameStatePlay* play = new GameStatePlay();
		Avatar *avatar = pc;
		avatar->stats.gfx_base = hero_options[current_option].base;
		avatar->stats.gfx_head = hero_options[current_option].head;
		avatar->stats.gfx_portrait = hero_options[current_option].portrait;
		avatar->stats.color_skin = HeroColors::options(HeroColors::SKIN)[color_index[0]].hex;
		avatar->stats.color_hair = HeroColors::options(HeroColors::HAIR)[color_index[1]].hex;
		avatar->stats.color_cloth = HeroColors::options(HeroColors::CLOTH)[color_index[2]].hex;
		avatar->stats.checkGFXPaths();
		avatar->stats.name = input_name->getText();
		avatar->stats.permadeath = button_permadeath->isChecked();
		save_load->setGameSlot(game_slot);
		play->resetGame();
		save_load->loadClass(class_list->getSelected());
		setRequestedGameState(play);
	}

	// scroll through portrait options
	if (button_next->checkClick()) {
		setHeroOption(OPTION_NEXT);
	}
	else if (button_prev->checkClick()) {
		setHeroOption(OPTION_PREV);
	}

	if (show_randomize && button_randomize->checkClick()) {
		if (!eset->hero_classes.list.empty()) {
			int class_index = static_cast<int>(rand() % eset->hero_classes.list.size());
			class_list->select(class_index);

			if (show_class_tip) {
				class_tip_data.clear();
				class_tip_data.addText(getClassTooltip(class_index));
			}
		}
		setHeroOption(OPTION_RANDOM);
	}

	if (input_name->getText() != hero_options[current_option].name)
		modified_name = true;

	// colour selectors
	for (int k = 0; k < 3; ++k) {
		const int n = static_cast<int>(HeroColors::options(k).size());
		if (button_color_prev[k]->checkClick())
			color_index[k] = (color_index[k] + n - 1) % n;
		else if (button_color_next[k]->checkClick())
			color_index[k] = (color_index[k] + 1) % n;
	}
	updateColorLabels();

	// live preview: rebuild when anything it shows changed, turn slowly
	updatePreview();
	preview_turn.tick();
	if (preview_turn.isEnd()) {
		preview_turn.reset(Timer::BEGIN);
		preview_stats->direction = static_cast<unsigned char>((preview_stats->direction + 1) % 8);
		preview->setDirection(preview_stats->direction);
	}
	preview->logic();
}

void GameStateNew::updateColorLabels() {
	const char *names[3] = { "Skin", "Hair", "Clothes" };
	for (int k = 0; k < 3; ++k) {
		const HeroColors::Option& o = HeroColors::options(k)[color_index[k]];
		label_color[k]->setText(msg->get(names[k]) + ": " + (o.hex.empty() ? msg->get("Original") : o.name));
	}
}

/**
 * The preview shows the chosen body/head with the selected class's starting
 * gear, in the chosen colours (see HeroColors).
 */
void GameStateNew::updatePreview() {
	if (hero_options.empty())
		return;
	const HeroOption& opt = hero_options[current_option];
	const int class_index = class_list->getSelected();
	std::string key = opt.base + "/" + opt.head + "/" + Parse::toString(typeid(int), const_cast<int*>(&class_index));
	for (int k = 0; k < 3; ++k)
		key += "/" + HeroColors::options(k)[color_index[k]].hex;
	if (key == preview_key)
		return;
	preview_key = key;

	preview_stats->gfx_base = opt.base;
	preview_stats->gfx_head = opt.head;
	preview_stats->color_skin = HeroColors::options(HeroColors::SKIN)[color_index[0]].hex;
	preview_stats->color_hair = HeroColors::options(HeroColors::HAIR)[color_index[1]].hex;
	preview_stats->color_cloth = HeroColors::options(HeroColors::CLOTH)[color_index[2]].hex;
	preview->setStatBlock(preview_stats);

	std::vector<std::string>& layers = preview->layer_reference_order;
	std::vector<std::string> img_gfx(layers.size());
	for (size_t i = 0; i < layers.size(); ++i) {
		if (Filesystem::fileExists(mods->locate("animations/avatar/" + opt.base + "/default_" + layers[i] + ".txt")))
			img_gfx[i] = "default_" + layers[i];
		else if (layers[i] == "head")
			img_gfx[i] = opt.head;
	}
	// the class gear needs item data; GameStateLoad normally leaves us its ItemManager
	if (!items)
		items = new ItemManager();
	if (class_index >= 0 && static_cast<size_t>(class_index) < eset->hero_classes.list.size()) {
		std::string equipment = eset->hero_classes.list[class_index].equipment;
		while (!equipment.empty()) {
			ItemID id = Parse::toItemID(Parse::popFirstString(equipment));
			if (!items->isValid(id))
				continue;
			const std::string type = items->getItemType(items->items[id]->type).id;
			for (size_t i = 0; i < layers.size(); ++i) {
				if (layers[i] == type && !items->items[id]->gfx.empty())
					img_gfx[i] = items->items[id]->gfx;
			}
		}
	}
	preview->loadGraphics(img_gfx);
	preview->setDirection(preview_stats->direction);
}

void GameStateNew::refreshWidgets() {
	button_exit->setPos(0, 0);
	button_create->setPos(0, 0);

	button_prev->setPos(0, 0);
	button_next->setPos(0, 0);
	button_permadeath->setPos(0, 0);
	button_randomize->setPos(0, 0);
	class_list->setPos(0, 0);
	for (int k = 0; k < 3; ++k) {
		button_color_prev[k]->setPos(0, 0);
		button_color_next[k]->setPos(0, 0);
	}

	label_portrait->setPos((settings->view_w - eset->resolutions.frame_w)/2, (settings->view_h - eset->resolutions.frame_h)/2);
	label_name->setPos((settings->view_w - eset->resolutions.frame_w)/2, (settings->view_h - eset->resolutions.frame_h)/2);
	label_permadeath->setPos((settings->view_w - eset->resolutions.frame_w)/2, (settings->view_h - eset->resolutions.frame_h)/2);
	label_classlist->setPos((settings->view_w - eset->resolutions.frame_w)/2, (settings->view_h - eset->resolutions.frame_h)/2);

	input_name->setPos(0, 0);
}

void GameStateNew::render() {
	const int fx = (settings->view_w - eset->resolutions.frame_w) / 2;
	const int fy = (settings->view_h - eset->resolutions.frame_h) / 2;

	// colour customisation: preview + selectors with a swatch each
	preview->setPos(Point(fx + 790, fy + 500));
	preview->render();
	for (int k = 0; k < 3; ++k) {
		button_color_prev[k]->render();
		button_color_next[k]->render();
		const int cy = fy + 548 + k * 56 + 21;
		const std::string& hex = HeroColors::options(k)[color_index[k]].hex;
		const int sx = fx + 674;
		render_device->drawRectangle(Point(sx - 1, cy - 12), Point(sx + 22, cy + 11), Color(8, 4, 5, 255));
		render_device->drawRectangle(Point(sx, cy - 11), Point(sx + 21, cy + 10), Color(222, 170, 44, 255));
		if (!hex.empty()) {
			long v = strtol(hex.c_str(), NULL, 16);
			Color c(static_cast<Uint8>((v >> 16) & 255), static_cast<Uint8>((v >> 8) & 255), static_cast<Uint8>(v & 255), 255);
			for (int y = cy - 9; y <= cy + 8; ++y)
				render_device->drawLine(sx + 2, y, sx + 19, y, c);
		}
		else {
			render_device->drawLine(sx + 2, cy + 8, sx + 19, cy - 9, Color(222, 170, 44, 255));
		}
		label_color[k]->setPos(fx + 704, cy);
		label_color[k]->render();
	}

	// display buttons
	button_exit->render();
	button_create->render();
	button_prev->render();
	button_next->render();
	input_name->render();

	if (show_permadeath)
		button_permadeath->render();

	if (show_randomize) {
		button_randomize->render();
	}

	// display portrait option
	Rect src;
	Rect dest;

	src.w = dest.w = portrait_pos.w;
	src.h = dest.h = portrait_pos.h;
	src.x = src.y = 0;
	dest.x = portrait_pos.x + (settings->view_w - eset->resolutions.frame_w)/2;
	dest.y = portrait_pos.y + (settings->view_h - eset->resolutions.frame_h)/2;

	if (portrait_image) {
		portrait_image->setClipFromRect(src);
		portrait_image->setDestFromRect(dest);
		render_device->render(portrait_image);
		portrait_border->setClipFromRect(src);
		portrait_border->setDestFromRect(dest);
		render_device->render(portrait_border);
	}

	// display labels
	label_portrait->render();
	label_name->render();

	if (show_permadeath)
		label_permadeath->render();

	// display class list
	if (show_classlist) {
		label_classlist->render();
		class_list->render();

		if (show_class_tip && !class_tip_data.isEmpty()) {
			class_tip->prerender(class_tip_data, Point(class_tip_pos.x, class_tip_pos.y), TooltipData::STYLE_ABSOLUTE);
			Rect temp = class_tip->bounds;
			Utils::alignToScreenEdge(Utils::ALIGN_FRAME_TOPLEFT, &temp);
			class_tip->render(class_tip_data, Point(temp.x, temp.y), TooltipData::STYLE_ABSOLUTE);
		}
	}

}

std::string GameStateNew::getClassTooltip(int index) {
	if (static_cast<size_t>(index) >= eset->hero_classes.list.size())
		return "";

	std::string tooltip;
	if (eset->hero_classes.list[index].description != "") tooltip += msg->get(eset->hero_classes.list[index].description);
	return tooltip;
}

GameStateNew::~GameStateNew() {
	if (portrait_image)
		delete portrait_image;

	if (portrait_border)
		delete portrait_border;

	if (delete_items) {
		delete items;
		items = NULL;
	}

	delete button_exit;
	delete button_create;
	delete button_next;
	delete button_prev;
	delete button_randomize;
	delete label_portrait;
	delete label_name;
	delete input_name;
	delete button_permadeath;
	delete label_permadeath;
	delete label_classlist;
	delete class_list;
	delete class_tip;
	for (int k = 0; k < 3; ++k) {
		delete button_color_prev[k];
		delete button_color_next[k];
		delete label_color[k];
	}
	delete preview;
	delete preview_stats;
}
