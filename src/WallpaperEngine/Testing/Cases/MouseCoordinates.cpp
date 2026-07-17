#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

// CEF exposes its own CHECK macro through CScene's renderer includes. Catch must own the
// test assertion macro in this translation unit.
#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using WallpaperEngine::Render::Camera;
using WallpaperEngine::Render::Wallpapers::CScene;

/**
 * Test GLFW to OpenGL coordinate conversion
 * GLFW: Y=0 at top, Y=height at bottom
 * OpenGL: Y=0 at bottom, Y=height at top
 */
TEST_CASE ("GLFW to OpenGL coordinate conversion") {
    const int framebufferHeight = 1080;

    // Mouse at top of screen (GLFW: Y=0)
    double glfwY = 0.0;
    double openglY = static_cast<double> (framebufferHeight) - glfwY;
    CHECK (openglY == 1080.0); // Should be at top in OpenGL (Y=height)

    // Mouse at bottom of screen (GLFW: Y=height)
    glfwY = 1080.0;
    openglY = static_cast<double> (framebufferHeight) - glfwY;
    CHECK (openglY == 0.0); // Should be at bottom in OpenGL (Y=0)

    // Mouse at middle of screen
    glfwY = 540.0;
    openglY = static_cast<double> (framebufferHeight) - glfwY;
    CHECK (openglY == 540.0); // Should be at middle in both systems
}

/**
 * Test Wayland to OpenGL coordinate conversion
 * Wayland: Y=0 at top, Y=height at bottom
 * OpenGL: Y=0 at bottom, Y=height at top
 */
TEST_CASE ("Wayland to OpenGL coordinate conversion") {
    const double viewportHeight = 1080.0;

    // Mouse at top of screen (Wayland: Y=0)
    double waylandY = 0.0;
    double openglY = viewportHeight - waylandY;
    CHECK (openglY == 1080.0); // Should be at top in OpenGL

    // Mouse at bottom of screen (Wayland: Y=height)
    waylandY = 1080.0;
    openglY = viewportHeight - waylandY;
    CHECK (openglY == 0.0); // Should be at bottom in OpenGL

    // Mouse at middle of screen
    waylandY = 540.0;
    openglY = viewportHeight - waylandY;
    CHECK (openglY == 540.0); // Should be at middle
}

/**
 * Test OpenGL to normalized coordinate conversion
 * OpenGL: Y=0 at bottom, Y=height at top
 * Normalized: 0=bottom, 1=top (OpenGL convention)
 */
TEST_CASE ("OpenGL to normalized coordinate conversion") {
    const int viewportY = 0;
    const int viewportHeight = 1080;

    // Mouse at top in OpenGL (Y=height)
    double openglY = 1080.0;
    double normalizedY = glm::clamp ((openglY - viewportY) / static_cast<double> (viewportHeight), 0.0, 1.0);
    CHECK (normalizedY == 1.0); // Should be 1.0 (top)

    // Mouse at bottom in OpenGL (Y=0)
    openglY = 0.0;
    normalizedY = glm::clamp ((openglY - viewportY) / static_cast<double> (viewportHeight), 0.0, 1.0);
    CHECK (normalizedY == 0.0); // Should be 0.0 (bottom)

    // Mouse at middle
    openglY = 540.0;
    normalizedY = glm::clamp ((openglY - viewportY) / static_cast<double> (viewportHeight), 0.0, 1.0);
    CHECK (std::abs (normalizedY - 0.5) < 0.001); // Should be ~0.5 (middle)
}

/**
 * Test OpenGL to CEF coordinate conversion
 * OpenGL: Y=0 at bottom, Y=height at top
 * CEF: Y=0 at top, Y=height at bottom
 */
TEST_CASE ("OpenGL to CEF coordinate conversion") {
    const int viewportHeight = 1080;
    const int viewportY = 0;

    // Mouse at top in OpenGL (Y=height)
    double openglY = 1080.0;
    int clampedY = std::clamp (static_cast<int> (openglY - viewportY), 0, viewportHeight);
    int cefY = viewportHeight - clampedY;
    CHECK (cefY == 0); // Should be 0 (top in CEF)

    // Mouse at bottom in OpenGL (Y=0)
    openglY = 0.0;
    clampedY = std::clamp (static_cast<int> (openglY - viewportY), 0, viewportHeight);
    cefY = viewportHeight - clampedY;
    CHECK (cefY == 1080); // Should be height (bottom in CEF)

    // Mouse at middle
    openglY = 540.0;
    clampedY = std::clamp (static_cast<int> (openglY - viewportY), 0, viewportHeight);
    cefY = viewportHeight - clampedY;
    CHECK (cefY == 540); // Should be middle
}

/**
 * Test complete coordinate flow: GLFW → OpenGL → Normalized
 * Verifies the full pipeline works correctly
 */
TEST_CASE ("Complete coordinate flow: GLFW to normalized") {
    const int framebufferHeight = 1080;
    const int viewportY = 0;
    const int viewportHeight = 1080;

    // Mouse at top of screen
    double glfwY = 0.0;
    double openglY = static_cast<double> (framebufferHeight) - glfwY; // Convert to OpenGL
    double normalizedY = glm::clamp ((openglY - viewportY) / static_cast<double> (viewportHeight), 0.0, 1.0);
    CHECK (normalizedY == 1.0); // Top should normalize to 1.0

    // Mouse at bottom of screen
    glfwY = 1080.0;
    openglY = static_cast<double> (framebufferHeight) - glfwY;
    normalizedY = glm::clamp ((openglY - viewportY) / static_cast<double> (viewportHeight), 0.0, 1.0);
    CHECK (normalizedY == 0.0); // Bottom should normalize to 0.0
}

TEST_CASE ("SceneScript cursor world position uses orthographic canvas coordinates") {
    const glm::mat4 identity (1.0f);

    const glm::vec3 bottomLeft
	= Camera::projectScreenPosition ({ 0.0f, 0.0f }, true, 343.0f, 193.0f, identity, identity, false);
    const glm::vec3 center
	= Camera::projectScreenPosition ({ 0.5f, 0.5f }, true, 343.0f, 193.0f, identity, identity, false);
    const glm::vec3 topRight
	= Camera::projectScreenPosition ({ 1.0f, 1.0f }, true, 343.0f, 193.0f, identity, identity, false);

    CHECK (bottomLeft == glm::vec3 (0.0f, 0.0f, 0.0f));
    CHECK (center == glm::vec3 (171.5f, 96.5f, 0.0f));
    CHECK (topRight == glm::vec3 (343.0f, 193.0f, 0.0f));
}

TEST_CASE ("SceneScript cursor world position intersects the perspective authoring plane") {
    constexpr float fov = 50.0f;
    constexpr float aspect = 16.0f / 9.0f;
    constexpr float cameraDistance = 2.3f;
    const glm::mat4 view
	= glm::lookAt (glm::vec3 (0.0f, 0.0f, cameraDistance), glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective (glm::radians (fov), aspect, 0.01f, 100.0f);

    const glm::vec3 center
	= Camera::projectScreenPosition ({ 0.5f, 0.5f }, false, 1920.0f, 1080.0f, projection, view, false);
    const glm::vec3 right
	= Camera::projectScreenPosition ({ 1.0f, 0.5f }, false, 1920.0f, 1080.0f, projection, view, false);
    const glm::vec3 top
	= Camera::projectScreenPosition ({ 0.5f, 1.0f }, false, 1920.0f, 1080.0f, projection, view, false);

    const float expectedY = cameraDistance * std::tan (glm::radians (fov) * 0.5f);
    CHECK (center.x == Catch::Approx (0.0f).margin (0.0001f));
    CHECK (center.y == Catch::Approx (0.0f).margin (0.0001f));
    CHECK (right.x == Catch::Approx (expectedY * aspect).margin (0.0001f));
    CHECK (top.y == Catch::Approx (expectedY).margin (0.0001f));

    projection[1][1] *= -1.0f;
    const glm::vec3 flippedTop
	= Camera::projectScreenPosition ({ 0.5f, 1.0f }, false, 1920.0f, 1080.0f, projection, view, true);
    CHECK (flippedTop.y == Catch::Approx (expectedY).margin (0.0001f));
}

/**
 * Test coordinate conversion with different viewport sizes
 * Ensures conversion works with non-standard viewport dimensions
 */
TEST_CASE ("Coordinate conversion with different viewport sizes") {
    // Test with 1920x1080 viewport
    {
	const int height = 1080;
	double glfwY = 0.0;
	double openglY = static_cast<double> (height) - glfwY;
	CHECK (openglY == 1080.0);
    }

    // Test with 2560x1440 viewport
    {
	const int height = 1440;
	double glfwY = 0.0;
	double openglY = static_cast<double> (height) - glfwY;
	CHECK (openglY == 1440.0);
    }

    // Test with 800x600 viewport
    {
	const int height = 600;
	double glfwY = 0.0;
	double openglY = static_cast<double> (height) - glfwY;
	CHECK (openglY == 600.0);
    }
}

TEST_CASE ("Wallpaper Engine camera parallax delay response") {
    CHECK (CScene::calculateParallaxSmoothingAlpha (0.0f, 1.0f / 60.0f) == 1.0f);
    CHECK (CScene::calculateParallaxSmoothingAlpha (1.0f, 1.0f / 60.0f) == Catch::Approx (1.0f / 9.0f));
    CHECK (CScene::calculateParallaxSmoothingAlpha (2.0f, 1.0f / 60.0f) == Catch::Approx (1.0f / 18.0f));
    CHECK (CScene::calculateParallaxSmoothingAlpha (3.0f, 1.0f / 60.0f) == 0.0f);
}

TEST_CASE ("Wallpaper Engine fog authoring values map to shader parameters") {
    const glm::vec4 distance = CScene::calculateFogParams (1.0f, 24.65f, 0.54f, 0.97f);
    CHECK (distance.x == 1.0f);
    CHECK (distance.y == Catch::Approx (23.65f));
    CHECK (distance.z == 0.54f);
    CHECK (distance.w == Catch::Approx (0.43f));

    const glm::vec4 height = CScene::calculateFogParams (-8.35f, 75.0f, 0.0f, 0.6f);
    CHECK (height.x == -8.35f);
    CHECK (height.y == Catch::Approx (83.35f));
    CHECK (height.z == 0.0f);
    CHECK (height.w == 0.6f);
}

TEST_CASE ("Wallpaper Engine camera path startup fade envelope") {
    CHECK (CScene::calculateCameraFadeAlpha (0.0f, 10.0f) == 1.0f);
    CHECK (CScene::calculateCameraFadeAlpha (0.25f, 10.0f) == Catch::Approx (0.5f));
    CHECK (CScene::calculateCameraFadeAlpha (0.5f, 10.0f) == 0.0f);
    CHECK (CScene::calculateCameraFadeAlpha (9.5f, 10.0f) == 0.0f);
    CHECK (CScene::calculateCameraFadeAlpha (9.75f, 10.0f) == Catch::Approx (0.5f));
    CHECK (CScene::calculateCameraFadeAlpha (10.0f, 10.0f) == 1.0f);
    CHECK (CScene::calculateCameraFadeAlpha (1.0f, 0.0f) == 0.0f);
}

TEST_CASE ("Camera path queue selection supports sequence and no-repeat random") {
    CHECK (CScene::chooseCameraPathIndex (std::nullopt, 3, "sequence", 2) == 0);
    CHECK (CScene::chooseCameraPathIndex (0, 3, "sequence", 2) == 1);
    CHECK (CScene::chooseCameraPathIndex (2, 3, "sequence", 2) == 0);
    CHECK (CScene::chooseCameraPathIndex (std::nullopt, 3, "random", 5) == 2);
    CHECK (CScene::chooseCameraPathIndex (1, 3, "random", 1) == 2);
    CHECK (CScene::chooseCameraPathIndex (2, 3, "random", 1) == 1);
}

TEST_CASE ("3D transparent sorting preserves fixed slots and orders blended models back to front") {
    using SortClass = WallpaperEngine::Render::RenderSortClass;
    const std::vector<CScene::TransparentSortKey> keys = {
	{ .sortable = true, .renderClass = SortClass::Translucent, .cameraDepth = -2.0f },
	{ .sortable = false, .renderClass = SortClass::Translucent, .cameraDepth = -100.0f },
	{ .sortable = true, .renderClass = SortClass::Opaque, .cameraDepth = -20.0f },
	{ .sortable = true, .renderClass = SortClass::Additive, .cameraDepth = -10.0f },
	{ .sortable = true, .renderClass = SortClass::Translucent, .cameraDepth = -9.0f },
	{ .sortable = true, .renderClass = SortClass::Opaque, .cameraDepth = -1.0f },
    };

    const std::vector<size_t> permutation = CScene::calculateTransparentSortPermutation (keys);
    REQUIRE (permutation.size () == keys.size ());
    CHECK (permutation == std::vector<size_t> { 2, 1, 5, 4, 0, 3 });
}

TEST_CASE ("Shader parallax position is independent of camera translation amount") {
    CHECK (CScene::calculateShaderParallaxPosition ({ 0.0f, 0.0f }) == glm::vec2 (0.5f));
    CHECK (CScene::calculateShaderParallaxPosition ({ -1.0f, 1.0f }) == glm::vec2 (0.0f, 1.0f));
    CHECK (CScene::calculateShaderParallaxPosition ({ -0.15f, 0.15f }) == glm::vec2 (0.425f, 0.575f));
}
