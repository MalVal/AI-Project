#ifndef ACQUISITION_ACCELEROMETER_H
#define ACQUISITION_ACCELEROMETER_H

#include <atomic>
#include <string>
#include <thread>
#include <cstdint>
#include <fstream>

class Accelerometer {
public:
    explicit Accelerometer(const std::atomic<uint64_t>& fileIndexRef);
    ~Accelerometer();

    Accelerometer(const Accelerometer&) = delete;
    Accelerometer& operator=(const Accelerometer&) = delete;

    void start();
    void stop();

private:
    void run();
    int initializeAccelerometer(int& address);
    bool readAcceleration(double& x, double& y, double& z);
    
    // Utilitaires I2C
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t* data, size_t len);
    bool readRegister(uint8_t reg, uint8_t& value);

    const std::atomic<uint64_t>& sharedFileIndex;
    int i2c_fd{-1};

    std::atomic<bool> isRunning{false};
    std::thread workerThread;
};

#endif // ACQUISITION_ACCELEROMETER_H