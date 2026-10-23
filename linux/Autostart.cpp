/* FxSound Linux: stable launch-on-login entries using the freedesktop format. */
#include "Autostart.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <cerrno>
#include <utility>
#include <unistd.h>

namespace {
std::string quoteArgument(const std::string& text)
{
    // GKeyFile applies the outer Desktop Entry string escaping on serialization.
    // These are the inner Exec quoting rules, including literal percent signs.
    // https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html
    std::string value = "\"";
    for (char c : text) {
        if (c == '%') value += "%%";
        else {
            if (c == '\\' || c == '"' || c == '$' || c == '`') value += '\\';
            value += c;
        }
    }
    return value + '"';
}
bool booleanValue(GKeyFile* file, const char* key, bool fallback)
{
    GError* error = nullptr;
    bool value = g_key_file_get_boolean(file, "Desktop Entry", key, &error);
    if (error) { g_error_free(error); return fallback; }
    return value;
}
}

bool FxSound::Autostart::enable(const std::string& desktopFile, const std::string& executable,
                              const std::string& appImage, const std::string& appDir, std::string& error)
{
    gchar* canonicalDir = g_canonicalize_filename(appDir.c_str(), nullptr);
    gchar* canonicalExe = g_canonicalize_filename(executable.c_str(), nullptr);
    std::string prefix = std::string(canonicalDir) + "/";
    bool imageLaunch = !appDir.empty() && std::string(canonicalExe).compare(0, prefix.size(), prefix) == 0;
    g_free(canonicalDir); g_free(canonicalExe);
    const std::string& launch = imageLaunch ? appImage : executable;
    if ((imageLaunch && appImage.empty()) || !g_path_is_absolute(launch.c_str()) ||
        !g_file_test(launch.c_str(), G_FILE_TEST_IS_REGULAR) || g_access(launch.c_str(), X_OK) != 0) {
        error = "The persistent executable is unavailable";
        return false;
    }
    // Some launchers resolve the program token before percent expansion, and
    // '=' is forbidden in that token by the specification. For these filenames
    // use a constant shell program with the filename in a positional argument;
    // the filename is never interpolated into shell source.
    std::string exec = launch.find_first_of("%=") == std::string::npos
        ? quoteArgument(launch) + " --autostart"
        : "/bin/sh -c " + quoteArgument("exec \"$1\" --autostart") +
          " fxsound-autostart " + quoteArgument(launch);
    GKeyFile* file = g_key_file_new();
    for (auto pair : {std::pair{"Type", "Application"}, {"Name", "FxSound"}, {"Icon", "fxsound"},
                      {"Terminal", "false"}, {"X-GNOME-Autostart-enabled", "true"},
                      {"X-FxSound-LaunchKind", imageLaunch ? "appimage" : "native"}})
        g_key_file_set_string(file, "Desktop Entry", pair.first, pair.second);
    g_key_file_set_string(file, "Desktop Entry", "Exec", exec.c_str());
    g_key_file_set_string(file, "Desktop Entry", "TryExec", launch.c_str());
    gsize length = 0;
    gchar* contents = g_key_file_to_data(file, &length, nullptr);
    gchar* parent = g_path_get_dirname(desktopFile.c_str());
    bool ok = g_mkdir_with_parents(parent, 0700) == 0;
    if (!ok) error = g_strerror(errno);
    GError* failure = nullptr;
    if (ok) ok = g_file_set_contents(desktopFile.c_str(), contents, length, &failure);
    if (failure) { error = failure->message; g_error_free(failure); }
    g_free(parent); g_free(contents); g_key_file_unref(file);
    return ok;
}

bool FxSound::Autostart::disable(const std::string& desktopFile, std::string& error)
{
    if (g_unlink(desktopFile.c_str()) == 0 || errno == ENOENT) return true;
    error = g_strerror(errno);
    return false;
}

bool FxSound::Autostart::isEnabled(const std::string& desktopFile)
{
    GKeyFile* file = g_key_file_new();
    bool enabled = g_key_file_load_from_file(file, desktopFile.c_str(), G_KEY_FILE_NONE, nullptr);
    enabled = enabled && !booleanValue(file, "Hidden", false) && booleanValue(file, "X-GNOME-Autostart-enabled", true);
    gchar* exec = g_key_file_get_string(file, "Desktop Entry", "Exec", nullptr);
    gchar* check = g_key_file_get_string(file, "Desktop Entry", "TryExec", nullptr);
    enabled = enabled && exec && *exec;
    if (check && *check) {
        gchar* resolved = g_find_program_in_path(check);
        enabled = enabled && resolved;
        g_free(resolved);
    }
    g_free(exec); g_free(check); g_key_file_unref(file);
    return enabled;
}
