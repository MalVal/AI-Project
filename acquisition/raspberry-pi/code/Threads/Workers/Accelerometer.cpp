#include "Accelerometer.h"

#include <iostream>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

namespace fs = std::filesystem;

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

std::string Accelerometer::generateFileName(uint64_t fileIndex) const {
    fs::create_directories(outputDirectory);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_r(&time, &localTime);

    std::ostringstream filename;
    filename << outputDirectory
             << "/accel_data_"
             << std::put_time(&localTime, "%Y%m%d_%H%M%S")
             << "_"
             << fileIndex
             << ".csv";

    return filename.str();
}

bool Accelerometer::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return write(i2c_fd, data, 2) == 2;
}

bool Accelerometer::readRegisters(uint8_t reg, uint8_t* data, size_t len) {
    i2c_msg msgs[2];

    msgs[0].addr = currentAddress;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;

    msgs[1].addr = currentAddress;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = static_cast<uint16_t>(len);
    msgs[1].buf = data;

    i2c_rdwr_ioctl_data rdwr{};
    rdwr.msgs = msgs;
    rdwr.nmsgs = 2;

    return ioctl(i2c_fd, I2C_RDWR, &rdwr) >= 0;
}

bool Accelerometer::readRegister(uint8_t reg, uint8_t& value) {
    return readRegisters(reg, &value, 1);
}

int Accelerometer::initializeAccelerometer(int& address) {
    i2c_fd = open("/dev/i2c-1", O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "[ACCEL] Cannot open I2C device: " << std::strerror(errno) << '\n';
        return -1;
    }

    const uint8_t possibleAddresses[] = {0x1D, 0x1C};
    bool detected = false;
    uint8_t id = 0;

    for (uint8_t addr : possibleAddresses) {
        currentAddress = addr;
        if (ioctl(i2c_fd, I2C_SLAVE, currentAddress) < 0) continue;

        if (readRegister(0x0D, id)) {
            address = currentAddress;
            detected = true;
            break;
        }
    }

    if (!detected) {
        std::cerr << "[ACCEL] Sensor not responding on 0x1D or 0x1C: "
                  << std::strerror(errno) << '\n';
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    if (id != 0x2A && id != 0x1A && id != 0x3A) {
        std::cerr << "[ACCEL] Unexpected chip ID: 0x" << std::hex << static_cast<int>(id)
                  << std::dec << " (expected 0x2A)\n";
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    if (!writeRegister(0x2A, 0x00) || !writeRegister(0x2A, 0x19)) {
        std::cerr << "[ACCEL] Failed to configure control registers: "
                  << std::strerror(errno) << '\n';
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    std::cout << "[ACCEL] MMA8452 detected at 0x" << std::hex << address
              << " with chip ID 0x" << static_cast<int>(id) << std::dec << '\n';
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
    std::string currentFilePath = generateFileName(localFileIndex);
    std::ofstream outFile(currentFilePath);

    std::cout << "[ACCEL] New file: " << currentFilePath << '\n';

    std::string buffer;
    buffer.reserve(8192);

    auto flushBuffer = [&]() {
        if (!buffer.empty()) {
            outFile.write(buffer.data(), buffer.size());
            buffer.clear();
        }
    };

    while (isRunning.load(std::memory_order_relaxed)) {
        // 1. Rotation de fichier si le MainThread change l'index
        uint64_t currentGlobalIndex = sharedFileIndex.load(std::memory_order_relaxed);
        if (currentGlobalIndex != localFileIndex) {
            flushBuffer();
            outFile.close();

            localFileIndex = currentGlobalIndex;
            currentFilePath = generateFileName(localFileIndex);
            outFile.open(currentFilePath);

            std::cout << "[ACCEL] Rotated to: " << currentFilePath << "\n";
        }

        // 2. Échantillonnage et bufferisation
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

    writeRegister(0x2A, 0x00);
    close(i2c_fd);
    i2c_fd = -1;

    std::cout << "[ACCEL] Thread stopped\n";
}