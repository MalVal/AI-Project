//
// Created by geele on 25-10-26.
//

#include <iostream>
#include <ostream>

#include "Threads/MainThread.h"

int main(int argc, char **argv) {
    std::cout << "Starting application...\n";

    MainThread mainApp;
    mainApp.start();

    // Prevent the main thread from exiting and killing the application
    mainApp.waitEnd();

    return 0;
}