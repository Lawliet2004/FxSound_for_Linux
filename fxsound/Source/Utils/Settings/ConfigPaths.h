/* FxSound: platform configuration root shared by settings, presets and logs. */
#pragma once
#include <JuceHeader.h>
#include <cstdlib>

namespace FxSound {
inline juce::File getUserConfigRoot()
{
#ifdef _WIN32
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#else
    // JUCE 6.1.6 reads user-dirs.dirs here rather than the XDG environment.
    // XDG_CONFIG_HOME must be absolute; empty/relative values use the default.
    if (const char* value = std::getenv("XDG_CONFIG_HOME")) {
        auto path = juce::String::fromUTF8(value);
        if (juce::File::isAbsolutePath(path)) return juce::File(path);
    }
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".config");
#endif
}
}
