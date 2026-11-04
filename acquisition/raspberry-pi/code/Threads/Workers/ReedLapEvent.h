//
// Created by geele on 27-10-26.
//

#ifndef ACQUISITION_REEDLAPEVENT_H
#define ACQUISITION_REEDLAPEVENT_H

#include <cstdint>

struct ReedLapEvent {
    uint64_t lapIndex;
    uint32_t arduinoTimestampMs;
};

#endif //ACQUISITION_REEDLAPEVENT_H
