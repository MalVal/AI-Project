cmake_minimum_required(VERSION 4.3)
project(RaspberryPi)

set(CMAKE_CXX_STANDARD 20)

add_executable(Acquisition code/main.cpp
        code/Threads/MainThread.cpp
        code/Threads/MainThread.h
        code/Threads/Workers/Accelerometer.cpp
        code/Threads/Workers/Accelerometer.h
        code/Threads/Workers/ReedRelay.cpp
        code/Threads/Workers/ReedRelay.h
        code/Threads/Workers/ReedLapEvent.h
        code/configuration.h)
