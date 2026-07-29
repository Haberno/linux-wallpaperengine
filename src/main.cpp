#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Debug/RenderHealth.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"

#include <cstring>

WallpaperEngine::Application::WallpaperApplication* app;

void signalhandler (const int sig) {
    if (app == nullptr) {
	return;
    }

    app->signal (sig);
}

/// default log location, so tools can find the log without being told where it is
std::filesystem::path defaultLogFile () {
    const char* stateHome = getenv ("XDG_STATE_HOME");
    const char* home = getenv ("HOME");

    if (stateHome != nullptr && stateHome[0] != '\0') {
	return std::filesystem::path (stateHome) / "linux-wallpaperengine" / "engine.log";
    }
    if (home != nullptr && home[0] != '\0') {
	return std::filesystem::path (home) / ".local/state/linux-wallpaperengine" / "engine.log";
    }
    return {};
}

void initLogging () {
    // Keep independent stream formatting state without heap-allocating objects that the
    // non-owning logger cannot release. Function-local statics outlive the logger singleton.
    static std::ostream output (std::cout.rdbuf ());
    static std::ostream error (std::cerr.rdbuf ());

    sLog.addOutput (&output);
    sLog.addError (&error);

    // Launchers such as Waypaper connect stdout/stderr to /dev/null, which loses the log entirely
    // and leaves nothing to inspect after a crash, so the log always goes to a file as well.
    // $WPE_LOG_FILE overrides the location; "off" disables the file sink completely.
    const char* logFile = getenv ("WPE_LOG_FILE");
    const std::string requested = logFile != nullptr ? logFile : "";

    if (requested == "off") {
	return;
    }

    const std::filesystem::path path = requested.empty () ? defaultLogFile () : std::filesystem::path (requested);
    if (path.empty ()) {
	return;
    }

    std::error_code ec;
    if (!path.parent_path ().empty ()) {
	std::filesystem::create_directories (path.parent_path (), ec);
    }

    // Keep exactly one previous run alongside the current one: a crash is usually diagnosed from
    // the run that died, which is no longer the one being written. Anything older is dropped so
    // the log cannot grow without bound.
    std::filesystem::rename (path, path.string () + ".1", ec);

    static std::ofstream logStream (path, std::ios::trunc);
    if (!logStream.is_open ()) {
	sLog.error ("Cannot open log file for writing: ", path.string ());
	return;
    }

    sLog.addOutput (&logStream);
    sLog.addError (&logStream);
}

// Chromium rotates the stack guard cookie in every process the zygote forks
// (--change-stack-guard-on-fork=enable). main's canary is written before the fork under the old
// cookie, so any exit path in the forked child compares it against the new %fs:0x28 and aborts in
// __stack_chk_fail, dumping a core per helper teardown. The check is emitted ahead of the exit
// call, so _exit alone cannot skip it: main must not carry a canary at all. Only main is exempted;
// every other function keeps the protector.
#if defined(__has_attribute)
#if __has_attribute(no_stack_protector)
#define WPE_NO_STACK_PROTECTOR __attribute__ ((no_stack_protector))
#endif
#endif
#if !defined(WPE_NO_STACK_PROTECTOR)
#define WPE_NO_STACK_PROTECTOR
#endif

WPE_NO_STACK_PROTECTOR int main (int argc, char* argv[]) {
    // CEF process bootstrap must be the first operation in every process. Helper processes run to
    // completion here; the browser process returns -1 and continues with normal engine startup.
    const int cefExitCode = WallpaperEngine::WebBrowser::WebBrowserContext::executeSubprocess (argc, argv);
    if (cefExitCode >= 0) {
	// Forked helpers must not unwind through main: the engine objects they would run destructors
	// for belong to the pre-fork parent. CEF has already run its own shutdown by this point.
	_exit (cefExitCode);
    }

#if defined(__GLIBC__)
    // Texture decoding runs one thread per core, and glibc would give each of them its own
    // arena. The decode buffers are hundreds of MB per wallpaper, and once freed they sit
    // mid-arena where malloc_trim cannot return them: 232 MiB stuck per heavy switch instead
    // of 5. Two arenas keep that memory reclaimable and cost ~3% of the decode.
    mallopt (M_ARENA_MAX, 2);

    // glibc's mmap threshold is adaptive: freeing an mmap'd block raises it towards 32 MiB so
    // later allocations of that size come from the brk arena instead. After a few switches the
    // decode buffers stop being mmap'd and start fragmenting the arena, which is what M_ARENA_MAX
    // above is trying to avoid: observed arena 420 MiB holding 173 MiB live, with mmap at 0.
    // Setting the threshold explicitly also pins it, so buffers this large keep going through
    // mmap and free returns them to the OS outright instead of leaving holes malloc_trim cannot
    // reach.
    mallopt (M_MMAP_THRESHOLD, 1024 * 1024);
#endif

    // arm the health report (if configured) before anything can fail so a report is
    // written on every exit path, even when the failure never touches a hook
    WallpaperEngine::Debug::RenderHealth::count ("run.start");

    try {
	// if type parameter is specified, this is a subprocess, so no logging should be enabled from our side
	bool enableLogging = true;
	const std::string typeZygote = "--type=zygote";
	const std::string typeUtility = "--type=utility";

	for (int i = 1; i < argc; i++) {
	    if (strncmp (typeZygote.c_str (), argv[i], typeZygote.size ()) == 0) {
		enableLogging = false;
		break;
	    }

	    if (strncmp (typeUtility.c_str (), argv[i], typeUtility.size ()) == 0) {
		enableLogging = false;
		break;
	    }
	}

	if (enableLogging) {
	    initLogging ();
	}

	WallpaperEngine::Application::ApplicationContext appContext (argc, argv);

	appContext.loadSettingsFromArgv ();

	app = new WallpaperEngine::Application::WallpaperApplication (appContext);

	// halt if the list-properties option was specified
	if (appContext.settings.general.onlyListProperties) {
	    delete app;
	    return 0;
	}

	// attach signals to gracefully stop
	std::signal (SIGINT, signalhandler);
	std::signal (SIGTERM, signalhandler);
	std::signal (SIGKILL, signalhandler);

	// show the wallpaper application
	app->show ();

	// remove signal handlers before destroying app
	std::signal (SIGINT, SIG_DFL);
	std::signal (SIGTERM, SIG_DFL);
	std::signal (SIGKILL, SIG_DFL);

	// A clean stop (signal, socket quit) returns 0; an abnormal one (the driver lost its only
	// output) returns non-zero so the launcher's supervisor relaunches instead of leaving the
	// wallpaper dead.
	const bool abnormal = app->abnormalTermination ();

	delete app;

	return abnormal ? 1 : 0;
    } catch (const std::exception& e) {
	// sLog.exception already recorded log.exception; this also catches failures thrown
	// directly (e.g. std::filesystem) that never went through a hook
	WallpaperEngine::Debug::RenderHealth::record ("fatal.exception", e.what ());

	std::cerr << e.what () << std::endl;
	return 1;
    }
}
