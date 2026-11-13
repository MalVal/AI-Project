#include "MainThread.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "Workers/ReedLapEvent.h"
#include "../Configuration.h"
#include "Workers/ReedRelay.h"
#include "Workers/Accelerometer.h"
#include "Workers/Microphone.h"

MainThread::MainThread() {
    lapLogBuffer.reserve(16384);
}

MainThread::~MainThread() {
    stop();
}

void MainThread::waitEnd() {
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void MainThread::start() {
    if (!isRunning.exchange(true)) {
        std::cout << "[MAIN] Lancement main thread (Mode 100% RAM)\n";
        workerThread = std::thread(&MainThread::run, this);
    }
}

void MainThread::stop() {
    if (isRunning.exchange(false)) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

bool MainThread::sendAll(int sock, const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t totalSent = 0;

    while (totalSent < size) {
        ssize_t sent = send(sock, ptr + totalSent, size - totalSent, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

bool MainThread::sendBufferOverTcp(int sock, const std::string& fileName, const void* data, size_t size) {
    if (size == 0 || size > 10 * 1024 * 1024) {
        std::cerr << "[NET] Buffer skipped (invalid size: " << size << " bytes) for " << fileName << "\n";
        return false;
    }

    if (fileName.empty() || fileName.size() > 255) {
        return false;
    }

    uint8_t nameLen = static_cast<uint8_t>(fileName.size());
    uint32_t netDataSize = htonl(static_cast<uint32_t>(size)); // Big-Endian for Tokio read_u32()

    // 1. 1 byte: filename length
    if (!sendAll(sock, &nameLen, sizeof(nameLen))) return false;
    // 2. 4 bytes: payload size (Big-Endian)
    if (!sendAll(sock, &netDataSize, sizeof(netDataSize))) return false;
    // 3. Filename bytes
    if (!sendAll(sock, fileName.data(), fileName.size())) return false;
    // 4. Payload bytes directly from RAM
    if (!sendAll(sock, data, size)) return false;

    std::cout << "[NET] Sent from RAM: " << fileName << " (" << size << " bytes)\n";
    return true;
}

void MainThread::uploadMemoryBuffers(std::string lapsSnapshot) {
    // Wait 250ms so Accelerometer and Microphone threads notice the new index and push their RAM buffers
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    std::vector<Accelerometer::MemoryChunk> accelChunks;
    std::vector<Microphone::MemoryChunk> micChunks;

    if (accelPtr) {
        accelChunks = accelPtr->extractReadyChunks();
    }
    if (micPtr) {
        micChunks = micPtr->extractReadyChunks();
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[NET] Socket creation error\n";
        return;
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Configuration::SERVER_PORT);

    if (inet_pton(AF_INET, Configuration::SERVER_IP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "[NET] Invalid server IP: " << Configuration::SERVER_IP << "\n";
        close(sock);
        return;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "[NET] Connection failed to " << Configuration::SERVER_IP
                  << ":" << Configuration::SERVER_PORT << "\n";
        close(sock);
        return;
    }

    // 1. Send updated laps_index.csv from RAM
    if (!lapsSnapshot.empty()) {
        sendBufferOverTcp(sock, "laps_index.csv", lapsSnapshot.data(), lapsSnapshot.size());
    }

    // 2. Send all ready Accelerometer CSV buffers from RAM
    for (const auto& chunk : accelChunks) {
        sendBufferOverTcp(sock, chunk.filename, chunk.data.data(), chunk.data.size());
    }

    // 3. Send all ready Microphone WAV buffers from RAM
    for (const auto& chunk : micChunks) {
        sendBufferOverTcp(sock, chunk.filename, chunk.data.data(), chunk.data.size());
    }

    close(sock);
}

void MainThread::handleLapEvent(const ReedLapEvent& lap) {
    std::cout << "[MAIN] Lap " << lap.lapIndex << " : " << lap.arduinoTimestampMs << " ms\n";

    std::string lapsSnapshot;
    {
        std::lock_guard<std::mutex> lock(lapMutex);
        lapLogBuffer += std::to_string(lap.lapIndex) + "," + std::to_string(lap.arduinoTimestampMs) + "\n";
        lapsSnapshot = lapLogBuffer;
    }

    if (lap.lapIndex > 0 && lap.lapIndex % Configuration::LAPS_PER_FILE == 0) {
        uint64_t newIdx = currentFileIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
        std::cout << "[MAIN] RAM rotation & upload triggered (New index: " << newIdx << ")\n";

        std::thread([this, snapshot = std::move(lapsSnapshot)]() mutable {
            this->uploadMemoryBuffers(std::move(snapshot));
        }).detach();
    }
}

void MainThread::run() {
    auto onLap = [this](const ReedLapEvent& lap) {
        this->handleLapEvent(lap);
    };

    ReedRelay reed(Configuration::SERIAL_PORT_RELAIS_REED, onLap);
    Accelerometer accel(currentFileIndex);
    Microphone microphone(Configuration::SERIAL_PORT_MICRO, currentFileIndex);

    accelPtr = &accel;
    micPtr = &microphone;

    reed.start();
    accel.start();
    microphone.start();

    while (isRunning.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    reed.stop();
    accel.stop();
    microphone.stop();

    accelPtr = nullptr;
    micPtr = nullptr;
}