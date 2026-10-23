#include "PortalHotkeys.h"
#include <gio/gio.h>
#include <chrono>
#include <algorithm>

namespace {
constexpr auto busName = "org.freedesktop.portal.Desktop";
constexpr auto object = "/org/freedesktop/portal/desktop";
constexpr auto iface = "org.freedesktop.portal.GlobalShortcuts";
struct Responses {
    std::map<std::string, GVariant*> values;
    ~Responses() { for (auto& v : values) g_variant_unref(v.second); }
};
void response(GDBusConnection*, const gchar*, const gchar* path, const gchar*,
              const gchar*, GVariant* value, gpointer data) {
    auto& values = static_cast<Responses*>(data)->values;
    auto it = values.find(path);
    if (it != values.end()) g_variant_unref(it->second);
    values[path] = g_variant_ref(value);
}
void closeObject(GDBusConnection* bus, const std::string& path, const char* interface) {
    if (!path.empty()) g_dbus_connection_call(bus, busName, path.c_str(), interface,
        "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
}
}
PortalHotkeys::PortalHotkeys(std::function<void(int)> cb) : callback_(std::move(cb)),
    worker_(&PortalHotkeys::run, this) {}
PortalHotkeys::~PortalHotkeys() { stopping_ = true; if (worker_.joinable()) worker_.join(); }
bool PortalHotkeys::set(int command, int modifiers, int key) {
    std::string trigger;
    if (modifiers & ~7) return false;
    if (modifiers & 2) trigger += "CTRL+";
    if (modifiers & 1) trigger += "ALT+";
    if (modifiers & 4) trigger += "SHIFT+";
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) trigger += char(key);
    else if (key >= 0x70 && key <= 0x7b) trigger += "F" + std::to_string(key - 0x70 + 1);
    else return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (bindings_[command] != trigger) { bindings_[command] = trigger; ++generation_; }
    return true; // Queued: compositor permission is asynchronous.
}
void PortalHotkeys::remove(int command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bindings_.erase(command)) ++generation_;
}
void PortalHotkeys::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!bindings_.empty()) { bindings_.clear(); ++generation_; }
}
void PortalHotkeys::run() {
    auto* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GError* error = nullptr;
    auto* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    auto* bus = address ? g_dbus_connection_new_for_address_sync(address,
        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                             G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error) : nullptr;
    g_free(address);
    if (error) { g_warning("FxSound shortcuts: %s", error->message); g_error_free(error); }
    if (bus) {
        g_dbus_connection_set_exit_on_close(bus, false);
        Responses responses;
        std::string session;
        unsigned applied = 0;
        bool registered = false;
        const auto subscription = g_dbus_connection_signal_subscribe(bus, busName,
            "org.freedesktop.portal.Request", "Response", nullptr, nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, response, &responses, nullptr);
        struct Activation { PortalHotkeys* owner; std::string* session; unsigned* applied; bool* registered; } activation{this, &session, &applied, &registered};
        const auto activated = g_dbus_connection_signal_subscribe(bus, busName, iface,
            "Activated", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* args, gpointer data) {
                auto& a = *static_cast<Activation*>(data);
                const char *path, *id; guint64 timestamp; GVariant* options;
                g_variant_get(args, "(&o&st@a{sv})", &path, &id, &timestamp, &options);
                if (*a.session == path && *a.applied == a.owner->generation_.load()) {
                    int command = -1;
                    {
                        std::lock_guard<std::mutex> lock(a.owner->mutex_);
                        for (const auto& b : a.owner->bindings_)
                            if (std::to_string(b.first) == id) { command = b.first; break; }
                    }
                    if (command >= 0) a.owner->callback_(command);
                }
                g_variant_unref(options);
            }, &activation, nullptr);
        const auto ownerChanged = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus",
            "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", busName,
            G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* args, gpointer data) {
                auto& a = *static_cast<Activation*>(data);
                const char *name, *oldOwner, *newOwner;
                g_variant_get(args, "(&s&s&s)", &name, &oldOwner, &newOwner);
                if (*oldOwner) *a.registered = false;
                ++a.owner->generation_;
            }, &activation, nullptr);
        auto request = [&](const char* method, GVariant* args, unsigned generation) -> GVariant* {
            GError* failure = nullptr;
            auto* reply = g_dbus_connection_call_sync(bus, busName, object, iface, method, args,
                G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &failure);
            if (!reply) {
                if (failure) { g_warning("FxSound shortcuts %s: %s", method, failure->message); g_error_free(failure); }
                return nullptr;
            }
            const char* path; g_variant_get(reply, "(&o)", &path);
            std::string requestPath(path); g_variant_unref(reply);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (!stopping_ && generation_ == generation && std::chrono::steady_clock::now() < deadline) {
                while (g_main_context_iteration(context, false)) {}
                auto it = responses.values.find(requestPath);
                if (it != responses.values.end()) {
                    auto* value = it->second; responses.values.erase(it);
                    guint code; GVariant* result;
                    g_variant_get(value, "(u@a{sv})", &code, &result); g_variant_unref(value);
                    if (code == 0) return result;
                    g_variant_unref(result);
                    g_warning("FxSound shortcuts: permission denied or request cancelled (%u)", code);
                    return nullptr;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            closeObject(bus, requestPath, "org.freedesktop.portal.Request");
            return nullptr;
        };
        while (!stopping_ && !g_dbus_connection_is_closed(bus)) {
            while (g_main_context_iteration(context, false)) {}
            unsigned wanted = generation_.load();
            if (wanted != applied) {
                // Coalesce the five registerKey calls into one permission request.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::map<int, std::string> bindings;
                { std::lock_guard<std::mutex> lock(mutex_); bindings = bindings_; wanted = generation_; }
                closeObject(bus, session, "org.freedesktop.portal.Session"); session.clear();
                applied = wanted;
                if (!bindings.empty()) {
                    if (!registered) {
                        GVariantBuilder registration; g_variant_builder_init(&registration, G_VARIANT_TYPE_VARDICT);
                        GError* failure = nullptr;
                        auto* reply = g_dbus_connection_call_sync(bus, busName, object,
                            "org.freedesktop.host.portal.Registry", "Register",
                            g_variant_new("(sa{sv})", "fxsound", &registration), nullptr,
                            G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &failure);
                        if (reply) g_variant_unref(reply);
                        // Older portals may identify the app without Registry.
                        if (failure) g_error_free(failure);
                        registered = true;
                    }
                    GVariantBuilder options; g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
                    const auto token = "fxsound_" + std::to_string(wanted);
                    g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(token.c_str()));
                    auto* result = request("CreateSession", g_variant_new("(a{sv})", &options), wanted);
                    if (result) {
                        const char* path = nullptr;
                        if (g_variant_lookup(result, "session_handle", "&s", &path)) session = path;
                        g_variant_unref(result);
                    }
                    if (!session.empty()) {
                        GVariantBuilder keys; g_variant_builder_init(&keys, G_VARIANT_TYPE("a(sa{sv})"));
                        for (const auto& binding : bindings) {
                            GVariantBuilder properties; g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
                            const char* description = "FxSound";
                            switch (binding.first) {
                                case 1001: description = "Toggle audio processing"; break;
                                case 1002: description = "Show or hide FxSound"; break;
                                case 1003: description = "Next preset"; break;
                                case 1004: description = "Previous preset"; break;
                                case 1005: description = "Next output device"; break;
                            }
                            g_variant_builder_add(&properties, "{sv}", "description", g_variant_new_string(description));
                            g_variant_builder_add(&properties, "{sv}", "preferred_trigger", g_variant_new_string(binding.second.c_str()));
                            g_variant_builder_add(&keys, "(sa{sv})", std::to_string(binding.first).c_str(), &properties);
                        }
                        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
                        result = request("BindShortcuts", g_variant_new("(oa(sa{sv})sa{sv})", session.c_str(), &keys, "", &options), wanted);
                        if (result) g_variant_unref(result);
                        else { closeObject(bus, session, "org.freedesktop.portal.Session"); session.clear(); }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        closeObject(bus, session, "org.freedesktop.portal.Session");
        g_dbus_connection_signal_unsubscribe(bus, ownerChanged);
        g_dbus_connection_signal_unsubscribe(bus, activated);
        g_dbus_connection_signal_unsubscribe(bus, subscription);
        g_dbus_connection_flush_sync(bus, nullptr, nullptr);
        g_dbus_connection_close_sync(bus, nullptr, nullptr);
        g_object_unref(bus);
    }
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
}
