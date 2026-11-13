#ifndef ACQUISITION_ACCELEROMETER_H
#define ACQUISITION_ACCELEROMETER_H

#include <atomic>
#include <string>
#include <thread>
#include <cstdint>
#include <vector>
#include <mutex>

class Accelerometer {
public:
    struct MemoryChunk {
        uint64_t index;
        std::string filename;
        std::string data;
    };

    explicit Accelerometer(const std::atomic<uint64_t>& fileIndexRef);
    ~Accelerometer();

    Accelerometer(const Accelerometer&) = delete;
    Accelerometer& operator=(const Accelerometer&) = delete;

    void start();
    void stop();

    std::vector<MemoryChunk> extractReadyChunks();

private:
    void run();
    std::string generateFileName(uint64_t fileIndex) const;

    int initializeAccelerometer(int& address);
    bool readAcceleration(double& x, double& y, double& z);

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t* data, size_t len);
    bool readRegister(uint8_t reg, uint8_t& value);

    const std::atomic<uint64_t>& sharedFileIndex;
    int i2c_fd{-1};
    uint8_t currentAddress{0x1D};

    std::mutex chunksMutex;
    std::vector<MemoryChunk> readyChunks;

    std::atomic<bool> isRunning{false};
    std::thread workerThread;
};

#endif