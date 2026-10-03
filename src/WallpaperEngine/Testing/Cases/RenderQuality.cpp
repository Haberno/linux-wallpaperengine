#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Render/FrameRateCounter.h"
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

TEST_CASE ("FPS measurements are independent for each paced output", "[fps]") {
    WallpaperEngine::Render::FrameRateCounter fast, slow;
    CHECK (fast.fps () == 0.0f);
    fast.frame (10.0);
    slow.frame (10.0);
    for (int i = 1; i <= 60; ++i) fast.frame (10.0 + i / 60.0);
    for (int i = 1; i <= 30; ++i) slow.frame (10.0 + i / 30.0);
    CHECK (fast.fps () == Catch::Approx (60.0));
    CHECK (slow.fps () == Catch::Approx (30.0));
}

TEST_CASE ("FPS measurements include rendering stalls in elapsed time", "[fps]") {
    WallpaperEngine::Render::FrameRateCounter counter;
    counter.frame (0.0);
    for (int i = 1; i <= 10; ++i) counter.frame (i * 0.01);
    counter.frame (1.0);
    CHECK (counter.fps () == Catch::Approx (11.0));
    for (int i = 1; i <= 30; ++i) counter.frame (1.0 + i / 30.0);
    CHECK (counter.fps () == Catch::Approx (30.0));
}

TEST_CASE ("authored effect extents precede fit and target scale independently of output quality", "[render-quality][fbo][size]") {
    WallpaperEngine::Data::Model::FBO target {
	.name = "_rt_GlitterTiles", .format = "r8", .scale = 1.0f, .unique = false,
	.width = 256, .height = 256,
    };
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (256, 256));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 640, 360 }, 2) == glm::uvec2 (256, 256));
    target.height = 0xffff;
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 2) == glm::uvec2 (256, 360));
    target.fit = 4097;
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 5000 }, 1) == glm::uvec2 (256, 5000));
    target.width = 192;
    target.height = 48;
    target.fit = 64;
    target.scale = 2;
    CHECK (FBOProvider::calculateEffectTargetExtent (target, { 320, 180 }, 1) == glm::uvec2 (64, 16));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (32, 8));
    target.width = 4097;
    target.height = 0xffff;
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (32, 18));
    target.width = 0xffff;
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (32, 18));
    target.fit = 0xffff;
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (160, 90));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 1, 1 }, 1) == glm::uvec2 (2, 2));
    target.width = 0;
    target.height = 1;
    target.fit = 0xffff;
    CHECK (FBOProvider::calculateEffectTargetExtent (target, { 320, 180 }, 1) == glm::uvec2 (0, 1));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (2, 2));
}

TEST_CASE ("authored zero fit uses raw zero extents and native minimum storage", "[render-quality][fbo][fit]") {
    WallpaperEngine::Data::Model::FBO target {
        .name = "_rt_fit", .format = "rgba8888", .scale = 2.0f, .unique = false,
    };
    CHECK (target.fit == 0xffff);
    CHECK (FBOProvider::calculateEffectTargetExtent (target, { 320, 180 }, 1) == glm::uvec2 (320, 180));
    target.fit = 0;
    for (const glm::vec2 size : { glm::vec2 (320, 180), glm::vec2 (180, 320) }) {
        CHECK (FBOProvider::calculateEffectTargetExtent (target, size, 1) == glm::uvec2 (0, 0));
        CHECK (FBOProvider::calculateEffectTargetSize (target, size, 1) == glm::uvec2 (2, 2));
    }
    target.width = 128;
    target.height = 72;
    CHECK (FBOProvider::calculateEffectTargetExtent (target, { 640, 360 }, 2) == glm::uvec2 (0, 0));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 640, 360 }, 2) == glm::uvec2 (2, 2));
    target.fit = 1;
    CHECK (FBOProvider::calculateEffectTargetExtent (target, { 320, 180 }, 1) == glm::uvec2 (1, 0));
    CHECK (FBOProvider::calculateEffectTargetSize (target, { 320, 180 }, 1) == glm::uvec2 (2, 2));
    // The generic helper still uses zero to mean no fit cap.
    CHECK (FBOProvider::calculateTargetSize ({ 320, 180 }, 1, true, 0) == glm::uvec2 (320, 180));
}
