/* Interactive backend probe used by the private PipeWire regression tests. */
#include "AudioPassthru.h"
#include <codecvt>
#include <iomanip>
#include <iostream>
#include <locale>
#include <string>

int main()
{
    AudioPassthru audio;
    if (audio.init() != 0) { std::cout << "INIT_FAILED" << std::endl; return 1; }
    std::cout << "READY" << std::endl;
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "quit") break;
        audio.processTimer();
        if (command == "unicode") {
            bool found = false;
            for (const auto& device : audio.getSoundDevices()) {
                std::cout << "DEVICE " << std::quoted(std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(device.pwszID))
                          << ' ' << std::quoted(std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(device.deviceFriendlyName)) << '\n';
                if (device.pwszID == L"fx_音響_🎧" && device.deviceFriendlyName == L"音響 🎧") {
                    audio.setAsPlaybackDevice(device);
                    found = true;
                }
            }
            std::cout << "UNICODE " << found << '\n';
        } else if (command.rfind("select ", 0) == 0) {
            SoundDevice device;
            device.pwszID = std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(command.substr(7));
            audio.setAsPlaybackDevice(device);
        } else if (command == "default") audio.restoreDefaultPlaybackDevice();
        std::cout << "AVAILABLE " << audio.isPlaybackDeviceAvailable() << std::endl;
    }
}
