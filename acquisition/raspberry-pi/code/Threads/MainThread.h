#ifndef UNTITLED_MAINTHREAD_H
#define UNTITLED_MAINTHREAD_H

#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <cstdint>

#include "Workers/ReedLapEvent.h"

class Accelerometer;
class Microphone;

class MainThread {
public:
    MainThread();
    ~MainThread();
    void waitEnd();

    MainThread(const MainThread&) = delete;
    MainThread& operator=(const MainThread&) = delete;

    void start();
    void stop();

private:
    void run();
    void handleLapEvent(const ReedLapEvent& lap);

    void uploadMemoryBuffers(std::string lapsSnapshot);
    bool sendBufferOverTcp(int sock, const std::string& fileName, const void* data, size_t size);
    bool sendAll(int sock, const void* data, size_t size);

    std::atomic<bool> isRunning{false};
    std::thread workerThread;

    std::atomic<uint64_t> currentFileIndex{0};

    std::mutex lapMutex;
    std::string lapLogBuffer;

    Accelerometer* accelPtr{nullptr};
    Microphone* micPtr{nullptr};
};

#endif