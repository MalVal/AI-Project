//
// Created by geele on 27-10-26.
//

#ifndef ACQUISITION_CONFIGURATION_H
#define ACQUISITION_CONFIGURATION_H

#include <string>

namespace Configuration {
    constexpr int LAPS_PER_FILE = 5;

    const std::string SERIAL_PORT_RELAIS_REED = "/dev/serial/by-path/platform-xhci-hcd.1-usb-0:1:1.0";
    const std::string SERIAL_PORT_MICRO = "/dev/serial/by-path/platform-xhci-hcd.0-usb-0:1:1.0";

}

#endif //ACQUISITION_CONFIGURATION_H
