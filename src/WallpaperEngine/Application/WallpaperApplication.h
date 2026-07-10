#pragma once

#include <chrono>
#include <random>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Assets/AssetLocator.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Render/Drivers/Detectors/FullScreenDetector.h"
#include "WallpaperEngine/Render/Drivers/GLFWOpenGLDriver.h"
#include "WallpaperEngine/Render/Drivers/Output/GLFWWindowOutput.h"
#include "WallpaperEngine/Render/RenderContext.h"

#include "WallpaperEngine/Audio/Drivers/SDLAudioDriver.h"

#include "WallpaperEngine/Input/InputContext.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"

#include "WallpaperEngine/Data/Assets/Types.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Media/MediaSource.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace WallpaperEngine::Application {

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Data::Model;
/**
 * Small wrapper class over the actual wallpaper's main application skeleton
 */
class WallpaperApplication {
public:
    explicit WallpaperApplication (ApplicationContext& context);
    ~WallpaperApplication ();

    /**
     * Prepares the application for rendering.
     */
    void setup ();
    /**
     * Renders a frame of the application.
     */
    void render ();
    /**
     * Cleans up all the resources used by the application.
     */
    static void cleanup ();
    /**
     * Shows the application until it's closed
     */
    void show ();
    /**
     * @return Whether the render loop stopped abnormally (the video driver lost its output), so the
     *         process should exit non-zero and let a supervisor relaunch it
     */
    [[nodiscard]] bool abnormalTermination () const;
    /**
     * Handles a OS signal sent to this PID
     *
     * @param signal
     */
    void signal (int signal);
    /**
     * @return Maps screens to loaded backgrounds
     */
    [[nodiscard]] const std::map<std::string, ProjectUniquePtr>& getBackgrounds () const;
    /**
     * @return The current application context
     */
    [[nodiscard]] ApplicationContext& getContext () const;
    /**
     * Renders a frame
     */
    void update (Render::Drivers::Output::OutputViewport* viewport);
    /**
     * Gets the output
     */
    [[nodiscard]] const WallpaperEngine::Render::Drivers::Output::Output& getOutput () const;
    /**
     * Sets the destination framebuffer for rendering. If not called, the default framebuffer will be used.
     */
    void setDestinationFramebuffer (GLuint framebuffer);

    /**
     * Gets the currently set destination framebuffer for rendering. If not set, returns 0 (the default framebuffer).
     */
    [[nodiscard]] GLuint getDestinationFramebuffer () const;

private:
    /**
     * Sets up an asset locator for the given background
     *
     * @param bg
     */
    AssetLocatorUniquePtr setupAssetLocator (const std::string& bg) const;
    /**
     * Initializes subsystems required for application operation
     */
    void initializeSubsystems ();

    /**
     * Loads projects based off the settings
     */
    void loadBackgrounds ();
    /**
     * Loads the given project
     *
     * @param bg
     * @return
     */
    [[nodiscard]] ProjectUniquePtr loadBackground (const std::string& bg);
    /**
     * Loads and parses the given project. Touches no GL or mutable application state so
     * the switch worker thread can run it off the render thread.
     *
     * @param bg
     * @return
     */
    [[nodiscard]] ProjectUniquePtr loadProject (const std::string& bg) const;
    /**
     * Re-arms the automatic screenshot so it is retaken after a background change
     */
    void resetScreenshotState ();
    /**
     * Prepares all background's values and updates their properties if required
     */
    void setupProperties ();
    /**
     * Updates the properties for the given background based on the current context
     *
     * @param project
     */
    void setupPropertiesForProject (const Project& project);
    /**
     * Prepares CEF browser to be used
     */
    void setupBrowser ();
    /**
     * Prepares desktop environment-related things (like render, window, fullscreen detector, etc)
     */
    void setupOutput ();
    /**
     * Prepares all audio-related things (like detector, output, etc)
     */
    void setupAudio ();
    /**
     * Prepares the render-context of all the backgrounds so they can be displayed on the screen
     */
    void prepareOutputs ();
    /**
     * Prepares output debugging for all opengl errors
     */
    void setupOpenGLDebugging ();
    /**
     * Takes an screenshot of the background and saves it to the specified path
     *
     * @param filename
     */
    void takeScreenshot (const std::filesystem::path& filename) const;

    struct ActivePlaylist {
	ApplicationContext::PlaylistDefinition definition;
	std::vector<std::size_t> order;
	std::size_t orderIndex = 0;
	std::chrono::steady_clock::time_point nextSwitch;
	std::chrono::steady_clock::time_point lastUpdate;
	std::set<std::size_t> failedIndices;
    };

    /**
     * A background switch staged by the loader thread. The heavy CPU work (project parse,
     * texture reads + decompression) happens off the render thread; only the GL work
     * (texture uploads, shader compilation) runs on the main thread when it is applied.
     */
    struct PreparedSwitch {
	/** Monotonic id used to drop requests superseded by a newer one for the same screen */
	uint64_t id = 0;
	std::string screen {};
	std::string path {};
	Render::TransitionMode transition = Render::TransitionMode_Fade;
	/** Parsed project, set by the worker on success */
	ProjectUniquePtr project = nullptr;
	/** Textures pre-parsed by the worker; the main thread only uploads them to the GPU */
	std::vector<std::pair<std::string, Data::Assets::TextureUniquePtr>> textures {};
	/** Failure description, empty on success */
	std::string error {};
    };

    /**
     * Queues a background switch for the given screen. The load happens on the loader
     * thread and the swap is applied by processPreparedSwitches once ready, so rendering
     * never blocks. Shared by playlists and the IPC control socket.
     */
    void requestBackgroundSwitch (
	const std::string& screen, const std::string& path,
	Render::TransitionMode transition = Render::TransitionMode_Fade
    );
    /** Loader thread main loop */
    void switchWorkerMain ();
    /** Applies a background switch the loader thread finished preparing, if any */
    void processPreparedSwitches ();
    /**
     * GL-thread part of a background switch: texture uploads, wallpaper build and swap
     *
     * @return whether the switch succeeded
     */
    bool applyPreparedSwitch (PreparedSwitch& job);
    /** Marks a playlist item as failed so the next advancement skips it */
    void markPlaylistItemFailed (const std::string& screen, const std::string& path);
    /** Stops and joins the loader thread */
    void stopSwitchWorker ();
    /**
     * Creates the unix control socket used to switch wallpapers at runtime
     */
    void setupControlSocket ();
    /**
     * Accepts and executes pending commands on the control socket
     */
    void processControlSocket ();

    void initializePlaylists ();
    void updatePlaylists ();
    void advancePlaylist (
	const std::string& screen, ActivePlaylist& playlist, const std::chrono::steady_clock::time_point& now
    );
    bool selectNextCandidate (ActivePlaylist& playlist, std::size_t& outOrderIndex);
    bool preflightWallpaper (const std::string& path);
    std::vector<std::size_t> buildPlaylistOrder (const ApplicationContext::PlaylistDefinition& definition);
    void ensureBrowserForProject (const Project& project);
    bool makeAnyViewportCurrent () const;

    /** The application context that contains the current app settings */
    ApplicationContext& m_context;
    /** Listening unix socket for runtime wallpaper switching, -1 when unavailable */
    int m_controlSocket = -1;
    /** Path of the bound control socket so it can be unlinked on exit */
    std::string m_controlSocketPath {};
    /** Inode of the socket file this instance bound; exit only unlinks the path while it still matches */
    ino_t m_controlSocketInode = 0;
    /** Maps screens to backgrounds */
    std::map<std::string, ProjectUniquePtr> m_backgrounds {};
    std::map<std::string, ActivePlaylist> m_activePlaylists {};

    std::unique_ptr<WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector> m_audioDetector = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::AudioContext> m_audioContext = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::SDLAudioDriver> m_audioDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder> m_audioRecorder = nullptr;
    std::unique_ptr<WallpaperEngine::Render::RenderContext> m_renderContext = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::VideoDriver> m_videoDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::Detectors::FullScreenDetector> m_fullScreenDetector = nullptr;
    std::unique_ptr<WallpaperEngine::WebBrowser::WebBrowserContext> m_browserContext = nullptr;
    std::unique_ptr<WallpaperEngine::Media::MediaSource> m_mediaSource = nullptr;
    std::mt19937 m_playlistRng { std::random_device {}() };

    /** Loader thread that prepares background switches off the render thread */
    std::thread m_switchWorker {};
    /** Guards the switch queues, ids and the stop flag */
    std::mutex m_switchMutex {};
    /** Wakes the loader thread when work arrives or on shutdown */
    std::condition_variable m_switchCv {};
    /** Switches waiting to be loaded by the worker */
    std::deque<PreparedSwitch> m_switchRequests {};
    /** Switches loaded by the worker, waiting to be applied on the render thread */
    std::deque<PreparedSwitch> m_switchResults {};
    /** Latest switch request id per screen, used to drop superseded work */
    std::map<std::string, uint64_t> m_latestSwitchIds {};
    /** Monotonic counter feeding PreparedSwitch::id */
    uint64_t m_switchIdCounter = 0;
    /** Set under m_switchMutex to make the loader thread exit */
    bool m_switchWorkerStop = false;
    /** When set, malloc_trim runs at this point to return post-switch allocator slack to the OS */
    std::chrono::steady_clock::time_point m_pendingMallocTrim {};

    bool m_isPaused = false;
    bool m_screenShotTaken = false;
    uint32_t m_nextFrameScreenshot = 0;
    std::chrono::steady_clock::time_point m_pauseStart {};
    GLuint m_destinationFramebuffer = 0;
};
} // namespace WallpaperEngine::Application
