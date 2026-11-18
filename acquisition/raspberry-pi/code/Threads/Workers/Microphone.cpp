#include "Microphone.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>

static void putLE16(uint8_t* dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFF);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void putLE32(uint8_t* dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFF);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dest[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

Microphone::Microphone(const std::atomic<uint64_t>& fileIndexRef)
    : serialPortPath("/dev/ttyUSB0"), sharedFileIndex(fileIndexRef) {}

Microphone::Microphone(std::string portPath, const std::atomic<uint64_t>& fileIndexRef)
    : serialPortPath(std::move(portPath)), sharedFileIndex(fileIndexRef) {}

Microphone::~Microphone() {
    stop();
}

void Microphone::start() {
    bool expected = false;
    if (!isRunning.compare_exchange_strong(expected, true))
        return;

    if (workerThread.joinable())
        workerThread.join();

    workerThread = std::thread(&Microphone::run, this);
}

void Microphone::stop() {
    isRunning.store(false);
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

std::vector<Microphone::MemoryChunk> Microphone::extractReadyChunks() {
    std::lock_guard<std::mutex> lock(chunksMutex);
    std::vector<MemoryChunk> extracted;
    extracted.swap(readyChunks);
    return extracted;
}

bool Microphone::openSerialPort() {
    serialFd = open(serialPortPath.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (serialFd < 0) {
        std::cerr << "[AUDIO] Failed to open " << serialPortPath << " : " << strerror(errno) << '\n';
        return false;
    }

    termios tty{};
    if (tcgetattr(serialFd, &tty) != 0) {
        std::cerr << "[AUDIO] tcgetattr error\n";
        closeSerialPort();
        return false;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(serialFd, TCSANOW, &tty) != 0) {
        std::cerr << "[AUDIO] tcsetattr error\n";
        closeSerialPort();
        return false;
    }

    tcflush(serialFd, TCIFLUSH);
    std::cout << "[AUDIO] Serial port opened: " << serialPortPath << '\n';
    return true;
}

void Microphone::closeSerialPort() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
    }
}

void Microphone::writeWavHeaderInRam(std::vector<uint8_t>& buf, uint32_t dataSize) {
    if (buf.size() < 44) return;

    const uint32_t fileSize = dataSize + 36;
    const uint32_t byteRate = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
    const uint16_t blockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);

    uint8_t* p = buf.data();
    std::memcpy(p + 0, "RIFF", 4);
    putLE32(p + 4, fileSize);
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    putLE32(p + 16, 16);
    putLE16(p + 20, 1); // PCM
    putLE16(p + 22, CHANNELS);
    putLE32(p + 24, SAMPLE_RATE);
    putLE32(p + 28, byteRate);
    putLE16(p + 32, blockAlign);
    putLE16(p + 34, BITS_PER_SAMPLE);
    std::memcpy(p + 36, "data", 4);
    putLE32(p + 40, dataSize);
}

void Microphone::startNewWavBuffer(uint64_t fileIndex) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_r(&time, &localTime);

    std::ostringstream filename;
    filename << "audio_"
             << std::put_time(&localTime, "%Y%m%d_%H%M%S")
             << "_"
             << fileIndex
             << ".wav";

    currentFileName = filename.str();
    activeFileIndex = fileIndex;

    currentWavBuffer.clear();
    currentWavBuffer.reserve(256 * 1024); // Pre-allocate 256 KB in RAM
    currentWavBuffer.resize(44, 0);      // Reserve 44 bytes for WAV header
    writeWavHeaderInRam(currentWavBuffer, 0);

    std::cout << "[AUDIO] New RAM buffer: " << currentFileName << '\n';
}

void Microphone::finalizeCurrentWavBuffer() {
    if (currentWavBuffer.size() <= 44) {
        return; // No audio data recorded
    }

    uint32_t dataSize = static_cast<uint32_t>(currentWavBuffer.size() - 44);
    writeWavHeaderInRam(currentWavBuffer, dataSize);

    std::cout << "[AUDIO] RAM buffer finalized. Index: " << activeFileIndex
              << " | Audio bytes: " << dataSize << '\n';

    std::lock_guard<std::mutex> lock(chunksMutex);
    readyChunks.push_back(MemoryChunk{
        activeFileIndex,
        currentFileName,
        std::move(currentWavBuffer)
    });
    currentWavBuffer.clear();
}

void Microphone::run() {
    if (!openSerialPort()) {
        isRunning.store(false);
        return;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint64_t currentIndex = sharedFileIndex.load(std::memory_order_acquire);
    startNewWavBuffer(currentIndex);

    constexpr size_t BUFFER_SIZE = 4096;
    uint8_t buffer[BUFFER_SIZE];

    auto lastDataReceived = std::chrono::steady_clock::now();
    std::cout << "[AUDIO] Recording in RAM started\n";

    while (isRunning.load(std::memory_order_acquire)) {
        uint64_t newIndex = sharedFileIndex.load(std::memory_order_acquire);
        if (newIndex != currentIndex) {
            std::cout << "\n[AUDIO] RAM buffer rotation: " << currentIndex << " -> " << newIndex << '\n';
            finalizeCurrentWavBuffer();
            currentIndex = newIndex;
            startNewWavBuffer(currentIndex);
        }

        pollfd descriptor{};
        descriptor.fd = serialFd;
        descriptor.events = POLLIN;

        int result = poll(&descriptor, 1, 100);
        if (result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[AUDIO] Poll error\n";
            break;
        }

        if (result == 0) {
            auto now = std::chrono::steady_clock::now();
            if (now - lastDataReceived >= std::chrono::seconds(2)) {
                std::cerr << "\n[AUDIO] No data received for 2 seconds\n";
                break;
            }
            continue;
        }

        if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) {
            std::cerr << "[AUDIO] Serial connection lost\n";
            break;
        }

        ssize_t bytesRead = read(serialFd, buffer, BUFFER_SIZE);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            std::cerr << "[AUDIO] Read error\n";
            break;
        }

        if (bytesRead == 0) continue;

        lastDataReceived = std::chrono::steady_clock::now();

        newIndex = sharedFileIndex.load(std::memory_order_acquire);
        if (newIndex != currentIndex) {
            finalizeCurrentWavBuffer();
            currentIndex = newIndex;
            startNewWavBuffer(currentIndex);
        }

        currentWavBuffer.insert(currentWavBuffer.end(), buffer, buffer + bytesRead);
    }

    finalizeCurrentWavBuffer();
    closeSerialPort();
    isRunning.store(false);
    std::cout << "[AUDIO] Worker stopped\n";
}