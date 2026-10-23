// Minimal upstream-library teardown diagnostic; no FxSound code is linked.
// c++ -g -O0 -fsanitize=address repro_pipewire_teardown.cpp \
//     $(pkg-config --cflags --libs libpipewire-0.3) -o pw_minimal_asan
// Compare default teardown against PIPEWIRE_DLCLOSE=false; see KUBUNTU_PORT.md.
#include <pipewire/pipewire.h>
int main() {
    pw_init(nullptr, nullptr);
    auto* loop = pw_thread_loop_new("minimal", nullptr);
    auto* context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    pw_context_destroy(context);
    pw_thread_loop_destroy(loop);
    pw_deinit();
}
