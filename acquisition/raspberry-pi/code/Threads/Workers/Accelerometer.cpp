#include "Accelerometer.h"
#include <iostream>
#include <array>
#include <chrono>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

Accelerometer::Accelerometer(const std::atomic<uint64_t>& fileIndexRef)
    : sharedFileIndex(fileIndexRef) {}

Accelerometer::~Accelerometer() {
    stop();
}

void Accelerometer::start() {
    if (!isRunning.exchange(true)) {
        workerThread = std::thread(&Accelerometer::run, this);
    }
}

void Accelerometer::stop() {
    if (isRunning.exchange(false)) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

void Accelerometer::run() {
    std::cout << "[ACCEL] Thread started\n";

    int address = -1;
    if (initializeAccelerometer(address) != 0) {
        isRunning.store(false);
        return;
    }

    // Gestion des fichiers optimisée
    uint64_t localFileIndex = sharedFileIndex.load(std::memory_order_relaxed);
    std::ofstream outFile("accel_data_" + std::to_string(localFileIndex) + ".csv");
    
    std::string buffer;
    buffer.reserve(8192); // Buffer de 8 Ko pour éviter les écritures disques constantes

    auto flushBuffer = [&]() {
        if (!buffer.empty()) {
            outFile.write(buffer.data(), buffer.size());
            buffer.clear();
        }
    };

    while (isRunning.load(std::memory_order_relaxed)) {
        // 1. Vérification de la demande de changement de fichier
        uint64_t currentGlobalIndex = sharedFileIndex.load(std::memory_order_relaxed);
        if (currentGlobalIndex != localFileIndex) {
            flushBuffer();
            outFile.close();
            localFileIndex = currentGlobalIndex;
            outFile.open("accel_data_" + std::to_string(localFileIndex) + ".csv");
            std::cout << "[ACCEL] Changement vers le fichier index " << localFileIndex << "\n";
        }

        // 2. Lecture et mise en mémoire tampon
        double x, y, z;
        if (readAcceleration(x, y, z)) {
            // Horodatage simplifié pour l'exemple
            auto now = std::chrono::system_clock::now().time_since_epoch();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            
            buffer += std::to_string(timestamp) + "," + 
                      std::to_string(x) + "," + 
                      std::to_string(y) + "," + 
                      std::to_string(z) + "\n";

            // Vider le buffer s'il dépasse une certaine taille (ex: 4 Ko)
            if (buffer.size() > 4096) {
                flushBuffer();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Nettoyage final
    flushBuffer();
    outFile.close();
    writeRegister(0x2A, 0x00);
    close(i2c_fd);
    std::cout << "[ACCEL] Thread stopped\n";
}

// ... Les fonctions initializeAccelerometer, writeRegister, readAcceleration 
// doivent être définies ici en tant que méthodes de classe (Accelerometer::nom_de_fonction), 
// en utilisant la variable membre i2c_fd au lieu de passer fd en paramètre.