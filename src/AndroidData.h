/*
Random Dungeon: game data shipped inside the Android APK.

The engine reads its data with plain file I/O, which can't see inside an
APK. So the APK carries the mods as assets (packed by
tools/android_pack_data.py, listed in assets/rd_manifest.txt) and on the
first launch -- or after an update changes the manifest's first line --
they are copied to the app's own storage (settings->path_data), which is
where the engine looks for them.
*/

#ifndef ANDROID_DATA_H
#define ANDROID_DATA_H

#include <string>

namespace AndroidData {
	// Copies the APK's game data to dest (a directory ending in '/') when
	// it's missing or outdated. Returns false if something couldn't be copied.
	bool extractIfNeeded(const std::string& dest);
}

#endif
