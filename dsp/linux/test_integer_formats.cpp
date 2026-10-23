/* FxSound Linux port: LP64 must preserve the Windows int32_t file format. */
#include "codedefs.h"
#include "vals.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    if (!config) return 2;
    auto path = std::filesystem::path(config) / "integer-format.cfg";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path);
        file << "Effect_Configuration\n1.0: Version\nTest\n2\nStereo\nStereo\n1\n1\n-1\n";
    }
    auto wide = path.wstring();
    PT_HANDLE* handle = nullptr;
    if (valsCfgRead(&handle, wide.data(), nullptr) != OKAY || !handle) return 1;
    int32_t timestamp = 0;
    bool ok = valsCfgGetPresetSaveTime(handle, &timestamp) == OKAY && timestamp == -1;
    ok = valsCfgWrite(handle) == OKAY && ok;
    valsCfgFreeUp(&handle);
    std::ifstream file(path);
    std::string line, last;
    while (std::getline(file, line)) if (!line.empty()) last = line;
    ok = last == "-1: Time of last preset save" && ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": signed 32-bit config timestamp: " << last << '\n';
    return ok ? 0 : 1;
}
