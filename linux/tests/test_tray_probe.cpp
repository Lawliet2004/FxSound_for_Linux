#include "fx_tray_sni.h"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    FxTraySNI tray([] {}, [](int) {});
    if (argc > 1 && std::string(argv[1]) == "immediate") return 0;
    std::cout << "READY" << std::endl;
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "quit") break;
        if (command == "active") std::cout << "ACTIVE " << tray.isActive() << std::endl;
        if (command == "stress") {
            for (int i = 0; i < 3000; ++i) {
                tray.setTitle(std::string(128 + i % 64, 'a' + i % 26));
                tray.setIconName("fxsound-" + std::to_string(i));
                tray.setIconThemePath("/tmp/icons-" + std::to_string(i));
                tray.setIconPixmap(8, 8, std::vector<unsigned char>(8 * 8 * 4, i % 256));
                MenuItem menu;
                menu.children.push_back(MenuItem{});
                tray.updateMenu(std::move(menu));
            }
            std::cout << "UPDATED" << std::endl;
        }
    }
}
