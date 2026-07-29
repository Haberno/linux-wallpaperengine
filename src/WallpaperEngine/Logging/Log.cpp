#include "Log.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>

using namespace WallpaperEngine::Logging;

void Log::emit (const std::vector<std::ostream*>& streams, const char* level, const std::string& message) {
    if (streams.empty ()) {
	return;
    }

    // Every line carries a wall-clock timestamp and a level so the output can be correlated with a
    // crash and machine-read afterwards. Without them an error is textually identical to ordinary
    // output and nothing downstream can tell the two apart.
    const auto now = std::chrono::system_clock::now ();
    const auto whole = std::chrono::floor<std::chrono::seconds> (now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds> (now - whole).count ();
    const std::time_t stamp = std::chrono::system_clock::to_time_t (whole);

    std::tm parts {};
    localtime_r (&stamp, &parts);

    char prefix[32];
    std::snprintf (
	prefix, sizeof (prefix), "%02d:%02d:%02d.%03d [%s] ", parts.tm_hour, parts.tm_min, parts.tm_sec,
	static_cast<int> (millis), level
    );

    for (const auto cur : streams) {
	*cur << prefix << message << std::endl;
    }
}

Log::Log () { assert (this->sInstance == nullptr); }

Log& Log::get () {
    if (sInstance == nullptr) {
	sInstance = std::unique_ptr<Log> (new Log ());
    }

    return *sInstance;
}

void Log::addOutput (std::ostream* stream) { this->mOutputs.push_back (stream); }

void Log::addError (std::ostream* stream) { this->mErrors.push_back (stream); }

std::unique_ptr<Log> Log::sInstance = nullptr;