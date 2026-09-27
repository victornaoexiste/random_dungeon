/*
Random Dungeon: game data shipped inside the Android APK (see AndroidData.h).
*/

#include "AndroidData.h"

#ifdef __ANDROID__

#include "Utils.h"

#include <SDL.h>

#include <cerrno>
#include <cstdio>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace {
	const char* MANIFEST = "rd_manifest.txt";

	// Reads a whole APK asset (SDL_RWFromFile looks inside the APK for relative paths).
	bool readAsset(const std::string& name, std::vector<char>& out) {
		SDL_RWops *rw = SDL_RWFromFile(name.c_str(), "rb");
		if (!rw)
			return false;
		Sint64 size = SDL_RWsize(rw);
		if (size < 0) {
			SDL_RWclose(rw);
			return false;
		}
		out.resize(static_cast<size_t>(size));
		size_t got = size > 0 ? SDL_RWread(rw, &out[0], 1, static_cast<size_t>(size)) : 0;
		SDL_RWclose(rw);
		return got == static_cast<size_t>(size);
	}

	void makeDirs(const std::string& path) {
		for (size_t i = 1; i < path.size(); ++i) {
			if (path[i] == '/') {
				std::string part = path.substr(0, i);
				if (mkdir(part.c_str(), 0775) != 0 && errno != EEXIST) {}
			}
		}
	}

	bool writeFile(const std::string& path, const std::vector<char>& data) {
		makeDirs(path);
		FILE *f = fopen(path.c_str(), "wb");
		if (!f)
			return false;
		size_t ok = data.empty() ? 0 : fwrite(&data[0], 1, data.size(), f);
		fclose(f);
		return ok == data.size();
	}

	std::string firstLine(const std::vector<char>& data) {
		std::string s(data.begin(), data.end());
		size_t nl = s.find('\n');
		return nl == std::string::npos ? s : s.substr(0, nl);
	}
}

bool AndroidData::extractIfNeeded(const std::string& dest) {
	std::vector<char> manifest;
	if (!readAsset(MANIFEST, manifest)) {
		Utils::logInfo("AndroidData: no packed data in this APK, using %s as-is", dest.c_str());
		return true;
	}
	const std::string stamp = firstLine(manifest);

	// already extracted for this build?
	FILE *f = fopen((dest + MANIFEST).c_str(), "rb");
	if (f) {
		char buf[256] = {0};
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
		std::string have(buf, n);
		size_t nl = have.find('\n');
		if (nl != std::string::npos)
			have = have.substr(0, nl);
		if (have == stamp) {
			Utils::logInfo("AndroidData: data %s already in place", stamp.c_str());
			return true;
		}
	}

	Utils::logInfo("AndroidData: extracting game data %s to %s", stamp.c_str(), dest.c_str());
	std::istringstream lines(std::string(manifest.begin(), manifest.end()));
	std::string line;
	std::getline(lines, line); // stamp
	unsigned copied = 0, failed = 0;
	std::vector<char> data;
	while (std::getline(lines, line)) {
		if (line.empty())
			continue;
		if (readAsset(line, data) && writeFile(dest + line, data))
			++copied;
		else {
			++failed;
			Utils::logError("AndroidData: could not copy %s", line.c_str());
		}
	}
	Utils::logInfo("AndroidData: %u files copied, %u failed", copied, failed);

	// write the manifest last: an interrupted copy is retried next launch
	if (failed == 0)
		writeFile(dest + MANIFEST, manifest);
	return failed == 0;
}

#else

bool AndroidData::extractIfNeeded(const std::string&) {
	return true;
}

#endif
