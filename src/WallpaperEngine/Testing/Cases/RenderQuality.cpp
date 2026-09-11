#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Render/WallpaperState.h"
#include <catch2/catch_approx.hpp>

using WallpaperEngine::Render::FBOProvider;
using WallpaperEngine::Render::WallpaperState;

TEST_CASE ("Scene framing retains fractional canvas bounds at odd output sizes", "[render-quality]") {
    using Scaling = WallpaperState::TextureUVsScaling;
    for (const bool flip : { false, true }) {
	WallpaperState fill (Scaling::ZoomFillUVs, 0);
	fill.updateState ({ 0, 0, 321, 181 }, flip, 64, 40);
	const auto uv = fill.getTextureUVs ();
	CHECK (uv.ustart == Catch::Approx (0));
	CHECK (uv.uend == Catch::Approx (1));
	CHECK ((uv.vstart + uv.vend) / 2 == Catch::Approx (0.5));
	CHECK (std::abs (uv.vend - uv.vstart) == Catch::Approx (181.0 * 64 / (321 * 40)));

	WallpaperState fit (Scaling::ZoomFitUVs, 0);
	fit.updateState ({ 0, 0, 321, 181 }, flip, 64, 40);
	const auto fitted = fit.getTextureUVs ();
	CHECK ((fitted.ustart + fitted.uend) / 2 == Catch::Approx (0.5));
	CHECK (fitted.uend - fitted.ustart == Catch::Approx (321.0 * 40 / (181 * 64)));
	CHECK (std::abs (fitted.vend - fitted.vstart) == Catch::Approx (1));
    }
}

TEST_CASE ("Effect fit preserves aspect ratio and caps simulation resolution", "[render-quality]") {
    CHECK (FBOProvider::calculateTargetSize ({3840, 2160}, 1, true, 512) == glm::uvec2 (512, 288));
    CHECK (FBOProvider::calculateTargetSize ({1080, 1920}, 1, true, 512) == glm::uvec2 (288, 512));
    CHECK (FBOProvider::calculateTargetSize ({100, 50}, 1, true, 512) == glm::uvec2 (100, 50));
    CHECK (FBOProvider::calculateTargetSize ({3840, 2160}, 2, true, 512) == glm::uvec2 (512, 288));
    CHECK (FBOProvider::calculateTargetSize ({8192, 1}, 1, true, 512) == glm::uvec2 (512, 1));
}

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
