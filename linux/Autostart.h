/* Linux login integration. Desktop Entry syntax, not shell command syntax. */
#pragma once
#include <string>

namespace FxSound::Autostart {
// The AppImage runtime supplies its persistent filename in APPIMAGE. APPDIR
// identifies a temporary extracted/mounted tree and must never become Exec.
bool enable(const std::string& desktopFile, const std::string& executable,
            const std::string& appImage, const std::string& appDir, std::string& error);
bool disable(const std::string& desktopFile, std::string& error);
bool isEnabled(const std::string& desktopFile);
}
