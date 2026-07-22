#include "RenderHealth.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

using namespace WallpaperEngine;

namespace {
constexpr std::size_t MAX_DETAILS_PER_METRIC = 10;
constexpr std::size_t MAX_DETAIL_LENGTH = 300;

struct State {
    std::mutex mutex;
    std::map<std::string, long> counters;
    std::map<std::string, std::vector<std::string>> details;
    std::chrono::steady_clock::time_point firstFrame;
    std::chrono::steady_clock::time_point lastFrame;
    double worstFrameSeconds = 0.0;
    long frames = 0;
};

State& state () {
    static State value;

    return value;
}

bool isContinuation (const unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/**
 * Length of the well-formed UTF-8 sequence starting at in[at] (per Unicode table 3-7,
 * so overlongs and surrogates are rejected too), or 0 when malformed/incomplete.
 */
std::size_t sequenceLength (const std::string& in, const std::size_t at) {
    const auto lead = static_cast<unsigned char> (in [at]);
    std::size_t length;
    unsigned char low = 0x80, high = 0xBF;

    if (lead >= 0xC2 && lead <= 0xDF) {
	length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
	length = 3;

	if (lead == 0xE0) {
	    low = 0xA0;
	} else if (lead == 0xED) {
	    high = 0x9F;
	}
    } else if (lead >= 0xF0 && lead <= 0xF4) {
	length = 4;

	if (lead == 0xF0) {
	    low = 0x90;
	} else if (lead == 0xF4) {
	    high = 0x8F;
	}
    } else {
	return 0;
    }

    if (at + length > in.size ()) {
	return 0;
    }

    const auto second = static_cast<unsigned char> (in [at + 1]);

    if (second < low || second > high) {
	return 0;
    }

    for (std::size_t i = 2; i < length; i++) {
	if (!isContinuation (static_cast<unsigned char> (in [at + i]))) {
	    return 0;
	}
    }

    return length;
}

std::string escape (const std::string& in) {
    std::string out;

    out.reserve (in.size () + 8);

    for (std::size_t i = 0; i < in.size ();) {
	const auto cur = static_cast<unsigned char> (in [i]);

	if (cur == '"') {
	    out += "\\\"";
	} else if (cur == '\\') {
	    out += "\\\\";
	} else if (cur == '\n') {
	    out += "\\n";
	} else if (cur == '\r') {
	    out += "\\r";
	} else if (cur == '\t') {
	    out += "\\t";
	} else if (cur < 0x20) {
	    char buffer [8];
	    std::snprintf (buffer, sizeof (buffer), "\\u%04x", cur);
	    out += buffer;
	} else if (cur < 0x80) {
	    out += static_cast<char> (cur);
	} else {
	    // multi-byte input: copy only well-formed UTF-8, anything else (mis-encoded
	    // log content, sequences split by truncation) becomes U+FFFD so the report
	    // always parses as strict JSON
	    const std::size_t length = sequenceLength (in, i);

	    if (length > 0) {
		out.append (in, i, length);
		i += length;
	    } else {
		out += "\\ufffd";
		i++;
	    }

	    continue;
	}

	i++;
    }

    return out;
}

/**
 * Runs from atexit, potentially during static destruction: only touches state()
 * (whose lifetime is guaranteed by the registration order in enabled()) and the
 * standard streams, never sLog.
 */
void writeReport () {
    auto& s = state ();
    const std::lock_guard lock (s.mutex);

    std::string json = "{\n  \"counters\": {";
    bool first = true;

    for (const auto& [metric, value] : s.counters) {
	json += first ? "\n" : ",\n";
	json += "    \"" + escape (metric) + "\": " + std::to_string (value);
	first = false;
    }

    const double elapsed
	= s.frames > 1 ? std::chrono::duration<double> (s.lastFrame - s.firstFrame).count () : 0.0;

    json += "\n  },\n  \"timing\": {";
    json += "\n    \"frames\": " + std::to_string (s.frames);
    json += ",\n    \"elapsed_seconds\": " + std::to_string (elapsed);
    json += ",\n    \"avg_fps\": "
	+ std::to_string (elapsed > 0.0 ? static_cast<double> (s.frames - 1) / elapsed : 0.0);
    json += ",\n    \"worst_frame_ms\": " + std::to_string (s.worstFrameSeconds * 1000.0);
    json += "\n  },\n  \"details\": {";
    first = true;

    for (const auto& [metric, list] : s.details) {
	json += first ? "\n" : ",\n";
	json += "    \"" + escape (metric) + "\": [";

	bool firstDetail = true;

	for (const auto& cur : list) {
	    json += firstDetail ? "\n" : ",\n";
	    json += "      \"" + escape (cur) + "\"";
	    firstDetail = false;
	}

	json += "\n    ]";
	first = false;
    }

    json += "\n  }\n}\n";

    const char* path = std::getenv ("WPE_HEALTH_REPORT");

    if (path == nullptr) {
	return;
    }

    if (std::string_view {path} == "-") {
	std::cout << json << std::flush;
	return;
    }

    std::ofstream out (path);
    out << json;
    std::cerr << "Health report written to " << path << std::endl;
}
} // namespace

bool Debug::RenderHealth::enabled () {
    static const bool value = [] {
	const bool set = std::getenv ("WPE_HEALTH_REPORT") != nullptr;

	if (set) {
	    // construct the state BEFORE registering the writer: atexit handlers registered
	    // after an object's initialization run before that object is destroyed, so the
	    // report can be written safely no matter how the process goes down
	    state ();
	    std::atexit (writeReport);
	}

	return set;
    } ();

    return value;
}

void Debug::RenderHealth::count (const std::string& metric) {
    if (!enabled ()) {
	return;
    }

    auto& s = state ();
    const std::lock_guard lock (s.mutex);

    s.counters [metric]++;
}

void Debug::RenderHealth::record (const std::string& metric, const std::string& detail) {
    if (!enabled ()) {
	return;
    }

    auto& s = state ();
    const std::lock_guard lock (s.mutex);

    s.counters [metric]++;

    auto& list = s.details [metric];

    if (list.size () >= MAX_DETAILS_PER_METRIC) {
	return;
    }

    auto trimmed = detail.substr (0, MAX_DETAIL_LENGTH);

    if (trimmed.size () < detail.size ()) {
	// never leave a split multi-byte sequence at the truncation point
	std::size_t tail = trimmed.size ();

	while (tail > 0 && isContinuation (static_cast<unsigned char> (trimmed [tail - 1]))
	       && trimmed.size () - tail < 3) {
	    tail--;
	}

	if (tail > 0) {
	    const auto lead = static_cast<unsigned char> (trimmed [tail - 1]);
	    const std::size_t expected = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;

	    if (expected > trimmed.size () - tail + 1) {
		trimmed.resize (tail - 1);
	    }
	}
    }

    if (std::ranges::find (list, trimmed) == list.end ()) {
	list.push_back (std::move (trimmed));
    }
}

void Debug::RenderHealth::frame () {
    if (!enabled ()) {
	return;
    }

    const auto now = std::chrono::steady_clock::now ();
    auto& s = state ();
    const std::lock_guard lock (s.mutex);

    if (s.frames > 0) {
	s.worstFrameSeconds
	    = std::max (s.worstFrameSeconds, std::chrono::duration<double> (now - s.lastFrame).count ());
    } else {
	s.firstFrame = now;
    }

    s.lastFrame = now;
    s.frames++;
}
