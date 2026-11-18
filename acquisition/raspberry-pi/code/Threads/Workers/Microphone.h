#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <atomic>
#include <string>
#include <thread>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>

class Microphone {
public:
    struct MemoryChunk {
        uint64_t index;
        std::string filename;
        std::vector<uint8_t> data;
    };

    explicit Microphone(const std::atomic<uint64_t>& fileIndexRef);
    Microphone(std::string portPath, const std::atomic<uint64_t>& fileIndexRef);
    ~Microphone();

    void start();
    void stop();

    std::vector<MemoryChunk> extractReadyChunks();

private:
    std::string serialPortPath;

    void run();

    bool openSerialPort();
    void closeSerialPort();

    void startNewWavBuffer(uint64_t fileIndex);
    void finalizeCurrentWavBuffer();
    void writeWavHeaderInRam(std::vector<uint8_t>& buf, uint32_t dataSize);

    const std::atomic<uint64_t>& sharedFileIndex;

    std::atomic<bool> isRunning{false};
    std::thread workerThread;

    int serialFd = -1;

    static constexpr uint32_t SAMPLE_RATE = 8000;
    static constexpr uint16_t CHANNELS = 1;
    static constexpr uint16_t BITS_PER_SAMPLE = 8;

    std::vector<uint8_t> currentWavBuffer;
    std::string currentFileName;
    uint64_t activeFileIndex = 0;

    std::mutex chunksMutex;
    std::vector<MemoryChunk> readyChunks;
};

#endif