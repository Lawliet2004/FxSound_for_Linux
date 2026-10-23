/* FxSound Linux: bounded, audio-thread-owned settings view for the original DSP. */
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string_view>

namespace fxsound {
struct RegistryImage {
    static constexpr size_t capacity = 256, pathLength = 256, valueLength = 512;
    struct Entry { std::array<wchar_t, pathLength> key{}; std::array<wchar_t, valueLength> value{}; };
    std::array<Entry, capacity> entries{};
    size_t count = 0;
    bool failed = false;

    const wchar_t* find(std::wstring_view key) const noexcept {
        for (size_t i = 0; i < count; ++i)
            if (key == entries[i].key.data()) return entries[i].value.data();
        return nullptr;
    }
    bool put(std::wstring_view key, std::wstring_view value) noexcept {
        if (key.size() >= pathLength || value.size() >= valueLength) return !(failed = true);
        size_t slot = 0;
        while (slot < count && key != entries[slot].key.data()) ++slot;
        if (slot == count) {
            if (count == capacity) return !(failed = true);
            ++count;
        }
        std::copy(key.begin(), key.end(), entries[slot].key.begin());
        entries[slot].key[key.size()] = 0;
        std::copy(value.begin(), value.end(), entries[slot].value.begin());
        entries[slot].value[value.size()] = 0;
        return true;
    }
    void remove(std::wstring_view key, bool recursive = false) noexcept {
        for (size_t i = 0; i < count;) {
            std::wstring_view candidate(entries[i].key.data());
            bool matches = candidate == key || (recursive && candidate.size() > key.size() &&
                candidate.substr(0, key.size()) == key &&
                ((!key.empty() && key.back() == L'\\') || candidate[key.size()] == L'\\'));
            if (matches) entries[i] = entries[--count]; else ++i;
        }
    }
    static bool join(const wchar_t* path, const wchar_t* name,
                     std::array<wchar_t, pathLength>& key) noexcept {
        std::wstring_view p(path ? path : L""), n(name ? name : L"");
        size_t size = p.size() + (n.empty() ? 0 : 1 + n.size());
        if (size >= key.size()) return false;
        std::copy(p.begin(), p.end(), key.begin());
        if (!n.empty()) {
            key[p.size()] = L'\\';
            std::copy(n.begin(), n.end(), key.begin() + p.size() + 1);
        }
        key[size] = 0;
        return true;
    }
};

RegistryImage* currentRegistry() noexcept;
// Capture is control-thread-only; it may lock/load the persistent store.
bool captureRegistry(RegistryImage& image);
class RegistryScope {
public:
    explicit RegistryScope(RegistryImage& image) noexcept;
    ~RegistryScope();
    RegistryScope(const RegistryScope&) = delete;
    RegistryScope& operator=(const RegistryScope&) = delete;
private:
    RegistryImage* previous_;
};
}
