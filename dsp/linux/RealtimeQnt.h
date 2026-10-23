/* Bounded storage for the original quantizer tables; owned by one DSP engine. */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace fxsound {
class QntStorage {
public:
    void* allocate(size_t count, size_t size) noexcept {
        if (size && count > blockSize / size) { failed = true; return nullptr; }
        const size_t bytes = count * size;
        for (auto& block : blocks_) if (!block.used) {
            block.used = true;
            std::memset(block.data.data(), 0, bytes);
            return block.data.data();
        }
        failed = true;
        return nullptr;
    }
    bool release(void* ptr) noexcept {
        for (auto& block : blocks_) if (ptr == block.data.data()) {
            block.used = false;
            return true;
        }
        return false;
    }
    bool failed = false;
private:
    // MIDI tables have 128 entries; the largest contains filter structures.
    // Capacity is deliberately bounded; exhaustion fails, never falls back to heap.
    static constexpr size_t blockSize = 8192;
    struct Block { alignas(std::max_align_t) std::array<std::byte, blockSize> data; bool used = false; };
    std::array<Block, 128> blocks_;
};
inline thread_local QntStorage* currentQntStorage = nullptr;
class QntScope {
public:
    explicit QntScope(QntStorage& storage) noexcept : previous_(currentQntStorage) { currentQntStorage = &storage; }
    ~QntScope() { currentQntStorage = previous_; }
private:
    QntStorage* previous_;
};
}
inline void* qntCalloc(size_t count, size_t size) {
    return fxsound::currentQntStorage ? fxsound::currentQntStorage->allocate(count, size) : std::calloc(count, size);
}
inline void qntFree(void* ptr) {
    if (!ptr || (fxsound::currentQntStorage && fxsound::currentQntStorage->release(ptr))) return;
    std::free(ptr);
}
