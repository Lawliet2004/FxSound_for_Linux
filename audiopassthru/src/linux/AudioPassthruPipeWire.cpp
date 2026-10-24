/*
FxSound — Linux build: PipeWire backend

Linux implementation of AudioPassthru on libpipewire. Two nodes:
  - "FxSound": a real null-audio-sink (support.null-audio-sink) so WirePlumber
    treats it as a first-class routable/default playback sink that apps play
    into (a bare pw_filter sink is NOT routed by the policy). Its monitor ports
    carry whatever apps played.
  - "FxSound Processor": a hidden pw_filter whose inputs are linked from the
    sink monitor and whose outputs are linked to the chosen device. The realtime
    callback runs DfxDsp::processAudio between them.
A registry listener enumerates real sinks (getSoundDevices), tracks our two
nodes' ports, and (re)creates the monitor->processor and processor->device
links; hot-plug add/remove notifies the app.

Threading: all PipeWire calls happen on the pw_thread_loop thread; public
methods take the loop lock. The realtime process callback is lock-free.
*/

#include "AudioPassthru.h"
#include "DfxDsp.h"
#include "PipeWireProcess.h"

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/extensions/metadata.h>
#include <spa/utils/json.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <codecvt>
#include <locale>
#include <memory>
#include <mutex>
#include "DefaultSinkRecovery.h"
#include <set>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr int kChannels = 2;

struct NodeInfo {
    uint32_t id = 0;
    std::string name;
    std::string description;
    std::string media_class;
};
struct PortInfo {
    uint32_t id = 0;
    uint32_t node_id = 0;
    enum pw_direction direction = PW_DIRECTION_INPUT;
    bool is_monitor = false;
    std::string channel;   // e.g. "FL", "FR"
    std::string name;
};
// Proxies may disappear independently of their endpoints (pw-link -d, policy,
// or server errors). Keep listeners at stable addresses and reconcile later.
struct OwnedLink {
    pw_proxy* proxy = nullptr;
    spa_hook listener{};
    bool dead = false;
    ~OwnedLink() {
        if (proxy) { spa_hook_remove(&listener); pw_proxy_destroy(proxy); }
    }
};
using LinkKey = std::pair<uint32_t, uint32_t>;
using Links = std::map<LinkKey, std::unique_ptr<OwnedLink>>;
} // namespace

struct AudioPassthruPrivate
{
    pw_thread_loop* loop = nullptr;
    pw_context*     context = nullptr;
    pw_core*        core = nullptr;
    pw_filter*      filter = nullptr;
    pw_proxy*       sink_proxy = nullptr;     // the routable null-audio-sink "FxSound"
    pw_registry*    registry = nullptr;
    spa_hook        registry_listener{};
    spa_hook        filter_listener{};
    spa_hook        core_listener{};
    int             done_seq = -1;
    bool            connection_error = false;
    bool            loop_started = false;
    bool            library_initialized = false;
    bool            metadata_ready = false;
    bool            stopping = false;

    uint32_t        filter_node_id = SPA_ID_INVALID;  // hidden processor node
    uint32_t        sink_node_id = SPA_ID_INVALID;    // FxSound null-sink node
    Links monitor_links;             // sink monitor -> filter input

    // "default" metadata: used to make FxSound the default sink on startup and
    // restore the previous default on exit.
    pw_metadata*    metadata = nullptr;
    uint32_t        metadata_id = SPA_ID_INVALID;
    spa_hook        metadata_listener{};
    std::string     prev_default_sink;     // value to restore on exit
    bool            captured_default = false;
    bool            default_set = false;
    std::string     configured_sink_name;
    std::string     configured_sink_json;
    DefaultSinkRecovery recovery;

    float* in_ports[kChannels]  = {nullptr, nullptr};
    float* out_ports[kChannels] = {nullptr, nullptr};

    FxProcessor processor;   // realtime DSP block (see PipeWireProcess.h)
    AudioPassthruCallback* callback = nullptr;

    // Graph model (guarded by the thread-loop lock).
    std::map<uint32_t, NodeInfo> nodes;
    std::map<uint32_t, PortInfo> ports;
    Links links;        // links we created (FxSound -> device)
    std::string default_sink_name;       // from the default-metadata
    bool        have_configured_sink = false;  // default.configured.audio.sink seen
    std::string selected_sink_name;      // user choice; empty => default

    bool started = false;
    bool initialization_requested = false;
    std::chrono::steady_clock::time_point next_retry{};
};

namespace {

std::wstring fromUtf8(const std::string& value) {
    try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(value); }
    catch (const std::range_error&) { return L""; }
}
std::string toUtf8(const std::wstring& value) {
    try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(value); }
    catch (const std::range_error&) { return ""; }
}

// ---- realtime processing ------------------------------------------------
void on_process(void* userdata, struct spa_io_position* position)
{
    if (!position) return;
    auto* d = static_cast<AudioPassthruPrivate*>(userdata);
    uint32_t n = position->clock.duration;

    float* in[kChannels];
    float* out[kChannels];
    for (int c = 0; c < kChannels; ++c) {
        in[c]  = static_cast<float*>(pw_filter_get_dsp_buffer(d->in_ports[c], n));
        out[c] = static_cast<float*>(pw_filter_get_dsp_buffer(d->out_ports[c], n));
    }
    for (int c = 0; c < kChannels; ++c) if (!out[c]) return;

    int rate = (position->clock.rate.denom > 0) ? (int)position->clock.rate.denom : 48000;
    d->processor.process(in, out, n, rate);
}



// ---- linking helpers ----------------------------------------------------
void link_destroyed(void* userdata) {
    auto* link = static_cast<OwnedLink*>(userdata);
    spa_hook_remove(&link->listener);
    link->proxy = nullptr;
    link->dead = true;
}
void link_removed(void* userdata) { static_cast<OwnedLink*>(userdata)->dead = true; }
void link_error(void* userdata, int, int, const char*) { link_removed(userdata); }
const pw_proxy_events link_events = {
    .version = PW_VERSION_PROXY_EVENTS,
    .destroy = link_destroyed,
    .removed = link_removed,
    .error = link_error,
};

// Preserve existing links, including their DSP graph state, unless an endpoint
// actually changes. Unrelated graph events must not interrupt playback.
void reconcileLinks(AudioPassthruPrivate* d, Links& links, const std::set<LinkKey>& wanted)
{
    for (auto it = links.begin(); it != links.end();) {
        if (it->second->dead || !wanted.count(it->first)) it = links.erase(it);
        else ++it;
    }
    for (const auto& key : wanted) {
        if (links.count(key)) continue;
        char output[16], input[16];
        std::snprintf(output, sizeof(output), "%u", key.first);
        std::snprintf(input, sizeof(input), "%u", key.second);
        auto* props = pw_properties_new(PW_KEY_LINK_OUTPUT_PORT, output,
            PW_KEY_LINK_INPUT_PORT, input, PW_KEY_OBJECT_LINGER, "false", nullptr);
        if (!props) continue;
        auto link = std::make_unique<OwnedLink>();
        link->proxy = static_cast<pw_proxy*>(pw_core_create_object(d->core,
            "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
        pw_properties_free(props);
        if (link->proxy) {
            pw_proxy_add_listener(link->proxy, &link->listener, &link_events, link.get());
            links.emplace(key, std::move(link));
        }
    }
}

void linkNodes(AudioPassthruPrivate* d, Links& links, uint32_t outputNode, uint32_t inputNode, bool monitor)
{
    std::vector<const PortInfo*> outputs, inputs;
    for (const auto& [id, port] : d->ports) {
        if (port.node_id == outputNode && port.direction == PW_DIRECTION_OUTPUT && (!monitor || port.is_monitor)) outputs.push_back(&port);
        if (port.node_id == inputNode && port.direction == PW_DIRECTION_INPUT) inputs.push_back(&port);
    }
    auto byChannel = [](const auto& ports, const char* channel, int fallback) -> const PortInfo* {
        for (auto* port : ports) if (port->channel == channel) return port;
        return static_cast<size_t>(fallback) < ports.size() ? ports[fallback] : nullptr;
    };
    std::set<LinkKey> wanted;
    if (outputNode != SPA_ID_INVALID && inputNode != SPA_ID_INVALID) {
        const char* channels[kChannels] = {"FL", "FR"};
        for (int c = 0; c < kChannels; ++c) {
            const auto* output = byChannel(outputs, channels[c], c);
            const auto* input = byChannel(inputs, channels[c], c);
            if (output && input) wanted.emplace(output->id, input->id);
        }
    }
    reconcileLinks(d, links, wanted);
}

uint32_t resolveTargetSink(AudioPassthruPrivate* d)
{
    const std::string& want = !d->selected_sink_name.empty() ? d->selected_sink_name
                                                              : d->default_sink_name;
    // Prefer exact name match; otherwise first real Audio/Sink that isn't us.
    uint32_t fallback = SPA_ID_INVALID;
    for (auto& kv : d->nodes) {
        const NodeInfo& n = kv.second;
        if (n.media_class != "Audio/Sink" || n.name == "FxSound") continue;
        if (!want.empty() && n.name == want) return n.id;
        if (fallback == SPA_ID_INVALID) fallback = n.id;
    }
    return fallback;
}

void reroute(AudioPassthruPrivate* d)
{
    if (d->stopping || d->connection_error) return;
    linkNodes(d, d->links, d->filter_node_id, resolveTargetSink(d), false);
    linkNodes(d, d->monitor_links, d->sink_node_id, d->filter_node_id, true);
}

// Server acknowledgments replace sleeps. Must be called with the loop locked;
// timed_wait releases it so callbacks can acknowledge requests or report errors.
void core_done(void* data, uint32_t id, int seq) {
    auto* d = static_cast<AudioPassthruPrivate*>(data);
    if (id == PW_ID_CORE) { d->done_seq = seq; pw_thread_loop_signal(d->loop, false); }
}
void core_error(void* data, uint32_t id, int, int result, const char* message) {
    auto* d = static_cast<AudioPassthruPrivate*>(data);
    // A port can disappear while its link request is in flight. In particular,
    // "unknown resource" (-ENOENT) may be reported on the core itself when we
    // release that failed proxy. Such errors do not mean the connection died.
    if (result == -EPIPE || result == -ECONNRESET || result == -ENOTCONN ||
        result == -ECONNABORTED || (id == PW_ID_CORE && result == -EPROTO)) {
        std::fprintf(stderr, "PipeWire connection error: %s (%d)\n", message ? message : "", result);
        d->connection_error = true;
        d->started = false;
        pw_thread_loop_signal(d->loop, false);
    }
}
const pw_core_events core_events = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = core_done,
    .error = core_error,
};
bool roundtrip(AudioPassthruPrivate* d) {
    if (!d->core || d->connection_error || !d->loop_started) return false;
    const int sequence = pw_core_sync(d->core, PW_ID_CORE, 0);
    if (sequence < 0) return false;
    timespec deadline;
    pw_thread_loop_get_time(d->loop, &deadline, 2 * SPA_NSEC_PER_SEC);
    while (d->done_seq != sequence && !d->connection_error) {
        if (pw_thread_loop_timed_wait_full(d->loop, &deadline) < 0) return false;
    }
    return !d->connection_error;
}
bool awaitConfiguredSink(AudioPassthruPrivate* d, const std::string& expected) {
    // Metadata is owned by another client (usually WirePlumber). A core sync
    // flushes our request, but that client's property echo can arrive later.
    timespec deadline;
    pw_thread_loop_get_time(d->loop, &deadline, 2 * SPA_NSEC_PER_SEC);
    while (d->configured_sink_name != expected && !d->connection_error) {
        if (pw_thread_loop_timed_wait_full(d->loop, &deadline) < 0) return false;
    }
    return !d->connection_error;
}
void filter_state_changed(void* userdata, pw_filter_state, pw_filter_state state, const char*) {
    auto* d = static_cast<AudioPassthruPrivate*>(userdata);
    if (state == PW_FILTER_STATE_ERROR) { d->connection_error = true; d->started = false; }
    d->filter_node_id = pw_filter_get_node_id(d->filter);
    reroute(d);
    pw_thread_loop_signal(d->loop, false);
}

const pw_filter_events filter_events = {
    .version = PW_VERSION_FILTER_EVENTS,
    .state_changed = filter_state_changed,
    .process = on_process,
};

// ---- default-sink metadata ----------------------------------------------
std::string sinkNameFromJson(const char* value);
void setDefaultSinkToFxSound(AudioPassthruPrivate* d)
{
    if (!d->metadata || !d->metadata_ready || d->sink_node_id == SPA_ID_INVALID || d->default_set || d->stopping) return;
    d->prev_default_sink = d->configured_sink_json;
    if (d->configured_sink_name == "FxSound") {
        std::string saved;
        if (d->recovery.load(saved) &&
            (saved.empty() || (!sinkNameFromJson(saved.c_str()).empty() && sinkNameFromJson(saved.c_str()) != "FxSound"))) {
            d->prev_default_sink = saved;
            if (!saved.empty()) d->default_sink_name = sinkNameFromJson(saved.c_str());
        }
    }
    // Save before claiming the default. If state cannot be persisted, keep
    // the user's existing routing and let the control timer retry later.
    if (!d->recovery.save(d->prev_default_sink)) {
        std::fprintf(stderr, "FxSound: cannot persist default-sink recovery state; leaving the default unchanged\n");
        return;
    }
    d->captured_default = true; // An absent configured default is a state too.
    d->default_set = pw_metadata_set_property(d->metadata, 0, "default.configured.audio.sink",
                             "Spa:String:JSON", "{ \"name\": \"FxSound\" }") >= 0;
}

// Extract "name" from a default-sink metadata value: { "name": "<node.name>" }.
std::string sinkNameFromJson(const char* value)
{
    struct spa_json it[2];
    char key[64], name[512];
    spa_json_init(&it[0], value, strlen(value));
    if (spa_json_enter_object(&it[0], &it[1]) <= 0) return "";
    while (spa_json_get_string(&it[1], key, sizeof(key)) > 0) {
        if (spa_streq(key, "name"))
            return spa_json_get_string(&it[1], name, sizeof(name)) > 0 ? name : "";
        const char* skip;
        if (spa_json_next(&it[1], &skip) <= 0) break;
    }
    return "";
}

int metadata_property(void* data, uint32_t subject, const char* key,
                      const char* /*type*/, const char* value)
{
    auto* d = static_cast<AudioPassthruPrivate*>(data);
    if (subject != 0) return 0;
    if (!key) {
        // A client may clear every property for subject 0 in one operation.
        // Do not mistake the stale cached FxSound value for continued ownership
        // and overwrite this explicit user change during shutdown.
        d->configured_sink_name.clear();
        d->configured_sink_json.clear();
        d->have_configured_sink = false;
        pw_thread_loop_signal(d->loop, false);
        return 0;
    }
    // Track the user's real default device (never ourselves) so that with no
    // explicit selection the processor feeds it rather than the lowest-id sink.
    // The configured value is the user's choice; the effective default.audio.sink
    // briefly reports another sink while WirePlumber switches to FxSound, so it
    // is only used when nothing has been configured.
    bool configured = spa_streq(key, "default.configured.audio.sink");
    if (configured) {
        d->configured_sink_name = value ? sinkNameFromJson(value) : "";
        d->configured_sink_json = value ? value : "";
        d->have_configured_sink = !d->configured_sink_name.empty();
        pw_thread_loop_signal(d->loop, false);
    }
    if (value && (configured || (spa_streq(key, "default.audio.sink") && !d->have_configured_sink)))
    {
        std::string name = sinkNameFromJson(value);
        if (!name.empty() && name != "FxSound")
        {
            if (configured) d->have_configured_sink = true;
            if (name != d->default_sink_name)
            {
                d->default_sink_name = name;
                if (d->selected_sink_name.empty()) reroute(d);
            }
        }
    }
    return 0;
}

const struct pw_metadata_events metadata_events = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = metadata_property,
};

// ---- registry listener --------------------------------------------------
void registry_global(void* data, uint32_t id, uint32_t /*perm*/, const char* type,
                     uint32_t /*ver*/, const struct spa_dict* props)
{
    auto* d = static_cast<AudioPassthruPrivate*>(data);
    if (!props) return;

    if (spa_streq(type, PW_TYPE_INTERFACE_Node)) {
        const char* mc = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        const char* nm = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        const char* de = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        NodeInfo ni;
        ni.id = id;
        ni.name = nm ? nm : "";
        ni.description = de ? de : (nm ? nm : "");
        ni.media_class = mc ? mc : "";
        d->nodes[id] = ni;
        // Our own routable null-sink front-end.
        if (mc && spa_streq(mc, "Audio/Sink") && ni.name == "FxSound")
            d->sink_node_id = id;
        reroute(d);
    } else if (spa_streq(type, PW_TYPE_INTERFACE_Metadata)) {
        const char* mn = spa_dict_lookup(props, "metadata.name");
        if (mn && spa_streq(mn, "default") && !d->metadata) {
            d->metadata = static_cast<pw_metadata*>(pw_registry_bind(
                d->registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
            if (d->metadata) {
                d->metadata_id = id;
                pw_metadata_add_listener(d->metadata, &d->metadata_listener,
                                         &metadata_events, d);
            }
        }
    } else if (spa_streq(type, PW_TYPE_INTERFACE_Port)) {
        const char* nid = spa_dict_lookup(props, PW_KEY_NODE_ID);
        const char* dir = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
        const char* ch  = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL);
        const char* nm  = spa_dict_lookup(props, PW_KEY_PORT_NAME);
        const char* mon = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);
        PortInfo pi;
        pi.id = id;
        pi.node_id = nid ? (uint32_t)strtoul(nid, nullptr, 10) : 0;
        pi.direction = (dir && spa_streq(dir, "out")) ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;
        pi.is_monitor = (mon && spa_streq(mon, "true"));
        pi.channel = ch ? ch : "";
        pi.name = nm ? nm : "";
        d->ports[id] = pi;
        // A new port on our nodes or the target may complete a routing.
        reroute(d);
    }
}

void registry_global_remove(void* data, uint32_t id)
{
    auto* d = static_cast<AudioPassthruPrivate*>(data);
    if (!d->stopping && (id == d->sink_node_id || id == d->filter_node_id || id == d->metadata_id)) {
        // The server can remain alive while a virtual node or WirePlumber's
        // metadata object disappears. Rebuild the transport on the UI timer.
        d->connection_error = true;
        d->started = false;
        pw_thread_loop_signal(d->loop, false);
    }
    auto it = d->nodes.find(id);
    if (it != d->nodes.end()) {
        d->nodes.erase(it);
    }
    // A node removal can precede its port removals. Drop those endpoints now
    // so fallback never links back into a disconnected node.
    for (auto port = d->ports.begin(); port != d->ports.end();) {
        if (port->first == id || port->second.node_id == id) port = d->ports.erase(port);
        else ++port;
    }
    if (d->sink_node_id == id) d->sink_node_id = SPA_ID_INVALID;
    reroute(d);
}

const struct pw_registry_events registry_events = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// UI/control thread only. Stop the old realtime thread before rebuilding any
// graph objects; the processor and attached original DSP remain alive.
void disconnectGraph(AudioPassthruPrivate* d, bool restore)
{
    if (d->loop) pw_thread_loop_lock(d->loop);
    d->stopping = true;
    // Drain metadata changes already accepted by the server before deciding
    // whether our default is still current (including a last-moment user edit).
    if (restore && d->metadata && d->default_set && !d->connection_error) roundtrip(d);
    // Respect a later explicit user default choice. Otherwise restore the
    // exact original JSON, or remove the property if it was originally absent.
    if (restore && d->metadata && d->default_set && d->captured_default &&
        d->configured_sink_name == "FxSound" && !d->connection_error) {
        pw_metadata_set_property(d->metadata, 0, "default.configured.audio.sink",
            d->prev_default_sink.empty() ? nullptr : "Spa:String:JSON",
            d->prev_default_sink.empty() ? nullptr : d->prev_default_sink.c_str());
        if (roundtrip(d) && awaitConfiguredSink(d,
                d->prev_default_sink.empty() ? "" : sinkNameFromJson(d->prev_default_sink.c_str())))
            d->recovery.clear();
    }
    // A later user choice (including no configured default) takes precedence.
    // Keep the journal on connection failure so a future process can recover.
    if (restore && d->default_set && !d->connection_error && d->configured_sink_name != "FxSound")
        d->recovery.clear();
    d->monitor_links.clear();
    d->links.clear();
    if (d->metadata) { spa_hook_remove(&d->metadata_listener); pw_proxy_destroy((pw_proxy*)d->metadata); }
    if (d->sink_proxy) pw_proxy_destroy(d->sink_proxy);
    if (d->registry) { spa_hook_remove(&d->registry_listener); pw_proxy_destroy((pw_proxy*)d->registry); }
    if (d->filter) { spa_hook_remove(&d->filter_listener); pw_filter_destroy(d->filter); }
    if (d->core) { spa_hook_remove(&d->core_listener); pw_core_disconnect(d->core); }
    if (d->loop) pw_thread_loop_unlock(d->loop);
    if (d->loop_started) pw_thread_loop_stop(d->loop);
    if (d->context) pw_context_destroy(d->context);
    if (d->loop) pw_thread_loop_destroy(d->loop);
    d->loop = nullptr; d->context = nullptr; d->core = nullptr;
    d->filter = nullptr; d->sink_proxy = nullptr; d->registry = nullptr; d->metadata = nullptr;
    d->loop_started = false; d->started = false; d->metadata_ready = false;
    d->filter_node_id = d->sink_node_id = SPA_ID_INVALID;
    d->metadata_id = SPA_ID_INVALID;
    d->nodes.clear(); d->ports.clear();
}

} // namespace

AudioPassthru::AudioPassthru() : data_(new AudioPassthruPrivate()) {}

AudioPassthru::~AudioPassthru()
{
    if (!data_) return;

    disconnectGraph(data_, true);
    if (data_->library_initialized) pw_deinit();
    delete data_;
    data_ = nullptr;
}

int AudioPassthru::init()
{
    if (data_->loop) {
        pw_thread_loop_lock(data_->loop);
        bool started = data_->started;
        pw_thread_loop_unlock(data_->loop);
        return started ? 0 : 1;
    }
    data_->initialization_requested = true;
    data_->next_retry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    data_->connection_error = false; data_->stopping = false;
    data_->done_seq = -1; data_->default_set = false; data_->captured_default = false;
    data_->configured_sink_name.clear(); data_->have_configured_sink = false;
    data_->configured_sink_json.clear();
    // PipeWire >= 0.3.49 reference-counts init/deinit. Retain one reference per
    // backend across reconnects and release it only after all loops are gone.
    if (!data_->library_initialized) {
        pw_init(nullptr, nullptr);
        data_->library_initialized = true;
    }

    data_->loop = pw_thread_loop_new("fxsound", nullptr);
    if (!data_->loop) return 1;
    data_->context = pw_context_new(pw_thread_loop_get_loop(data_->loop), nullptr, 0);
    if (!data_->context) return 1;

    pw_thread_loop_lock(data_->loop);
    if (pw_thread_loop_start(data_->loop) != 0) { pw_thread_loop_unlock(data_->loop); return 1; }

    data_->loop_started = true;
    data_->core = pw_context_connect(data_->context, nullptr, 0);
    if (!data_->core) { pw_thread_loop_unlock(data_->loop); return 1; }

    pw_core_add_listener(data_->core, &data_->core_listener, &core_events, data_);
    data_->registry = pw_core_get_registry(data_->core, PW_VERSION_REGISTRY, 0);
    if (!data_->registry) { pw_thread_loop_unlock(data_->loop); return 1; }
    pw_registry_add_listener(data_->registry, &data_->registry_listener,
                             &registry_events, data_);

    // Front-end: a real null-audio-sink named "FxSound" that WirePlumber treats
    // as a first-class routable/default-able sink (same approach as EasyEffects).
    // Apps play into it; its monitor ports carry that audio, which we capture,
    // process and forward to the device. A bare pw_filter sink is NOT routed by
    // the policy, so we must use the null-sink adapter factory here.
    auto* sink_props = pw_properties_new(
        PW_KEY_FACTORY_NAME,     "support.null-audio-sink",
        PW_KEY_NODE_NAME,        "FxSound",
        PW_KEY_NODE_DESCRIPTION, "FxSound",
        PW_KEY_MEDIA_CLASS,      "Audio/Sink",
        "audio.position",        "FL,FR",
        "monitor.channel-volumes", "true",
        "monitor.passthrough",   "true",
        "node.virtual",          "true",
        nullptr);
    data_->sink_proxy = static_cast<pw_proxy*>(pw_core_create_object(
        data_->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE,
        &sink_props->dict, 0));
    pw_properties_free(sink_props);
    if (!data_->sink_proxy) { pw_thread_loop_unlock(data_->loop); return 1; }

    // Back-end: hidden processor node. Its inputs are fed from the FxSound sink
    // monitor; its outputs go to the chosen device. NOT an Audio/Sink (so it is
    // not a second routable sink).
    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,       "Audio",
        PW_KEY_MEDIA_CATEGORY,   "Filter",
        PW_KEY_MEDIA_ROLE,       "DSP",
        PW_KEY_NODE_NAME,        "FxSound Processor",
        PW_KEY_NODE_DESCRIPTION, "FxSound Processor",
        // We wire this node's links ourselves (monitor -> in, out -> device);
        // keep WirePlumber from auto-connecting it to other nodes.
        PW_KEY_NODE_AUTOCONNECT, "false",
        nullptr);

    data_->filter = pw_filter_new(data_->core, "FxSound Processor", props);
    if (!data_->filter) { pw_thread_loop_unlock(data_->loop); return 1; }
    pw_filter_add_listener(data_->filter, &data_->filter_listener, &filter_events, data_);

    const char* in_names[kChannels]  = {"input_FL", "input_FR"};
    const char* out_names[kChannels] = {"output_FL", "output_FR"};
    const char* chans[kChannels]     = {"FL", "FR"};
    for (int c = 0; c < kChannels; ++c) {
        data_->in_ports[c] = static_cast<float*>(pw_filter_add_port(
            data_->filter, PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(float),
            pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                              PW_KEY_PORT_NAME, in_names[c],
                              PW_KEY_AUDIO_CHANNEL, chans[c], nullptr), nullptr, 0));
        data_->out_ports[c] = static_cast<float*>(pw_filter_add_port(
            data_->filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(float),
            pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                              PW_KEY_PORT_NAME, out_names[c],
                              PW_KEY_AUDIO_CHANNEL, chans[c], nullptr), nullptr, 0));
    }

    if (pw_filter_connect(data_->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) {
        pw_thread_loop_unlock(data_->loop); return 1;
    }

    // First roundtrip enumerates globals; the second flushes bindings made by
    // those callbacks, including the initial metadata snapshot. Claiming the
    // default earlier could lose a previously unset or not-yet-replayed value.
    bool ready = roundtrip(data_) && roundtrip(data_);
    data_->filter_node_id = pw_filter_get_node_id(data_->filter);
    ready = ready && data_->metadata && data_->filter_node_id != SPA_ID_INVALID && data_->sink_node_id != SPA_ID_INVALID;
    if (ready) {
        data_->metadata_ready = true;
        reroute(data_);
        setDefaultSinkToFxSound(data_);
        reroute(data_); // A recovered preference can change the target device.
        ready = data_->default_set && roundtrip(data_) && awaitConfiguredSink(data_, "FxSound");
    }
    data_->started = ready;
    if (!ready) std::fprintf(stderr, "PipeWire init not ready: metadata=%d filter=%u sink=%u error=%d default=%s\n",
        data_->metadata != nullptr, data_->filter_node_id, data_->sink_node_id,
        data_->connection_error, data_->configured_sink_name.c_str());
    pw_thread_loop_unlock(data_->loop);
    return ready ? 0 : 1;
}

void AudioPassthru::mute(bool mute)
{
    if (data_) data_->processor.setMuted(mute);
}

std::vector<SoundDevice> AudioPassthru::getSoundDevices(bool)
{
    std::vector<SoundDevice> out;
    if (!data_) return out;
    if (data_->loop) pw_thread_loop_lock(data_->loop);

    // The backend remains available while waiting for audio services at login.
    // Our own FxSound node is the Linux equivalent of the Windows "FxSound Audio
    // Enhancer" virtual driver. Surface it so the controller marks the engine
    // enabled (dfx_enabled_); isRealDevice=false keeps it out of the output list.
    if (data_->initialization_requested) {
        SoundDevice dfx;
        dfx.isDFXDevice = true;
        dfx.isRealDevice = false;
        dfx.isActive = data_->started;
        dfx.deviceNumChannel = kChannels;
        dfx.deviceFriendlyName = L"FxSound Audio Enhancer";
        dfx.deviceDescription = L"FxSound Audio Enhancer";
        dfx.pwszID = L"FxSound";
        out.push_back(dfx);
    }

    for (auto& kv : data_->nodes) {
        const NodeInfo& n = kv.second;
        if (n.media_class != "Audio/Sink" || n.name == "FxSound") continue;
        SoundDevice sd;
        sd.isPlaybackDevice = true;
        sd.isRealDevice = true;
        sd.isActive = true;
        sd.isDefaultDevice = (n.name == data_->default_sink_name);
        sd.isTargetedRealPlaybackDevice = (n.id == resolveTargetSink(data_));
        sd.isUserSelectedPlaybackDevice = (n.name == data_->selected_sink_name);
        sd.deviceNumChannel = 2;
        sd.pwszID = fromUtf8(n.name);
        sd.pwszIDRealDevices = sd.pwszID;
        sd.deviceFriendlyName = fromUtf8(n.description);
        sd.deviceDescription = sd.deviceFriendlyName;
        out.push_back(sd);
    }
    if (data_->loop) pw_thread_loop_unlock(data_->loop);
    return out;
}

int  AudioPassthru::setBufferLength(int)                    { return 0; }
int AudioPassthru::processTimer()
{
    if (!data_) return 0;
    bool retry = !data_->loop && data_->initialization_requested;
    bool restore_partial_default = false;
    if (data_->loop) {
        pw_thread_loop_lock(data_->loop);
        retry = data_->connection_error || !data_->started;
        restore_partial_default = !data_->started;
        pw_thread_loop_unlock(data_->loop);
    }
    if (retry && std::chrono::steady_clock::now() >= data_->next_retry) {
        // A partial first connection may already have claimed the default.
        // Restore it before retrying, so the next attempt captures the user
        // default rather than our own temporary virtual sink.
        disconnectGraph(data_, restore_partial_default);
        init(); // !started keeps failed attempts eligible for the next retry.
        data_->next_retry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    if (!data_->loop) return 0;
    pw_thread_loop_lock(data_->loop);
    reroute(data_); // Also repairs a link removed independently of its ports.
    auto* callback = data_->callback;
    pw_thread_loop_unlock(data_->loop);
    if (callback) callback->onSoundDeviceChange(getSoundDevices(false));
    return 0;
}

void AudioPassthru::setDspProcessingModule(DfxDsp* dsp)
{
    if (data_) data_->processor.setDsp(dsp);
}

void AudioPassthru::setAsPlaybackDevice(const SoundDevice sound_device)
{
    if (!data_) return;
    if (data_->loop) pw_thread_loop_lock(data_->loop);
    data_->selected_sink_name = toUtf8(sound_device.pwszID);
    if (data_->loop) {
        reroute(data_);
        pw_thread_loop_unlock(data_->loop);
    }
}

void AudioPassthru::registerCallback(AudioPassthruCallback* cb)
{
    if (!data_) return;
    if (data_->loop) pw_thread_loop_lock(data_->loop);
    data_->callback = cb;
    if (data_->loop) pw_thread_loop_unlock(data_->loop);
}

bool AudioPassthru::isPlaybackDeviceAvailable()
{
    if (!data_ || !data_->loop) return false;
    pw_thread_loop_lock(data_->loop);
    bool available = data_->started && !data_->connection_error && data_->links.size() == kChannels && data_->monitor_links.size() == kChannels;
    for (const auto& [key, link] : data_->links) available &= !link->dead;
    for (const auto& [key, link] : data_->monitor_links) available &= !link->dead;
    pw_thread_loop_unlock(data_->loop);
    return available;
}

bool AudioPassthru::checkDeviceChanges() { return false; }

void AudioPassthru::restoreDefaultPlaybackDevice()
{
    if (!data_ || !data_->loop) return;
    pw_thread_loop_lock(data_->loop);
    data_->selected_sink_name.clear();
    reroute(data_);
    pw_thread_loop_unlock(data_->loop);
}
