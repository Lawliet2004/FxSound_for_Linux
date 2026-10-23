#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <string>

// Owns a private bus connection and GLib context; never blocks the GUI thread.
class PortalHotkeys {
public:
    explicit PortalHotkeys(std::function<void(int)> callback);
    ~PortalHotkeys();
    bool set(int command, int modifiers, int key);
    void remove(int command);
    void clear();
private:
    void run();
    std::function<void(int)> callback_;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> generation_{0};
    std::mutex mutex_;
    std::map<int, std::string> bindings_;
    std::thread worker_;
};
