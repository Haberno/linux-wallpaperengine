#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace WallpaperEngine::WebBrowser::CEF {
struct ResourceRange {
    int64_t offset;
    int64_t length;
};

inline std::optional<ResourceRange> parseResourceRange (std::string_view header, int64_t length) {
    if (header.empty ()) {
	return ResourceRange { 0, length };
    }
    if (!header.starts_with ("bytes=") || length <= 0) {
	return std::nullopt;
    }
    header.remove_prefix (6);
    const auto dash = header.find ('-');
    if (dash == std::string_view::npos) {
	return std::nullopt;
    }
    const auto number = [] (std::string_view text, int64_t& result) {
	const auto parsed = std::from_chars (text.data (), text.data () + text.size (), result);
	return parsed.ec == std::errc {} && parsed.ptr == text.data () + text.size () && result >= 0;
    };
    int64_t first = 0, last = length - 1;
    if (dash == 0) {
	int64_t suffix;
	if (!number (header.substr (1), suffix) || suffix == 0) {
	    return std::nullopt;
	}
	first = length - std::min (length, suffix);
    } else {
	if (!number (header.substr (0, dash), first) || first >= length) {
	    return std::nullopt;
	}
	if (dash + 1 < header.size () && !number (header.substr (dash + 1), last)) {
	    return std::nullopt;
	}
	last = std::min (last, length - 1);
	if (last < first) {
	    return std::nullopt;
	}
    }
    return ResourceRange { first, last - first + 1 };
}
} // namespace WallpaperEngine::WebBrowser::CEF
