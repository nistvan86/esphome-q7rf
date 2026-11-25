# Changelog

## 2026-11-25
- ESP32 / ESP-IDF support
    - fixed boot loop (`spi_device_release_bus` assert): the CC1101 reset sequence now toggles CS directly instead of releasing an unacquired SPI bus
    - SPI clock raised from 1 kHz to 1 MHz (ESP32 hardware SPI can't go that low)
    - wait for the synthesizer calibration after STX instead of aborting the transmission
- Fixed a stack buffer overflow in `encode_bits()` with 16 bit device IDs
- Added Q8RF support
    - `q7rf_zone` option for addressing Q8RF heating zones 1-4
    - `q7rf_framing: extended` option: 16 stop-framed bursts like the original thermostats, needed by some Q8RF receivers to accept pairing; sent without blocking the main loop
    - `q7rf_frequency` option
- CC1101 uses fixed packet length, returns to IDLE after each packet (no TX FIFO underflow)
- Encoded messages, zone, frequency and framing are printed in the config dump
- Warning is logged when pairing is requested before the CC1101 is initialized
- Updated readme

## 2021-11-09
- Added watchdog functionality
- Reworked into external component structure
- Bugfixes reported by 2225damjaich42 and Tamás Ficzere
    - don't strict check modem version (new variant of CC1101 appeared in the wild)
    - better pair service name
- Log sent command name instead of hex value
- Updated readme
- Added changelog

## 2021-11-04
- Updated for ESPHome v2021.10.3

## 2021-01-01
- Initial commit.
