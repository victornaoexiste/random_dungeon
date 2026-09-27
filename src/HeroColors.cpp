/*
Random Dungeon: hero colour customisation (see HeroColors.h).
*/

#include "HeroColors.h"

#include "FileParser.h"
#include "StatBlock.h"
#include "UtilsParsing.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>

namespace {
	std::vector<HeroColors::Option> opts[HeroColors::KIND_COUNT];
	bool loaded = false;

	void load() {
		loaded = true;
		const char *keys[HeroColors::KIND_COUNT] = { "skin", "hair", "cloth" };
		for (int k = 0; k < HeroColors::KIND_COUNT; ++k) {
			HeroColors::Option o;
			o.name = "Original";
			opts[k].push_back(o);
		}
		FileParser infile;
		// @CLASS HeroColors|Description of engine/hero_colors.txt
		if (infile.open("engine/hero_colors.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE)) {
			while (infile.next()) {
				for (int k = 0; k < HeroColors::KIND_COUNT; ++k) {
					// @ATTR skin|repeatable(string, string) : Name, Hex colour|A skin tone option ("rrggbb"). hair and cloth work the same way.
					if (infile.key == keys[k]) {
						HeroColors::Option o;
						o.name = Parse::popFirstString(infile.val);
						o.hex = Parse::popFirstString(infile.val);
						if (o.hex.size() == 6)
							opts[k].push_back(o);
					}
				}
			}
			infile.close();
		}
	}

	struct HLS { float h, l, s; };

	HLS toHLS(float r, float g, float b) {
		float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
		HLS o;
		o.l = (mx + mn) / 2.f;
		if (mx == mn) { o.h = 0; o.s = 0; return o; }
		float d = mx - mn;
		o.s = o.l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
		if (mx == r) o.h = (g - b) / d + (g < b ? 6.f : 0.f);
		else if (mx == g) o.h = (b - r) / d + 2.f;
		else o.h = (r - g) / d + 4.f;
		o.h /= 6.f;
		return o;
	}

	float hue2rgb(float p, float q, float t) {
		if (t < 0) t += 1;
		if (t > 1) t -= 1;
		if (t < 1.f / 6) return p + (q - p) * 6 * t;
		if (t < 0.5f) return q;
		if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
		return p;
	}

	void fromHLS(const HLS& c, float& r, float& g, float& b) {
		if (c.s <= 0) { r = g = b = c.l; return; }
		float q = c.l < 0.5f ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s;
		float p = 2 * c.l - q;
		r = hue2rgb(p, q, c.h + 1.f / 3);
		g = hue2rgb(p, q, c.h);
		b = hue2rgb(p, q, c.h - 1.f / 3);
	}

	float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

	bool parseHex(const std::string& hex, HLS& out) {
		if (hex.size() != 6) return false;
		long v = strtol(hex.c_str(), NULL, 16);
		out = toHLS(static_cast<float>((v >> 16) & 255) / 255.f, static_cast<float>((v >> 8) & 255) / 255.f, static_cast<float>(v & 255) / 255.f);
		return true;
	}

	bool isSkinHue(const HLS& c) {
		return (c.h < 0.13f || c.h > 0.95f) && c.s < 0.75f;
	}

	// exposed skin inside a clothing layer (arms out of sleeves, etc.):
	// stricter than isSkinHue so leather (darker) still gets dyed
	bool isSkinStrict(const HLS& c) {
		return c.h > 0.02f && c.h < 0.11f && c.s > 0.25f && c.s < 0.65f && c.l > 0.22f && c.l < 0.8f;
	}
}

const std::vector<HeroColors::Option>& HeroColors::options(int kind) {
	if (!loaded) load();
	return opts[kind];
}

std::string HeroColors::animName(const StatBlock *stats, const std::string& layer_type, const std::string& gfx) {
	std::string name = "animations/avatar/" + stats->gfx_base + "/" + gfx + ".txt";
	std::string spec;
	const bool body = gfx.compare(0, 8, "default_") == 0;
	const bool hair_head = (layer_type == "head" && gfx == stats->gfx_head);
	const bool clothing = !body && (layer_type == "chest" || layer_type == "legs" || layer_type == "feet" || layer_type == "hands");

	if ((body || hair_head) && !stats->color_skin.empty())
		spec += "S=" + stats->color_skin;
	if (hair_head && !stats->color_hair.empty())
		spec += std::string(spec.empty() ? "" : ",") + "H=" + stats->color_hair;
	if (clothing && !stats->color_cloth.empty()) {
		spec += std::string(spec.empty() ? "" : ",") + "C=" + stats->color_cloth;
		// skin showing through the clothes follows the chosen skin tone
		if (!stats->color_skin.empty())
			spec += ",S=" + stats->color_skin;
	}

	return spec.empty() ? name : name + "|" + spec;
}

bool HeroColors::split(const std::string& filename, std::string& base, std::string& spec) {
	size_t bar = filename.find('|');
	if (bar == std::string::npos) {
		base = filename;
		spec.clear();
		return false;
	}
	base = filename.substr(0, bar);
	spec = filename.substr(bar + 1);
	return true;
}

void HeroColors::recolor(SDL_Surface *surface, const std::string& spec) {
	if (!surface || surface->format->format != SDL_PIXELFORMAT_ARGB8888)
		return;

	HLS skin, hair, cloth;
	bool has_skin = false, has_hair = false, has_cloth = false;
	std::string rest = spec;
	while (!rest.empty()) {
		std::string item = Parse::popFirstString(rest);
		if (item.size() < 3 || item[1] != '=') continue;
		if (item[0] == 'S') has_skin = parseHex(item.substr(2), skin);
		else if (item[0] == 'H') has_hair = parseHex(item.substr(2), hair);
		else if (item[0] == 'C') has_cloth = parseHex(item.substr(2), cloth);
	}
	if (!has_skin && !has_hair && !has_cloth)
		return;

	if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
	Uint8 *row = static_cast<Uint8*>(surface->pixels);
	for (int y = 0; y < surface->h; ++y, row += surface->pitch) {
		Uint32 *px = reinterpret_cast<Uint32*>(row);
		for (int x = 0; x < surface->w; ++x) {
			Uint32 p = px[x];
			Uint32 a = p >> 24;
			if (a == 0) continue;
			HLS c = toHLS(static_cast<float>((p >> 16) & 255) / 255.f, static_cast<float>((p >> 8) & 255) / 255.f, static_cast<float>(p & 255) / 255.f);
			HLS n = c;
			bool changed = false;

			if (has_hair && c.l < 0.24f) {
				// hair: dark pixels of the head; keep their shading, move to the target lightness
				float t = c.l / 0.24f;
				n.h = hair.h;
				n.s = hair.s;
				n.l = clamp01(hair.l * (0.35f + 0.9f * t));
				changed = true;
			}
			else if (has_skin && (has_cloth ? isSkinStrict(c) : isSkinHue(c)) && (!has_hair || c.l >= 0.24f)) {
				// skin: keep relative shading, take the tone's hue/lightness
				n.h = skin.h;
				n.s = clamp01(skin.s * std::max(0.4f, std::min(1.3f, c.s / 0.45f)));
				n.l = clamp01(c.l * skin.l / 0.42f);
				changed = true;
			}
			else if (has_cloth && !isSkinStrict(c) && !(c.s < 0.15f && c.l > 0.65f) && c.l > 0.08f) {
				// clothes: dye everything but near-white trims and near-black straps
				n.h = cloth.h;
				n.s = clamp01(cloth.s * std::max(0.5f, std::min(1.2f, c.s / 0.4f)));
				n.l = clamp01(c.l * cloth.l / 0.40f);
				changed = true;
			}

			if (changed) {
				float r, g, b;
				fromHLS(n, r, g, b);
				px[x] = (a << 24) | (static_cast<Uint32>(r * 255.f + 0.5f) << 16) | (static_cast<Uint32>(g * 255.f + 0.5f) << 8) | static_cast<Uint32>(b * 255.f + 0.5f);
			}
		}
	}
	if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}
