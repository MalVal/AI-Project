#ifndef UNTITLED_MAINTHREAD_H
#define UNTITLED_MAINTHREAD_H

#include <atomic>
#include <thread>

#include "Workers/ReedLapEvent.h"

class MainThread {
public:
    MainThread();
    ~MainThread();
    void waitEnd();

    // Empêche la copie
    MainThread(const MainThread&) = delete;
    MainThread& operator=(const MainThread&) = delete;

    void start();
    void stop();

private:
    void run();
    void handleLapEvent(const ReedLapEvent& lap);

    std::atomic<bool> isRunning{false};
    std::thread workerThread;
    
    // Le signal de changement de fichier partagé avec les workers
    std::atomic<uint64_t> currentFileIndex{0};
    std::ofstream lapLog;
};

#endif //UNTITLED_MAINTHREAD_H