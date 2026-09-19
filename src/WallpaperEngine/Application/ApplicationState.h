#pragma once

#include "ApplicationContext.h"
#include <atomic>

namespace WallpaperEngine::Application {
/**
 * Represents current application state
 */
class ApplicationState {
public:
    struct {
        // Written by the signal handler as well as read by audio/render threads.
        static_assert (std::atomic<bool>::is_always_lock_free);
        std::atomic<bool> keepRunning { false };
    } general {};

    struct {
	bool enabled;
	int volume;
    } audio {};

    struct {
	bool enabled;
    } mouse {};
};
} // namespace WallpaperEngine::Application