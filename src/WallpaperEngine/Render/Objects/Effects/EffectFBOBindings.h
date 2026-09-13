#pragma once

#include <map>
#include <string>
#include <vector>

namespace WallpaperEngine::Render::Objects::Effects {
class EffectFBOBindings {
public:
    explicit EffectFBOBindings (const std::vector<std::string>& names) {
	for (const auto& name : names) m_names.emplace (name, name);
    }

    [[nodiscard]] const std::string& resolveName (const std::string& name) const {
	const auto found = m_names.find (name);
	return found == m_names.end () ? name : found->second;
    }

    void swap (const std::string& source, const std::string& target) {
	const auto from = m_names.find (source);
	const auto to = m_names.find (target);
	if (from == m_names.end () || to == m_names.end () || from == to) return;

	// Native swap commands transpose references to fixed physical FBOs.
	for (auto& entry : m_names) {
	    if (entry.second == from->first) entry.second = to->first;
	    else if (entry.second == to->first) entry.second = from->first;
	}
    }

private:
    std::map<std::string, std::string> m_names;
};
}
