#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <atomic>
#include <string>
#include <thread>
#include <cstdint>
#include <fstream>
#include <cstddef>

class Microphone {
public:
    explicit Microphone(const std::atomic<uint64_t>& fileIndexRef);
    ~Microphone();

    // Prevent copy
    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;

    // Thread lifecycle
    void start();
    void stop();

private:
    void run();

    // Serial communication
    bool openSerialPort();
    void closeSerialPort();

    // WAV management
    bool createNewWavFile(uint64_t fileIndex);
    void closeWavFile();
    void writeWavHeader(uint32_t dataSize);

    // Shared index from MainThread
    const std::atomic<uint64_t>& sharedFileIndex;

    // Thread management
    std::atomic<bool> isRunning{false};
    std::thread workerThread;

    // Serial configuration
    std::string serialPort = "/dev/ttyUSB0";
    int serialFd = -1;

    // Audio configuration
    static constexpr uint32_t SAMPLE_RATE = 8000;
    static constexpr uint16_t CHANNELS = 1;
    static constexpr uint16_t BITS_PER_SAMPLE = 8;

    // WAV management
    std::ofstream wavFile;
    uint32_t audioBytesWritten = 0;

    // Current file
    uint64_t activeFileIndex = 0;

    // Output directory
    std::string outputDirectory = "enregistrements";
};

#endif // MICROPHONE_H
