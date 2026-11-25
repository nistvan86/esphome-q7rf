# esphome-q7rf

This is an ESPHome custom component which allows you to control a Computherm/Delta Q7RF/Q8RF receiver equiped furnace using a TI CC1101 transceiver module. It defines a switch platform for state toggling and a service for pairing.

The project was originally tested with an ESP8266 module (NodeMCU). It is also tested on an ESP32-C3 (ESP32-C3-Zero) using the ESP-IDF framework. Protocol timing critical parts are done by the CC1101 modem.

Current tested compatible ESPHome versions:
* v2025.12.2 (ESP8266, Q7RF)
* v2026.4.0 (ESP32-C3, ESP-IDF, Q8RF zone 1)

**Use this project at your own risk. Reporting and/or fixing issues is always welcome.**

## Hardware
You need a CC1101 module which is assembled for 868 MHz. The chip on its own can be configured for many targets, but the antenna design on the board have to be tuned for the specific frequency in mind.

The CC1101 module is connected to the standard SPI pins of ESP8266 (secondary SPI PINs, the first set is used by the SPI flash chip).

Connections:

[CC1101 868MHz module pinouts](./doc/cc1101-pinout.jpg)

[NodeMCU module pinouts](./doc/nodemcu.jpg)

    NODEMCU               CC1101
    ============================
    3.3V                  VCC
    GND                   GND
    D7 (GPIO13/HMOSI)     MOSI
    D5 (GPIO14/HSCLK)     SCLK
    D6 (GPIO12/HMISO)     MISO
    D8 (GPIO15/HCS)       CSN

On ESP32 any free GPIOs can be used. A tested ESP32-C3-Zero wiring:

    ESP32-C3-ZERO         CC1101
    ============================
    3V3                   VCC
    GND                   GND
    GPIO9                 MOSI
    GPIO8                 SCLK
    GPIO7                 MISO
    GPIO6                 CSN

GPIO8/GPIO9 are strapping pins on the ESP32-C3. ESPHome prints a warning about them, which is harmless here, because the CC1101 inputs do not pull these lines at boot.

GDO0/GDO2 do not need to be connected.

## ESPHome setup

If you're not familiar with ESPHome and its integration with Home Assistant, please read the relevant section in the [official manual](https://esphome.io/guides/getting_started_hassio.html).

Add this component using the following configuration in your node's yaml file:

    external_components:
      - source: github://nistvan86/esphome-q7rf@main
        components: [ q7rf ]

    switch:
      - platform: q7rf
        id: q7rf_switch
        name: Q7RF switch
        cs_pin: D8
        q7rf_device_id: 0x6ed5
        q7rf_resend_interval: 60000
        q7rf_turn_on_watchdog_interval: 0

    spi:
      clk_pin: D5
      miso_pin: D6
      mosi_pin: D7

    api:
      custom_services: true

Where:
* `q7rf_device_id` (required): is a 16 bit transmitter specific ID and learnt by the receiver in the pairing process. If you operate multiple furnaces in the vicinity you must specify unique IDs for each transmitter. You can generate random identifiers for example with [random-hex](https://www.browserling.com/tools/random-hex) (use 4 digits).

* `q7rf_resend_interval` (optional): specifies how often to repeat the last state in milliseconds. Since this is a simplex protocol, there's no response arriving from the receiver and we need to compensate for corrupt or lost messages by repeating them.

  Default is: 60000 ms (1 minute)

* `q7rf_turn_on_watchdog_interval` (optional): specifies how long the furnace can stay turned on after the last `write_state` call arrived for the switch component in milliseconds. This can be used for example in conjunction with the `keep_alive` setting of Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) component to add an additional safe guard against the crash of HA and to prevent excessive heating costs.

  Default is: 0 ms (no watchdog).

  Example: 900000 ms (15 minutes) and generic thermostat `keep_alive` set to 3 minutes

* `q7rf_zone` (optional): heating zone of a Q8RF receiver, `1`-`4`. The Q7RF receiver has a single zone, leave it at the default.

  Default is: 1

* `q7rf_framing` (optional): how the messages are put on air.
  * `legacy`: a single packet with 2 bursts (~80 ms), separated by a short gap. This is the original behaviour of this component and works with Q7RF receivers (and some Q8RF receivers).
  * `extended`: 16 bursts (~670 ms), each followed by a proper "stop" gap, similar to the original wireless thermostats. Use this if the receiver does not react to pairing with `legacy` (seen with Q8RF receivers). The bursts are sent without blocking the ESPHome main loop.

  Default is: `legacy`

* `q7rf_frequency` (optional): carrier frequency, between 863 and 870 MHz. Original wireless thermostats were measured between 868.275 and 868.35 MHz.

  Default is: 868.285MHz. Example: `868.35MHz` (tested with a Q8RF receiver, together with `q7rf_framing: extended`).

Once flashed, check the logs (or the UART output of the ESP) to see if configuration was successful.

You should see similar lines (note: C/D lines are only visible if you left the logger's level at the default DEBUG or lower):

During the `setup()` initialization:

    [I][q7rf.switch:x]: CC1101 found with partnum: 00 and version: 14
    [I][q7rf.switch:x]: CC1101 initialized.

During configuration print:

    [C][q7rf.switch:x]: Q7RF:
    [C][q7rf.switch:x]:   CC1101 CS Pin: GPIO15
    [C][q7rf.switch:x]:   Q7RF Device ID: 0x6ed5
    [C][q7rf.switch:x]:   Q7RF Zone: 1
    [C][q7rf.switch:x]:   Q7RF Frequency: 868.285 MHz (FREQ=0x216544)
    [C][q7rf.switch:x]:   Q7RF Framing: legacy, 2 bursts per message
    [C][q7rf.switch:x]:   Q7RF Resend interval: 60000 ms
    [C][q7rf.switch:x]:   Q7RF Turn on watchdog interval: 0 ms
    [C][q7rf.switch:x]:   Encoded PAIR: e3...
    [C][q7rf.switch:x]:   Encoded HEAT ON: e3...
    [C][q7rf.switch:x]:   Encoded HEAT OFF: e3...

The `Encoded ...` lines show the exact bytes loaded into the CC1101 TX FIFO for one packet, which helps when comparing with a radio capture.

In Home Assistant under _Settings_ → _Entities_ you should see a new switch with the same name you have specified ("Q7RF switch" in this example). In case you have disabled the automatic dashboard, add the switch to one of your dashboards. Find it and try toggling it. In the ESPHome log output you should see the component reacting:

    [D][switch:x]: 'Q7RF switch' Turning ON.
    [D][switch:x]: 'Q7RF switch': Sending state ON
    [D][q7rf.switch:x]: Handling prioritized message.
    [D][q7rf.switch:x]: Sending message: HEAT ON (2 bursts)
    [D][q7rf.switch:x]: Sent message: HEAT ON

## Pairing with the receiver

In order to make the receiver recognize the transmitter, we need to execute the pairing process.

Put the receiver into learning mode:
* **Q7RF**: press and hold the M/A button until it starts flashing green.
* **Q8RF**: press and hold the button of the zone set in `q7rf_zone` until its LED starts blinking.

Then go to Home Assistant's _Developer tools_ → _Actions_, select the service `esphome.<NODE_NAME>_q7rf_pair` and press _Perform action_. ESPHome log will show a similar output:

    [I][q7rf.switch:x]: Enqueued pairing.
    [D][q7rf.switch:x]: Handling prioritized message.
    [D][q7rf.switch:x]: Sending message: PAIR (2 bursts)
    [D][q7rf.switch:x]: Sent message: PAIR

The receiver should stop flashing, and the pairing is now complete. Test if the associated Home Assistant switch controls the furnace correctly by manually toggling it.

Instead of the action you can also add a button to the node, which triggers the pairing directly:

    button:
      - platform: template
        name: Pairing
        entity_category: config
        on_press:
          - lambda: id(q7rf_switch).on_pairing();

### Pairing troubleshooting

If the log shows `Sent message: PAIR` but the receiver keeps flashing:
1. Set `q7rf_framing: extended`, flash, and pair again.
2. If it still fails, also try `q7rf_frequency: 868.35MHz`.
3. Make sure the CC1101 board is an 868 MHz variant and has an antenna soldered to the ANT pad.

If the log shows `Pairing ignored: CC1101 not initialized.`, the modem could not be configured at boot. Check the wiring and the boot log.

## Usage example

You can configure Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) to control the furnace (use the new switch as the `heater`).

## Q&A

**How can I revert back to the bundled wireless thermostat?**

Set the receiver into learning mode with the M/A button, then hold the SET + DAY button on your wireless thermostat until the blinking stops.

**Can I use this simultaneously with the wireless thermostat the receiver came with?**

No. Both transmitters should have their own device IDs. The receiver only listens to the currently paired device. Even if you manage to clone the wireless thermostat's ID, transmitters will not know about each other and switch the furnace in a hectic way (especially with the message repeating in place).

Since the CC1101 is a transreceiver it would be possible to extend this project to allow pairing the wireless thermostat to this ESPHome component (using a separate device ID pair), then listen and forward it's messages. In my opinion the protocol is too limited to do anything useful with the original thermostat in a home automation environment: no temperature or scheduling is transmitted, only on/off signal is sent. It's better to replace it completely.

**How can I control multiple Q8RF zones?**

Use a separate ESPHome node with its own CC1101 for each zone, each with a unique `q7rf_device_id` and its own `q7rf_zone`, then pair each node with its zone on the receiver. Multiple `q7rf` switches on a single node (separate CC1101 modules on different `cs_pin`s) are untested, and they would all register the same `q7rf_pair` action, so use pairing buttons there.

## Resources

* The [cc1101-ook library](https://github.com/martyrs/cc1101-ook) which functioned as a template for the communication best practices with the modem.
* [denx's awesome article series](https://ardu.blog.hu/2019/04/17/computherm_q8rf_uj_kihivas_part) about reverse engineering the Q8RF's protocol. Unfortunatelly it's only
  available in Hungarian.
* [Issue #2](https://github.com/nistvan86/esphome-q7rf/issues/2) about Q8RF zone addressing.
* [ComputhermRF](https://github.com/denxhun/ComputhermRF) and [esphome_component_computhermqrf](https://github.com/afarago/esphome_component_computhermqrf), alternative implementations using RFM117W/RFM217W modules, which served as reference for the extended framing.
* [CC1101 product manual](http://www.ti.com/lit/ds/symlink/cc1101.pdf) from TI
