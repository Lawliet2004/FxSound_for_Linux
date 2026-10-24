/* FxSound Linux: durable, per-server recovery of our configured default. */
#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

class DefaultSinkRecovery
{
public:
    DefaultSinkRecovery()
    {
        const char* state = std::getenv("XDG_STATE_HOME");
        const char* home = std::getenv("HOME");
        std::filesystem::path base;
        if (state && std::filesystem::path(state).is_absolute()) base = state;
        else if (home && *home) base = std::filesystem::path(home) / ".local/state";
        else return;
        const char* runtime = std::getenv("PIPEWIRE_RUNTIME_DIR");
        if (!runtime || !*runtime) runtime = std::getenv("XDG_RUNTIME_DIR");
        const char* remote = std::getenv("PIPEWIRE_REMOTE");
        std::string endpoint = std::string(runtime ? runtime : "") + "/" +
                               (remote && *remote ? remote : "pipewire-0");
        // A stable hash scopes journals to audio servers, independently of PID.
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned char byte : endpoint) { hash ^= byte; hash *= UINT64_C(1099511628211); }
        path_ = base / "fxsound" / ("default-sink-" + std::to_string(hash) + ".recovery");
    }

    bool load(std::string& previous) const
    {
        int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) return false;
        struct stat status{};
        std::string bytes;
        bool ok = ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
                  status.st_size >= 0 && status.st_size <= maxBytes;
        char buffer[4096];
        while (ok) {
            auto size = ::read(fd, buffer, sizeof(buffer));
            if (size < 0 && errno == EINTR) continue;
            if (size < 0) { ok = false; break; }
            if (!size) break;
            bytes.append(buffer, static_cast<size_t>(size));
            if (bytes.size() > maxBytes) ok = false;
        }
        ::close(fd);
        if (!ok || bytes.compare(0, magic.size(), magic) != 0) return false;
        previous = bytes.substr(magic.size());
        return true;
    }

    bool save(const std::string& previous) const
    {
        if (path_.empty() || previous.size() + magic.size() > maxBytes) return false;
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) return false;
        std::string temporary = path_.string() + ".XXXXXX";
        int fd = ::mkstemp(temporary.data()); // private 0600, same filesystem
        if (fd < 0) return false;
        std::string bytes = magic + previous;
        size_t written = 0;
        bool ok = true;
        while (written < bytes.size()) {
            auto count = ::write(fd, bytes.data() + written, bytes.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { ok = false; break; }
            written += static_cast<size_t>(count);
        }
        if (ok) ok = ::fsync(fd) == 0;
        if (::close(fd) != 0) ok = false;
        if (ok) ok = ::rename(temporary.c_str(), path_.c_str()) == 0;
        if (!ok) ::unlink(temporary.c_str());
        if (ok) {
            int directory = ::open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            ok = directory >= 0 && ::fsync(directory) == 0;
            if (directory >= 0) ::close(directory);
        }
        return ok;
    }

    void clear() const { if (!path_.empty()) ::unlink(path_.c_str()); }

private:
    static constexpr size_t maxBytes = 65536;
    inline static const std::string magic = "FxSound default sink recovery v1\n";
    std::filesystem::path path_;
};
