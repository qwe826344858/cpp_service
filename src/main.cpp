#include "server.h"
#include <iostream>

int main() {
    try {
        AudioServer server;
        server.run(9002);
    } catch (std::exception & e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
