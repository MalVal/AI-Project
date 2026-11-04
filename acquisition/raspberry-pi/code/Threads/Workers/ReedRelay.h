//
// Created by geele on 27-10-26.
//

#ifndef ACQUISITION_REEDRELAY_H
#define ACQUISITION_REEDRELAY_H

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "ReedLapEvent.h"

class ReedRelay {
public:
    using LapCallback = std::function<void(const ReedLapEvent&)>;

    explicit ReedRelay(std::string portPath, LapCallback onLapDetected);
    ~ReedRelay();

    ReedRelay(const ReedRelay&) = delete;
    ReedRelay& operator=(const ReedRelay&) = delete;

    void start();
    void stop();

private:
    void run();
    int configureSerialPort(int fd);

    std::string serialPortPath;
    LapCallback lapCallback;
    uint64_t currentLap{0};

    std::atomic<bool> isRunning{false};
    std::thread workerThread;
};


#endif //ACQUISITION_REEDRELAY_H
