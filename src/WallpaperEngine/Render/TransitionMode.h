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
    TransitionMode_Last = TransitionMode_Honeycomb,
};
} // namespace WallpaperEngine::Render
