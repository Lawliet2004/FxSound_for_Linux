/* Fail if the real callback attempts filesystem I/O while controls change.
 * The seccomp filter is confined to a test child / the test's audio thread.
 * https://docs.kernel.org/userspace-api/seccomp_filter.html
 */
#include "PipeWireProcess.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static bool denyFileIO()
{
#if defined(__x86_64__)
    constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#else
    constexpr unsigned architecture = 0;
    return false;
#endif
#define DENY(number) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP)
    const sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#ifdef __NR_open
        DENY(__NR_open),
#endif
        DENY(__NR_openat),
#ifdef __NR_openat2
        DENY(__NR_openat2),
#endif
        DENY(__NR_read), DENY(__NR_write), DENY(__NR_readv), DENY(__NR_writev),
        DENY(__NR_pread64), DENY(__NR_pwrite64), DENY(__NR_newfstatat),
#ifdef __NR_stat
        DENY(__NR_stat), DENY(__NR_lstat),
#endif
        DENY(__NR_fstat), DENY(__NR_statx), DENY(__NR_fsync), DENY(__NR_fdatasync),
        DENY(__NR_renameat), DENY(__NR_unlinkat), DENY(__NR_mkdirat),
#ifdef __NR_access
        DENY(__NR_access),
#endif
        DENY(__NR_faccessat),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
#undef DENY
    sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])),
                       const_cast<sock_filter*>(filter)};
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
           prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

int main()
{
    struct sigaction action{};
    action.sa_handler = [](int) { _exit(99); };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSYS, &action, nullptr) != 0) return 1;
    // Positive control: prove this kernel actually rejects a file open.
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) {
        if (!denyFileIO()) _exit(77);
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) close(fd);
        _exit(1);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) return 1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 77) return 77;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 99) return 1;

    DfxDsp dsp;
    dsp.powerOn(true);
    FxProcessor processor;
    processor.setDsp(&dsp);
    std::array<float, 256> left{}, right{}, outLeft{}, outRight{};
    left.fill(.1f); right.fill(-.08f);
    const float* input[2] = {left.data(), right.data()};
    float* output[2] = {outLeft.data(), outRight.data()};
    std::atomic<bool> ready{false}, done{false};
    std::atomic<int> blocks{0}, failure{0};
    std::thread audio([&] {
        processor.process(input, output, 256, 48000);
        if (!denyFileIO()) { failure.store(77); ready.store(true); return; }
        ready.store(true, std::memory_order_release);
        do {
            int block = blocks.fetch_add(1);
            processor.process(input, output, 256, block % 64 ? 48000 : 44100);
        } while (!done.load(std::memory_order_acquire));
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    if (!failure.load()) {
        for (int change = 0; change < 100; ++change) {
            dsp.setNumBands(change % 2 ? 10 : 31);
            dsp.setFilterQ(change % 2 ? 1.f : 2.f);
            dsp.setEqBandBoostCut(2, change % 2 ? 6.f : -6.f);
            dsp.setEffectValue(DfxDsp::Bass, static_cast<float>(change % 11));
            std::array<float, 10> spectrum{};
            dsp.getSpectrumBandValues(spectrum.data(), 10);
        }
    }
    done.store(true, std::memory_order_release);
    audio.join();
    if (failure.load()) return failure.load();
    std::printf("PASS: %d callback blocks with filesystem syscalls prohibited, including control and rate changes\n", blocks.load());
    return blocks.load() > 1 ? 0 : 1;
}
