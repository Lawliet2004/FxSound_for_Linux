/* Exercise the login entry through the desktop's real GIO parser/launcher. */
#include "Autostart.h"
#include <gio/gdesktopappinfo.h>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    const std::string action = argv[1];
    std::string error;
    if (action == "create" && argc == 6) {
        bool ok = FxSound::Autostart::enable(argv[2], argv[3], argv[4], argv[5], error);
        if (!ok) std::cerr << error << '\n';
        return ok ? 0 : 1;
    }
    if (action == "enabled") return FxSound::Autostart::isEnabled(argv[2]) ? 0 : 1;
    if (action == "disable") return FxSound::Autostart::disable(argv[2], error) ? 0 : 1;
    if (action == "launch") {
        auto* info = g_desktop_app_info_new_from_filename(argv[2]);
        if (!info) { std::cerr << "Desktop Entry parsing failed\n"; return 1; }
        GError* failure = nullptr;
        bool ok = g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, &failure);
        if (failure) { std::cerr << failure->message << '\n'; g_error_free(failure); }
        g_object_unref(info);
        return ok ? 0 : 1;
    }
    return 2;
}
