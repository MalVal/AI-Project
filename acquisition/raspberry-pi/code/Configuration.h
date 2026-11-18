//
// Created by geele on 27-10-26.
//

#ifndef ACQUISITION_CONFIGURATION_H
#define ACQUISITION_CONFIGURATION_H

#include <string>
#include <cstdint>

namespace Configuration {
    constexpr int LAPS_PER_FILE = 5;

    const std::string SERVER_IP = "192.168.137.1";
    constexpr uint16_t SERVER_PORT = 9000;

    const std::string SERIAL_PORT_RELAIS_REED = "/dev/serial/by-path/platform-xhci-hcd.1-usb-0:1:1.0";
    const std::string SERIAL_PORT_MICRO = "/dev/serial/by-path/platform-xhci-hcd.0-usb-0:1:1.0";
}

#endif
