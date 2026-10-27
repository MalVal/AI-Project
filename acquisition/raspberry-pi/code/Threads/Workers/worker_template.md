# Structure Standard d'un Thread Worker

Ce document explique comment créer un nouveau composant (Worker) pour l'acquisition de données. Chaque nouveau capteur doit respecter ce modèle pour garantir la sécurité des threads (thread-safety) et une écriture optimisée dans les fichiers.

## 1. Fichier d'en-tête
Header : `MySensor.h`

Le fichier `.h` doit inclure une référence atomique pour suivre les changements de fichiers ordonnés par le `MainThread`.

```cpp
#ifndef MY_SENSOR_H
#define MY_SENSOR_H

#include <atomic>
#include <string>
#include <thread>
#include <cstdint>
#include <fstream>

class MySensor {
public:
    // 1. Constructor takes the atomic file index by reference
    explicit MySensor(const std::atomic<uint64_t>& fileIndexRef);
    ~MySensor();

    // 2. Prevent copy to avoid thread duplication
    MySensor(const MySensor&) = delete;
    MySensor& operator=(const MySensor&) = delete;

    // 3. Thread lifecycle management
    void start();
    void stop();

private:
    void run();
    
    // Custom sensor methods here
    bool readSensorData(double& dataOut);

    // Shared index from MainThread
    const std::atomic<uint64_t>& sharedFileIndex;

    // Thread management variables
    std::atomic<bool> isRunning{false};
    std::thread workerThread;
};

#endif // MY_SENSOR_H