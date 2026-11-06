#include "Accelerometer.h"

#include <iostream>
#include <array>
#include <chrono>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

Accelerometer::Accelerometer(const std::atomic<uint64_t>& fileIndexRef)
    : sharedFileIndex(fileIndexRef) {}

Accelerometer::~Accelerometer() {
    stop();
}

void Accelerometer::start() {
    if (!isRunning.exchange(true)) {
        workerThread = std::thread(&Accelerometer::run, this);
    }
}

void Accelerometer::stop() {
    if (isRunning.exchange(false)) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

bool Accelerometer::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return write(i2c_fd, data, 2) == 2;
}

bool Accelerometer::readRegisters(uint8_t reg, uint8_t* data, size_t len) {
    return write(i2c_fd, &reg, 1) == 1 &&
           read(i2c_fd, data, len) == static_cast<ssize_t>(len);
}

bool Accelerometer::readRegister(uint8_t reg, uint8_t& value) {
    return readRegisters(reg, &value, 1);
}

int Accelerometer::initializeAccelerometer(int& address) {
    address = 0x1D;
    i2c_fd = open("/dev/i2c-1", O_RDWR);

    if (i2c_fd < 0) {
        std::cerr << "[ACCEL] Cannot open I2C device: " << std::strerror(errno) << '\n';
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, address) < 0) {
        std::cerr << "[ACCEL] Cannot select address: " << std::strerror(errno) << '\n';
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    uint8_t id = 0;

    // MMA8452 WHO_AM_I register check (0x0D must return 0x2A)
    if (!readRegister(0x0D, id) || id != 0x2A ||
        !writeRegister(0x2A, 0x00) ||  // Standby mode
        !writeRegister(0x2A, 0x19)) {  // Active mode, 100Hz ODR
        std::cerr << "[ACCEL] MMA8452 initialization failed\n";
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    std::cout << "[ACCEL] MMA8452 detected at 0x1D\n";
    return 0;
}

bool Accelerometer::readAcceleration(double& x, double& y, double& z) {
    std::array<uint8_t, 6> data{};
    if (!readRegisters(0x01, data.data(), data.size())) {
        std::cerr << "[ACCEL] Read failed: " << std::strerror(errno) << '\n';
        return false;
    }

    double* axes[] = {&x, &y, &z};

    for (int i = 0; i < 3; ++i) {
        int value = ((data[2 * i] << 8) | data[2 * i + 1]) >> 4;
        if (value & 0x800) {
            value -= 4096;
        }
        *axes[i] = value / 1024.0;
    }

    return true;
}

void Accelerometer::run() {
    std::cout << "[ACCEL] Thread started\n";

    int address = -1;
    if (initializeAccelerometer(address) != 0) {
        isRunning.store(false);
        return;
    }

    uint64_t localFileIndex = sharedFileIndex.load(std::memory_order_relaxed);
    std::ofstream outFile("accel_data_" + std::to_string(localFileIndex) + ".csv");

    std::string buffer;
    buffer.reserve(8192);

    auto flushBuffer = [&]() {
        if (!buffer.empty()) {
            outFile.write(buffer.data(), buffer.size());
            buffer.clear();
        }
    };

    while (isRunning.load(std::memory_order_relaxed)) {
        // 1. Check if MainThread triggered a file rotation
        uint64_t currentGlobalIndex = sharedFileIndex.load(std::memory_order_relaxed);
        if (currentGlobalIndex != localFileIndex) {
            flushBuffer();
            outFile.close();
            localFileIndex = currentGlobalIndex;
            outFile.open("accel_data_" + std::to_string(localFileIndex) + ".csv");
            std::cout << "[ACCEL] Rotated to file index " << localFileIndex << "\n";
        }

        // 2. Read sensor and append to buffer
        double x, y, z;
        if (readAcceleration(x, y, z)) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

            buffer += std::to_string(timestamp) + "," +
                      std::to_string(x) + "," +
                      std::to_string(y) + "," +
                      std::to_string(z) + "\n";

            if (buffer.size() > 4096) {
                flushBuffer();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    flushBuffer();
    outFile.close();

    // Set accelerometer to standby before closing
    writeRegister(0x2A, 0x00);
    close(i2c_fd);
    i2c_fd = -1;

    std::cout << "[ACCEL] Thread stopped\n";
}