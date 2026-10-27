#include "MainThread.h"
#include <iostream>
#include <fstream>
#include <chrono>

#include "Workers/ReedLapEvent.h"
#include "../Configuration.h"
#include "Workers/ReedRelay.h"
#include "Workers/Accelerometer.h"

MainThread::MainThread() : lapLog("laps_index.csv", std::ios::app) {
}

MainThread::~MainThread() {
    stop();
}

void MainThread::waitEnd() {
    workerThread.join();
}

void MainThread::start() {
    if (!isRunning.exchange(true)) {
        std::cout << "[MAIN] Lancement main thread\n";
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

void MainThread::handleLapEvent(const ReedLapEvent& lap) {
    std::cout << "[MAIN] Lap " << lap.lapIndex << " : " << lap.arduinoTimestampMs << " ms\n";

    lapLog << lap.lapIndex << "," << lap.arduinoTimestampMs << "\n";
    lapLog.flush();

    if (lap.lapIndex > 0 && lap.lapIndex % Configuration::LAPS_PER_FILE == 0) {
        currentFileIndex.fetch_add(1, std::memory_order_relaxed);

        std::cout << "[MAIN] File rotation (New index: " << currentFileIndex.load() << ")\n";
    }
}

void MainThread::run() {
    auto onLap = [this](const ReedLapEvent& lap) {
        this->handleLapEvent(lap);
    };

    ReedRelay reed("/dev/ttyACM0", onLap);
    Accelerometer accel(currentFileIndex);

    reed.start();
    accel.start();

    while (isRunning.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    reed.stop();
    accel.stop();
}