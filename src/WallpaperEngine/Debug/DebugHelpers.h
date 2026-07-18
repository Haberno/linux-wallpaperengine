#pragma once

#include <string>

namespace WallpaperEngine::Debug {
/**
 * Centralized home for permanently-shipped debug hooks. Every helper here must
 * be a strict no-op unless its environment variable is set. The ledger of all
 * hooks and their triggers lives in src/WallpaperEngine/Debug/README.md
 */

/**
 * Writes content to $<envVar>/<name>.<counter><extension> and logs the path.
 * Slashes in name are flattened so the result stays a single file.
 *
 * @return The written path, or an empty string when envVar is unset (no-op)
 */
std::string dumpText (const char* envVar, std::string name, const std::string& extension, const std::string& content);
} // namespace WallpaperEngine::Debug
