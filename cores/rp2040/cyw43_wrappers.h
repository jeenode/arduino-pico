/*
    CYW43 TCP/Ethernet wrappers
    Copyright (c) 2023 Earle F. Philhower, III <earlephilhower@yahoo.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#include <Arduino.h>
#include <pico/cyw43_driver.h>

extern bool __isPicoW;
#ifdef __cplusplus
extern "C" {
#endif
void init_cyw43_wifi();
void cyw43_pinMode(pin_size_t pin, PinMode mode);
void cyw43_digitalWrite(pin_size_t pin, PinStatus val);
PinStatus cyw43_digitalRead(pin_size_t pin);
bool __cyw43PoweredOff();                  // WiFi.powerOff() is in effect and the chip is off (WL_ON low)
void __cyw43SetOffRequested(bool off);     // WiFi.powerOff() / powerOn()
bool __cyw43VsysReadBegin(pin_size_t pin); // analogRead(29) with the chip off: divider on, true when it was switched
void __cyw43VsysReadEnd(bool switched);    // ... and off again
#ifdef __cplusplus
}
#endif
