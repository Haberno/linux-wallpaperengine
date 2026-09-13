#include "WallpaperEngine/Render/FBOProvider.h"

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

using namespace WallpaperEngine::Data::Assets;
using WallpaperEngine::Render::FBOProvider;

TEST_CASE ("Fluid targets keep signed half-float velocity and pressure storage", "[fluid][fbo][format]") {
    const auto velocity = FBOProvider::resolveTargetFormat ("rg1616f");
    CHECK (velocity == TextureFormat_RG1616f);

    const auto pressure = FBOProvider::resolveTargetFormat ("r16f");
    CHECK (pressure == TextureFormat_R16f);
}

TEST_CASE ("Fluid dye inherits the current scene color format", "[fluid][fbo][format]") {
    FBOProvider scene (nullptr);
    FBOProvider image (&scene);
    FBOProvider effect (&image);

    const auto resolveDye = [&] {
	return FBOProvider::resolveTargetFormat ("rgba_backbuffer", effect.getBackbufferFormat ());
    };
    CHECK (resolveDye () == TextureFormat_ARGB8888);
    scene.setBackbufferFormat (TextureFormat_RGBA16161616f);
    CHECK (resolveDye () == TextureFormat_RGBA16161616f);
    scene.setBackbufferFormat (TextureFormat_ARGB8888);
    CHECK (resolveDye () == TextureFormat_ARGB8888);
}

TEST_CASE ("Explicit effect target formats keep their storage independent of HDR", "[fluid][fbo][format]") {
    for (const auto backbuffer : { TextureFormat_ARGB8888, TextureFormat_RGBA16161616f }) {
	for (const auto format : { "rgba16161616f", "rgba16f" }) {
	    CHECK (FBOProvider::resolveTargetFormat (format, backbuffer) == TextureFormat_RGBA16161616f);
	}
	CHECK (FBOProvider::resolveTargetFormat ("rgba8888", backbuffer) == TextureFormat_ARGB8888);
	CHECK (FBOProvider::resolveTargetFormat ("unknown", backbuffer) == TextureFormat_ARGB8888);
    }
}
