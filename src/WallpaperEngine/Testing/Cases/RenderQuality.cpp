#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/FBOProvider.h"

using WallpaperEngine::Render::FBOProvider;

TEST_CASE ("render quality scales scene targets and clamps unsafe factors", "[render][quality]") {
    CHECK (FBOProvider::calculateTargetSize ({ 1920.0f, 1080.0f }, 1.0f) == glm::uvec2 (1920, 1080));
    CHECK (FBOProvider::calculateTargetSize ({ 1920.0f, 1080.0f }, 1.5f) == glm::uvec2 (2880, 1620));
    CHECK (FBOProvider::calculateTargetSize ({ 3.0f, 3.0f }, 0.1f) == glm::uvec2 (2, 2));
    CHECK (FBOProvider::calculateTargetSize ({ 3.0f, 3.0f }, 8.0f) == glm::uvec2 (6, 6));
    CHECK (FBOProvider::calculateTargetSize ({ 0.0f, 0.0f }, 1.0f) == glm::uvec2 (1, 1));
}

TEST_CASE ("protocol-sized render targets ignore supersampling", "[render][quality]") {
    CHECK (FBOProvider::isFixedSizeTarget ("_rt_shadowAtlas"));
    CHECK (FBOProvider::isFixedSizeTarget ("_alias_lightCookie"));
    CHECK_FALSE (FBOProvider::isFixedSizeTarget ("_rt_FullFrameBuffer"));
    CHECK (
	FBOProvider::calculateTargetSize ({ 2048.0f, 2048.0f }, 2.0f, false) == glm::uvec2 (2048, 2048)
    );
}
