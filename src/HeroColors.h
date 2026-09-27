/*
Random Dungeon: hero colour customisation (skin, hair, clothes).

The hero sprites are painted layers: body parts (default_*, mostly skin),
the head (face + hair in one image) and one image per piece of clothing.
A layer is recoloured when it's loaded: its animation name gets a suffix
like "animations/avatar/female/head_long.txt|S=f2cdb0,H=1a1614", the suffix
is carried to its image filename, and the render device recolours the
image before making the texture (see recolor()). The render cache keys on
the full name, so every colour combination is loaded once and shared.

 - skin (S): body layers and the face part of the head (skin-toned pixels)
 - hair (H): the dark pixels of the head layer
 - clothes (C): chest/legs/feet/hands items, keeping their shading
Choices come from engine/hero_colors.txt (name + hex per option).
*/

#ifndef HERO_COLORS_H
#define HERO_COLORS_H

#include <string>
#include <vector>

class StatBlock;
struct SDL_Surface;

namespace HeroColors {
	class Option {
	public:
		std::string name;
		std::string hex; // "rrggbb", empty = the sprite's own colours
	};

	enum { SKIN = 0, HAIR = 1, CLOTH = 2, KIND_COUNT = 3 };

	// options for SKIN / HAIR / CLOTH (first is always "original")
	const std::vector<Option>& options(int kind);

	// animation file for one hero layer, with the colour suffix if any
	std::string animName(const StatBlock *stats, const std::string& layer_type, const std::string& gfx);

	// "file|spec" -> base + spec; false (base = filename) when there's no spec
	bool split(const std::string& filename, std::string& base, std::string& spec);

	// applies a spec ("S=..,H=..,C=..") to a 32-bit ARGB8888 surface
	void recolor(SDL_Surface *surface, const std::string& spec);
}

#endif
