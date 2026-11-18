
#include "ReedRelay.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <algorithm>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

ReedRelay::ReedRelay(std::string portPath, LapCallback onLapDetected)
    : serialPortPath(std::move(portPath)), lapCallback(std::move(onLapDetected)) {
}

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

    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= (CS8 | CLOCAL | CREAD | HUPCL);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }

    // Force DTR and RTS high
    int status = TIOCM_DTR | TIOCM_RTS;

    if (ioctl(fd, TIOCMBIS, &status) != 0) {
        // Continue even if DTR/RTS cannot be set
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    tcflush(fd, TCIFLUSH);

    return 0;
}

void ReedRelay::run() {
    int fd = open(serialPortPath.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        isRunning.store(false);
        return;
    }

    if (configureSerialPort(fd) != 0) {
        close(fd);
        isRunning.store(false);
        return;
    }

    std::string line;
    line.reserve(64);
    char buffer[64];

    while (isRunning.load(std::memory_order_relaxed)) {
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLIN;

        const int result = poll(&descriptor, 1, 100);

        if (result < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (result == 0) continue;

        if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) {
            break;
        }

        const ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            break;
        }

        if (bytesRead == 0) continue;

        for (ssize_t i = 0; i < bytesRead; ++i) {
            const char byte = buffer[i];

            if (byte == '\n') {
                // Remove trailing carriage returns and spaces
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }

                if (!line.empty()) {
                    char* endPtr = nullptr;
                    unsigned long parsedTime = std::strtoul(line.c_str(), &endPtr, 10);

                    // Allow parsing to succeed even if there is trailing whitespace that wasn't caught
                    if (endPtr != line.c_str()) {
                        ReedLapEvent event;
                        event.lapIndex = ++currentLap;
                        event.arduinoTimestampMs = static_cast<uint32_t>(parsedTime);

                        if (lapCallback) {
                            lapCallback(event);
                        }
                    }
                }

                line.clear();
            } else if (byte != '\r') {
                // Only add printable ASCII characters to prevent parsing errors
                if (line.size() < 64 && byte >= 32 && byte <= 126) {
                    line += byte;
                }
            }
        }
    }

    close(fd);
}
