#include "DefaultSinkRecovery.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main()
{
    std::string pattern = (std::filesystem::temp_directory_path() / "fx-recovery-XXXXXX").string();
    const char* directory = ::mkdtemp(pattern.data());
    assert(directory);
    std::filesystem::path root(directory);
    ::setenv("XDG_STATE_HOME", directory, 1);
    ::setenv("PIPEWIRE_RUNTIME_DIR", "/private/test-runtime", 1);
    ::unsetenv("PIPEWIRE_REMOTE");
    DefaultSinkRecovery journal;
    std::string loaded = "untouched";
    assert(!journal.load(loaded) && loaded == "untouched");
    std::string exact = "{\n \"name\": \"音響 🎧\", \"extra\": 42\n}";
    assert(journal.save(exact));
    assert(journal.load(loaded) && loaded == exact);
    assert(journal.save(""));
    assert(journal.load(loaded) && loaded.empty());
    std::filesystem::path file;
    for (const auto& entry : std::filesystem::directory_iterator(root / "fxsound")) file = entry.path();
    struct stat status{};
    assert(::stat(file.c_str(), &status) == 0 && (status.st_mode & 0777) == 0600);
    ::setenv("PIPEWIRE_REMOTE", "another-server", 1);
    DefaultSinkRecovery otherServer;
    assert(!otherServer.load(loaded));
    assert(otherServer.save("other"));
    journal.clear();
    assert(otherServer.load(loaded) && loaded == "other");
    assert(journal.save(exact));
    { std::ofstream corrupt(file); corrupt << "truncated journal"; }
    assert(!journal.load(loaded));
    assert(journal.save(exact));
    assert(!journal.save(std::string(65536, 'x')));
    assert(journal.load(loaded) && loaded == exact);
    auto blocked = root / "regular-file";
    { std::ofstream out(blocked); out << "preserve"; }
    ::setenv("XDG_STATE_HOME", blocked.c_str(), 1);
    DefaultSinkRecovery unavailable;
    assert(!unavailable.save(exact));
    journal.clear(); otherServer.clear();
    std::filesystem::remove_all(root);
    std::cout << "PASS: exact/unset recovery, server scoping, private atomic files, malformed/oversized state and write failure\n";
}
