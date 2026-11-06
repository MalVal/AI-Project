#include "ReedRelay.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <algorithm>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

ReedRelay::ReedRelay(std::string portPath, LapCallback onLapDetected)
    : serialPortPath(std::move(portPath)), lapCallback(std::move(onLapDetected)) {}

ReedRelay::~ReedRelay() {
    stop();
}

void ReedRelay::start() {
    if (!isRunning.exchange(true)) {
        workerThread = std::thread(&ReedRelay::run, this);
    }
}

void ReedRelay::stop() {
    if (isRunning.exchange(false)) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

int ReedRelay::configureSerialPort(int fd) {
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) return -1;

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    return tcsetattr(fd, TCSANOW, &tty);
}

void ReedRelay::run() {
    int fd = open(serialPortPath.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[REED] Failed to open " << serialPortPath << '\n';
        isRunning.store(false);
        return;
    }

    if (configureSerialPort(fd) != 0) {
        std::cerr << "[REED] Configuration error\n";
        close(fd);
        isRunning.store(false);
        return;
    }

    std::cout << "[REED] Listening on " << serialPortPath << '\n';

    std::string line;
    line.reserve(64);
    char buffer[64];

    while (isRunning.load(std::memory_order_relaxed)) {
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLIN;

        const int result = poll(&descriptor, 1, 100);
        if (result <= 0) continue;

        if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) break;

        const ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        for (ssize_t i = 0; i < bytesRead; ++i) {
            const char byte = buffer[i];

            if (byte == '\n') {
                // Trim trailing carriage return and whitespace
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }

                // IMPORTANT: Verifier que la chaine n'est pas vide APRES le nettoyage
                if (!line.empty()) {
                    char* endPtr = nullptr;
                    unsigned long parsedTime = std::strtoul(line.c_str(), &endPtr, 10);

                    ReedLapEvent event;
                    event.lapIndex = ++currentLap;

                    // Verification : endPtr doit pointer vers la fin de chaine sans caractere non-numerique
                    if (endPtr == line.c_str() || *endPtr != '\0') {
                        std::cerr << "[REED] Non-numeric serial line: '" << line
                                  << "' -> Fallback to system timestamp\n";
                        auto now = std::chrono::steady_clock::now().time_since_epoch();
                        event.arduinoTimestampMs = static_cast<uint32_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()
                        );
                    } else {
                        event.arduinoTimestampMs = static_cast<uint32_t>(parsedTime);
                    }

                    if (lapCallback) {
                        lapCallback(event);
                    }
                }

                // On vide toujours le buffer de ligne pour le prochain message
                line.clear();
            } else if (line.size() < 64) {
                line += byte;
            }
        }
    }

    close(fd);
}