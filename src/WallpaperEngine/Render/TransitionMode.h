#pragma once

namespace WallpaperEngine::Render {
/** How a wallpaper switch reveals the incoming wallpaper over the outgoing one */
enum TransitionMode : int {
    TransitionMode_None = 0,
    TransitionMode_Fade = 1,
    TransitionMode_WipeLeft = 2,
    TransitionMode_WipeRight = 3,
    TransitionMode_WipeUp = 4,
    TransitionMode_WipeDown = 5,
    TransitionMode_Disc = 6,
    TransitionMode_Stripes = 7,
    TransitionMode_Pixelate = 8,
    TransitionMode_Honeycomb = 9,
    TransitionMode_WipeDiag = 10,
    TransitionMode_Clock = 11,
    TransitionMode_Iris = 12,
    TransitionMode_Checkerboard = 13,
    TransitionMode_Blinds = 14,
    TransitionMode_Split = 15,
    TransitionMode_Voronoi = 16,
    TransitionMode_Noise = 17,
    TransitionMode_Dots = 18,
    TransitionMode_InkSplash = 19,
    TransitionMode_Last = TransitionMode_InkSplash,
};
} // namespace WallpaperEngine::Render
