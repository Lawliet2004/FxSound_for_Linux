#include "PortalHotkeys.h"
#include <iostream>
#include <string>
int main() {
    PortalHotkeys keys([](int command) { std::cout << "activated " << command << std::endl; });
    std::string operation;
    while (std::cin >> operation) {
        if (operation == "quit") break;
        if (operation == "clear") keys.clear();
        if (operation == "set") {
            int command, modifiers, key; std::cin >> command >> modifiers >> key;
            std::cout << "queued " << keys.set(command, modifiers, key) << std::endl;
        }
    }
}
