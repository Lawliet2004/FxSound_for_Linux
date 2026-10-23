/*
FxSound — Linux build: realtime process-block self-test

Drives FxProcessor (the body of the PipeWire realtime callback) with the real
DfxDsp and synthetic float signals. Checks the block never allocates (malloc, calloc and realloc
are counted while processing), that output stays finite and within [-1, 1], and
the plain-copy / silence paths. Exit code 0 = pass.
*/

#include "PipeWireProcess.h"
#include "DfxDsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <pthread.h>

// ---- allocation counter -------------------------------------------------
// Replaced malloc forwards to glibc. Sanitizers own malloc, so under ASan the
// counter is compiled out and the allocation checks are reported as skipped.
static thread_local bool g_counting = false;
static thread_local int  g_allocs = 0;
static thread_local int g_rt_locks = 0;
static thread_local int g_rt_frees = 0;
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t*);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (g_counting) ++g_rt_locks;
    return __real_pthread_mutex_lock(mutex);
}
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
static constexpr bool kCountAllocs = false;
#else
static constexpr bool kCountAllocs = true;
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void __libc_free(void*);
extern "C" void* malloc(size_t n)
{
    if (g_counting) ++g_allocs;
    return __libc_malloc(n);
}
extern "C" void* calloc(size_t count, size_t size)
{
    if (g_counting) ++g_allocs;
    return __libc_calloc(count, size);
}
extern "C" void* realloc(void* ptr, size_t size)
{
    if (g_counting) ++g_allocs;
    return __libc_realloc(ptr, size);
}
extern "C" void free(void* ptr)
{
    if (g_counting && ptr) ++g_rt_frees;
    __libc_free(ptr);
}
#endif

static bool check(bool cond, const char* what)
{
    std::printf("%s: %s\n", cond ? "ok  " : "FAIL", what);
    return cond;
}

struct Stereo {
    std::vector<float> l, r;
    explicit Stereo(uint32_t n, float amp = 0.f, double freq = 440.0, int rate = 48000) : l(n), r(n)
    {
        for (uint32_t i = 0; i < n; ++i)
            l[i] = r[i] = (float)(amp * std::sin(2.0 * M_PI * freq * i / rate));
    }
};

static int run(FxProcessor& p, Stereo& in, Stereo& out, uint32_t n, int rate)
{
    const float* ip[2] = {in.l.data(), in.r.data()};
    float* op[2] = {out.l.data(), out.r.data()};
    g_allocs = 0;
    g_counting = true;
    p.process(ip, op, n, rate);
    g_counting = false;
    return g_allocs;
}

static bool finiteAndBounded(const Stereo& s)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        for (float v : {s.l[i], s.r[i]})
            if (!std::isfinite(v) || v > 1.f || v < -1.f) return false;
    return true;
}

int main()
{
    bool ok = true;
    if (!kCountAllocs)
        std::printf("Allocation hooks disabled under sanitizer; only the native run measures heap activity.\n");

    // No DSP attached: output is an exact copy of the input.
    {
        FxProcessor p;
        Stereo in(256, 0.5f), out(256);
        run(p, in, out, 256, 48000);
        ok &= check(out.l == in.l && out.r == in.r, "no dsp: output copies input");
    }

    DfxDsp dsp;
    dsp.powerOn(true);
    FxProcessor p;
    p.setDsp(&dsp);

    // Warm up at 48 kHz, then no allocation on steady-state blocks.
    {
        Stereo in(1024, 0.5f), out(1024);
        int first = run(p, in, out, 1024, 48000);
        ok &= check(first == 0, "first attached block does not allocate");
        int a = run(p, in, out, 1024, 48000);
        std::printf("allocations in steady-state block: %d\n", a);
        ok &= check(a == 0, "steady-state block does not allocate");
    }

    // A quantum larger than any seen before must not allocate either.
    {
        Stereo in(8192, 0.5f), out(8192);
        int a = run(p, in, out, 8192, 48000);
        std::printf("allocations in first 8192-frame block: %d\n", a);
        ok &= check(a == 0, "larger quantum does not allocate");
        ok &= check(finiteAndBounded(out), "8192-frame output finite and within [-1, 1]");
    }

    // Graph rate change handled without allocating.
    {
        Stereo in(512, 0.5f, 440.0, 44100), out(512);
        int a = run(p, in, out, 512, 44100);
        std::printf("allocations on rate change: %d\n", a);
        ok &= check(a == 0, "rate change does not allocate");
    }

    // Full-scale input with DSP gain: hard clip keeps output in range.
    {
        Stereo in(4096, 1.0f, 60.0), out(4096);
        for (int k = 0; k < 4; ++k) run(p, in, out, 4096, 48000);
        ok &= check(finiteAndBounded(out), "full-scale input stays finite and within [-1, 1]");
    }

    // Settings are prepared on the control thread; consuming any complete
    // update must also avoid allocation and mutexes on the audio thread.
    {
        Stereo in(256, 0.2f), out(256);
        for (int change = 0; change < 24; ++change) {
            dsp.setNumBands(change % 3 == 0 ? 5 : change % 3 == 1 ? 10 : 31);
            dsp.setFilterQ(change % 2 ? 1.f : 2.f);
            dsp.setEqBandFrequency(2, change % 2 ? 500.f : 1000.f);
            dsp.setEqBandBoostCut(2, change % 2 ? 6.f : -6.f);
            dsp.setBalance(change % 2 ? 3.f : -3.f);
            dsp.setNormalization(change % 2 ? -3.f : 0.f);
            dsp.setMasterGain(change % 2 ? -2.f : 0.f);
            dsp.eqOn(change % 2 == 0);
            for (int effect = 0; effect < DfxDsp::NumEffects; ++effect)
                dsp.setEffectValue(static_cast<DfxDsp::Effect>(effect), static_cast<float>(change % 11));
            int allocations = run(p, in, out, 256, change % 2 ? 44100 : 48000);
            ok &= check(allocations == 0 && finiteAndBounded(out), "updated control block is finite and does not allocate");
        }
    }

    // All common supported graph rates, including the internal downsampling
    // branches, with odd blocks and a quantum larger than the adapter buffers.
    for (int rate : {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000}) {
        bool rateOk = true;
        for (uint32_t frames : {1u, 31u, 127u, 256u, 513u, 8193u}) {
            Stereo in(frames, 0.2f, 440.0, rate), out(frames);
            rateOk &= run(p, in, out, frames, rate) == 0 && finiteAndBounded(out);
        }
        std::printf("rate %d: ", rate);
        ok &= check(rateOk, "all quanta remain finite and do not allocate");
    }

    // Silence in, silence (or near-silence) out once the DSP has settled.
    {
        Stereo in(4096), out(4096);
        for (int k = 0; k < 16; ++k) run(p, in, out, 4096, 48000);
        float peak = 0.f;
        for (size_t i = 0; i < out.l.size(); ++i)
            peak = std::fmax(peak, std::fmax(std::fabs(out.l[i]), std::fabs(out.r[i])));
        std::printf("peak after silence: %g\n", peak);
        ok &= check(peak < 1e-4f, "silence in gives silence out");
    }

    // Muted: zeros regardless of input.
    {
        Stereo in(256, 0.5f), out(256, 0.25f);
        p.setMuted(true);
        run(p, in, out, 256, 48000);
        p.setMuted(false);
        bool zero = true;
        for (size_t i = 0; i < 256; ++i) zero &= out.l[i] == 0.f && out.r[i] == 0.f;
        ok &= check(zero, "muted output is silent");
    }

    // Disabled processing must preserve float samples, including headroom
    // above nominal full scale. The physical output owns final PCM conversion.
    {
        Stereo in(1024, 1.4f), out(1024);
        dsp.powerOn(false);
        int allocations = run(p, in, out, 1024, 48000);
        ok &= check(std::memcmp(in.l.data(), out.l.data(), 1024 * sizeof(float)) == 0 &&
                    std::memcmp(in.r.data(), out.r.data(), 1024 * sizeof(float)) == 0,
                    "power off is sample-transparent even above full scale");
        ok &= check(allocations == 0, "bypass does not allocate");
        dsp.powerOn(true);
    }

    ok &= check(g_rt_locks == 0, "audio callback and control handoff never take a mutex");
    if (kCountAllocs) ok &= check(g_rt_frees == 0, "audio callback never frees heap memory");
    std::printf("PROCESS SELFTEST: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
