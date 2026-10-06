#pragma once
#include <memory>
#include <string>

namespace WallpaperEngine::Audio {
enum class SoundOutput { Unknown, Speakers, Headphones };

SoundOutput classifySoundOutput (unsigned channels, unsigned portType, const char* portName, const char* formFactor);
bool matchesSoundStream (const std::string& streamName, int processId, const char* name, const char* owner);

/** Read-only observer of the sink actually used by the identified SDL stream. */
class SoundOutputMetadata {
public:
    SoundOutputMetadata (std::string streamName, int processId);
    ~SoundOutputMetadata ();
    void update ();
    SoundOutput output () const;
private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
