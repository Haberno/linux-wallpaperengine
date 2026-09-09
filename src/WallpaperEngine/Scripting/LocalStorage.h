#pragma once

#include <filesystem>
#include <string>

#include "quickjs.h"

namespace WallpaperEngine::Scripting {
/** Install the JSON storage transport consumed by builtins.js. Values are scoped
 *  to a project and output; the global scope is shared by that project's outputs.
 *  An explicit state directory lets tests avoid the user's persistent storage. */
void installLocalStorage (
    JSContext* context, JSValueConst global, const std::string& project, const std::string& screen,
    const std::filesystem::path& stateDirectory = {}
);
}
