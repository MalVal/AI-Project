#include "Microphone.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>

namespace fs = std::filesystem;

// WAV helpers: write explicitly in little-endian format
static void writeLE16(std::ofstream& file, uint16_t value)
{
    char bytes[2] = {
        static_cast<char>(value),
        static_cast<char>(value >> 8)
    };

    file.write(bytes, 2);
}

static void writeLE32(std::ofstream& file, uint32_t value)
{
    char bytes[4] = {
        static_cast<char>(value),
        static_cast<char>(value >> 8),
        static_cast<char>(value >> 16),
        static_cast<char>(value >> 24)
    };

    file.write(bytes, 4);
}

// --------------------------------------------------
// CONSTRUCTOR / DESTRUCTOR
// --------------------------------------------------

Microphone::Microphone(std::string portPath, const std::atomic<uint64_t>& fileIndexRef)
    : serialPortPath(portPath), sharedFileIndex(fileIndexRef)
{
}

Microphone::~Microphone()
{
    stop();
}

// --------------------------------------------------
// START / STOP
// --------------------------------------------------

void Microphone::start()
{
    bool expected = false;

    if (!isRunning.compare_exchange_strong(expected, true))
        return;

    // Join a previous completed thread before restarting
    if (workerThread.joinable())
        workerThread.join();

    workerThread = std::thread(&Microphone::run, this);
}

void Microphone::stop()
{
    isRunning.store(false);

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

// --------------------------------------------------
// SERIAL PORT
// --------------------------------------------------

bool Microphone::openSerialPort()
{
    serialFd = open(
        serialPortPath.c_str(),
        O_RDONLY | O_NOCTTY | O_NONBLOCK
    );

    if (serialFd < 0)
    {
        std::cerr << "[AUDIO] Failed to open "
                  << serialPortPath << " : "
                  << strerror(errno) << '\n';
        return false;
    }

    termios tty{};

    if (tcgetattr(serialFd, &tty) != 0)
    {
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

    if (tcsetattr(serialFd, TCSANOW, &tty) != 0)
    {
        std::cerr << "[AUDIO] tcsetattr error\n";
        closeSerialPort();
        return false;
    }

    tcflush(serialFd, TCIFLUSH);

    std::cout << "[AUDIO] Serial port opened: " << serialPortPath << '\n';

    return true;
}

void Microphone::closeSerialPort()
{
    if (serialFd >= 0)
    {
        close(serialFd);
        serialFd = -1;
    }
}

// --------------------------------------------------
// WAV MANAGEMENT
// --------------------------------------------------

bool Microphone::createNewWavFile(uint64_t fileIndex)
{
    fs::create_directories(outputDirectory);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_r(&time, &localTime);

    std::ostringstream filename;

    filename << outputDirectory
             << "/audio_"
             << std::put_time(&localTime, "%Y%m%d_%H%M%S")
             << "_"
             << fileIndex
             << ".wav";

    wavFile.open(filename.str(), std::ios::binary);

    if (!wavFile.is_open())
    {
        std::cerr << "[AUDIO] Cannot create WAV file\n";
        return false;
    }

    audioBytesWritten = 0;
    activeFileIndex = fileIndex;

    // Reserve 44 bytes for WAV header
    char header[44] = {};
    wavFile.write(header, sizeof(header));

    std::cout << "[AUDIO] New file: "
              << filename.str() << '\n';

    return wavFile.good();
}

void Microphone::writeWavHeader(uint32_t dataSize)
{
    if (!wavFile.is_open())
        return;

    const uint32_t fileSize = dataSize + 36;

    const uint32_t byteRate = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE / 8;

    const uint16_t blockAlign = CHANNELS * BITS_PER_SAMPLE / 8;

    wavFile.seekp(0);

    wavFile.write("RIFF", 4);
    writeLE32(wavFile, fileSize);

    wavFile.write("WAVE", 4);

    wavFile.write("fmt ", 4);
    writeLE32(wavFile, 16);

    writeLE16(wavFile, 1); // PCM
    writeLE16(wavFile, CHANNELS);
    writeLE32(wavFile, SAMPLE_RATE);
    writeLE32(wavFile, byteRate);
    writeLE16(wavFile, blockAlign);
    writeLE16(wavFile, BITS_PER_SAMPLE);

    wavFile.write("data", 4);
    writeLE32(wavFile, dataSize);

    wavFile.seekp(0, std::ios::end);
}

void Microphone::closeWavFile()
{
    if (wavFile.is_open())
    {
        writeWavHeader(audioBytesWritten);

        wavFile.close();

        std::cout << "[AUDIO] File closed. Index: "
                  << activeFileIndex
                  << " | Bytes: "
                  << audioBytesWritten
                  << '\n';
    }
}

// --------------------------------------------------
// WORKER THREAD
// --------------------------------------------------

void Microphone::run()
{
    if (!openSerialPort())
    {
        isRunning.store(false);
        return;
    }

    // Arduino reset delay
    std::this_thread::sleep_for(
        std::chrono::seconds(2)
    );

    // Read initial index from MainThread
    uint64_t currentIndex = sharedFileIndex.load(std::memory_order_acquire);

    if (!createNewWavFile(currentIndex))
    {
        closeSerialPort();
        isRunning.store(false);
        return;
    }

    constexpr size_t BUFFER_SIZE = 4096;

    char buffer[BUFFER_SIZE];

    auto lastDataReceived = std::chrono::steady_clock::now();

    std::cout << "[AUDIO] Recording started\n";

    while (isRunning.load(std::memory_order_acquire))
    {
        // ------------------------------------------
        // 1. Check MainThread file index
        // ------------------------------------------

        uint64_t newIndex = sharedFileIndex.load(std::memory_order_acquire);

        if (newIndex != currentIndex)
        {
            std::cout << "\n[AUDIO] File rotation: "
                      << currentIndex << " -> "
                      << newIndex << '\n';

            closeWavFile();

            currentIndex = newIndex;

            if (!createNewWavFile(currentIndex))
                break;
        }

        // ------------------------------------------
        // 2. Wait for serial data
        // ------------------------------------------

        pollfd descriptor{};

        descriptor.fd = serialFd;
        descriptor.events = POLLIN;

        int result = poll(&descriptor, 1, 100);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;

            std::cerr << "[AUDIO] Poll error\n";
            break;
        }

        if (result == 0)
        {
            // No data: check the 2-second timeout
            auto now = std::chrono::steady_clock::now();

            if (now - lastDataReceived >=
                std::chrono::seconds(2))
            {
                std::cerr << "\n[AUDIO] No data received "
                          << "for 2 seconds\n";
                break;
            }

            continue;
        }

        if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP))
        {
            std::cerr << "[AUDIO] Serial connection lost\n";
            break;
        }

        // ------------------------------------------
        // 3. Read audio data
        // ------------------------------------------

        ssize_t bytesRead =
            read(serialFd, buffer, BUFFER_SIZE);

        if (bytesRead < 0)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR)
                continue;

            std::cerr << "[AUDIO] Read error\n";
            break;
        }

        if (bytesRead == 0)
            continue;

        lastDataReceived = std::chrono::steady_clock::now();

        // ------------------------------------------
        // 4. Check index again before writing
        // ------------------------------------------

        newIndex =
            sharedFileIndex.load(std::memory_order_acquire);

        if (newIndex != currentIndex)
        {
            closeWavFile();

            currentIndex = newIndex;

            if (!createNewWavFile(currentIndex))
                break;
        }

        // ------------------------------------------
        // 5. Write received audio
        // ------------------------------------------

        wavFile.write(buffer, bytesRead);

        if (!wavFile.good())
        {
            std::cerr << "[AUDIO] WAV write error\n";
            break;
        }

        audioBytesWritten +=
            static_cast<uint32_t>(bytesRead);
    }

    // ------------------------------------------
    // 6. Cleanup
    // ------------------------------------------

    closeWavFile();
    closeSerialPort();

    isRunning.store(false);

    std::cout << "[AUDIO] Worker stopped\n";
}
