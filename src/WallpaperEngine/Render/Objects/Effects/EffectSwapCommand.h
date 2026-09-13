#pragma once

#include <memory>

#include "EffectFBOBindings.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"

namespace WallpaperEngine::Render::Objects::Effects {
/** An authored swap runs between material passes, without consuming a draw target. */
struct EffectSwapCommand {
    size_t beforePass;
    std::shared_ptr<EffectFBOBindings> bindings;
    std::string source;
    std::string target;
    const Data::Model::UserSetting* visible;

    void execute () const {
	if (visible->value->getBool ()) bindings->swap (source, target);
    }
};
}
