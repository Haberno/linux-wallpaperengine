#include "WallpaperEngine/Render/Objects/CModel.h"

// CEF exposes its own CHECK macro through CModel's renderer includes.
#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::Model::BlendingMode;
using WallpaperEngine::Render::Objects::CModel;

TEST_CASE ("Model passes render opaque before blended while preserving authored order") {
    const std::vector<BlendingMode> modes {
	WallpaperEngine::Data::Model::BlendingMode_Translucent,
	WallpaperEngine::Data::Model::BlendingMode_Normal,
	WallpaperEngine::Data::Model::BlendingMode_Additive,
	WallpaperEngine::Data::Model::BlendingMode_AlphaToCoverage,
	WallpaperEngine::Data::Model::BlendingMode_Translucent,
	WallpaperEngine::Data::Model::BlendingMode_Normal,
    };

    CHECK (CModel::calculatePassRenderPermutation (modes) == std::vector<size_t> { 1, 3, 5, 0, 4, 2 });
}
