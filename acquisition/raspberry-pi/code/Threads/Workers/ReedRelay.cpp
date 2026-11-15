
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
    std::cout << "[REED] Constructor - Port: " << serialPortPath << std::endl;
}

ReedRelay::~ReedRelay() {
    std::cout << "[REED] Destructor called" << std::endl;
    stop();
}

void ReedRelay::start() {
    std::cout << "[REED] Start requested" << std::endl;

    if (!isRunning.exchange(true)) {
        std::cout << "[REED] Starting worker thread" << std::endl;
        workerThread = std::thread(&ReedRelay::run, this);
    } else {
        std::cout << "[REED] Already running" << std::endl;
    }
}

void ReedRelay::stop() {
    std::cout << "[REED] Stop requested" << std::endl;

    if (isRunning.exchange(false)) {
        std::cout << "[REED] Waiting for worker thread..." << std::endl;

        if (workerThread.joinable()) {
            workerThread.join();
        }

        std::cout << "[REED] Worker thread stopped" << std::endl;
    } else {
        std::cout << "[REED] Already stopped" << std::endl;
    }
}

int ReedRelay::configureSerialPort(int fd) {
    std::cout << "[REED] Configuring serial port..." << std::endl;

    termios tty{};

    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "[REED] tcgetattr failed: " << strerror(errno) << std::endl;
        return -1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= (CLOCAL | CREAD | CS8 | HUPCL);
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "[REED] tcsetattr failed: " << strerror(errno) << std::endl;
        return -1;
    }

    std::cout << "[REED] Serial configured: 115200 baud, 8N1" << std::endl;

    // Force DTR and RTS high
    int status = TIOCM_DTR | TIOCM_RTS;

    if (ioctl(fd, TIOCMBIS, &status) != 0) {
        std::cerr << "[REED] Warning: Failed to set DTR/RTS: "
                  << strerror(errno) << std::endl;
    } else {
        std::cout << "[REED] DTR/RTS enabled" << std::endl;
    }

    std::cout << "[REED] Waiting 2 seconds for Arduino reset..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    tcflush(fd, TCIFLUSH);

    std::cout << "[REED] Serial buffer flushed - Ready!" << std::endl;

    return 0;
}

void ReedRelay::run() {
    std::cout << "[REED] Worker thread started" << std::endl;
    std::cout << "[REED] Opening port: " << serialPortPath << std::endl;

    int fd = open(serialPortPath.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        std::cerr << "[REED] Failed to open " << serialPortPath
                  << ": " << strerror(errno) << std::endl;
        isRunning.store(false);
        return;
    }

    std::cout << "[REED] Port opened successfully (fd=" << fd << ")" << std::endl;

    if (configureSerialPort(fd) != 0) {
        std::cerr << "[REED] Configuration error" << std::endl;
        close(fd);
        isRunning.store(false);
        return;
    }

    std::cout << "[REED] Listening on " << serialPortPath << std::endl;

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

            std::cerr << "[REED] poll error: " << strerror(errno) << std::endl;
            break;
        }

        if (result == 0) continue;

        if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) {
            std::cerr << "[REED] Serial connection error/disconnected. revents="
                      << descriptor.revents << std::endl;
            break;
        }

        const ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }

            std::cerr << "[REED] Read error: " << strerror(errno) << std::endl;
            break;
        }

        if (bytesRead == 0) continue;

        // --- DIAGNOSTIC : Affichage des octets bruts en Hexadécimal ---
        std::cout << "[REED] Received " << bytesRead << " bytes | HEX: ";
        for (ssize_t i = 0; i < bytesRead; ++i) {
            std::cout << std::hex << (int)(unsigned char)buffer[i] << " ";
        }
        std::cout << std::dec << std::endl; // Retour en affichage décimal standard
        // --------------------------------------------------------------

        for (ssize_t i = 0; i < bytesRead; ++i) {
            const char byte = buffer[i];

            if (byte == '\n') {
                // Remove trailing carriage returns and spaces
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }

                std::cout << "[REED] Complete line received: '" << line << "'" << std::endl;

                if (!line.empty()) {
                    char* endPtr = nullptr;
                    unsigned long parsedTime = std::strtoul(line.c_str(), &endPtr, 10);

                    // Allow parsing to succeed even if there is trailing whitespace that wasn't caught
                    if (endPtr != line.c_str()) {
                        ReedLapEvent event;
                        event.lapIndex = ++currentLap;
                        event.arduinoTimestampMs = static_cast<uint32_t>(parsedTime);

                        std::cout << "[REED] LAP DETECTED!" << std::endl;
                        std::cout << "[REED] Lap index: " << event.lapIndex << std::endl;
                        std::cout << "[REED] Arduino timestamp: " << event.arduinoTimestampMs << " ms" << std::endl;

                        if (lapCallback) {
                            std::cout << "[REED] Calling lap callback..." << std::endl;
                            lapCallback(event);
                            std::cout << "[REED] Callback completed" << std::endl;
                        } else {
                            std::cerr << "[REED] Warning: No lap callback registered" << std::endl;
                        }
                    } else {
                        std::cout << "[REED] Ignored non-numeric message: '" << line << "'" << std::endl;
                    }
                }
                line.clear(); // Always clear after processing a newline
            } else if (byte != '\r') {
                // Only add printable ASCII characters to prevent parsing errors
                if (line.size() < 64 && byte >= 32 && byte <= 126) {
                    line += byte;
                }
            }
        }
    }

    std::cout << "[REED] Closing serial port..." << std::endl;
    close(fd);

    std::cout << "[REED] Worker thread terminated" << std::endl;
}
