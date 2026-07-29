#pragma once

#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <vector>

#include "WallpaperEngine/Debug/RenderHealth.h"

namespace WallpaperEngine::Logging {
/**
 * Singleton class, simplifies logging for the whole app
 */
class Log {
public:
    void addOutput (std::ostream* stream);
    void addError (std::ostream* stream);

    template <typename... Data> void out (Data... data) {
	Log::emit (this->mOutputs, "INFO", this->buildBuffer (data...));
    }

    template <typename... Data> void debug (Data... data) {
#if (!NDEBUG) && (!ERRORONLY)
	Log::emit (this->mOutputs, "DEBUG", this->buildBuffer (data...));
#endif /* DEBUG */
    }

    template <typename... Data> void debugerror (Data... data) {
#if (!NDEBUG) && (ERRORONLY)
	Log::emit (this->mOutputs, "DEBUG", this->buildBuffer (data...));
#endif /* DEBUG */
    }

    template <typename... Data> void error (Data... data) {
	std::string str = this->buildBuffer (data...);

	Debug::RenderHealth::record ("log.error", str);

	Log::emit (this->mErrors, "ERROR", str);
    }

    template <class EX, typename... Data> [[noreturn]] void exception (Data... data) {
	std::string str = this->buildBuffer (data...);

	Debug::RenderHealth::record ("log.exception", str);

	Log::emit (this->mErrors, "FATAL", str);

	// now throw the exception
	throw EX (str);
    }

    template <typename... Data> [[noreturn]] void exception (Data... data) {
	this->exception<std::runtime_error> (data...);
    }

    static Log& get ();

private:
    Log ();

    /// writes one timestamped, level-tagged line to each of the given streams
    static void emit (const std::vector<std::ostream*>& streams, const char* level, const std::string& message);

    template <typename... Data> std::string buildBuffer (Data... data) {
	// buffer the string first
	std::stringbuf buffer;
	std::ostream bufferStream (&buffer);

	((bufferStream << std::forward<Data> (data)), ...);

	return buffer.str ();
    }

    std::vector<std::ostream*> mOutputs = {};
    std::vector<std::ostream*> mErrors = {};
    static std::unique_ptr<Log> sInstance;
};
} // namespace WallpaperEngine::Logging

#define sLog (WallpaperEngine::Logging::Log::get ())