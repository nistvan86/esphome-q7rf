# esphome-q7rf

This is an ESPHome custom component which allows you to control a Computherm/Delta Q7RF/Q8RF receiver equiped furnace using a TI CC1101 transceiver module. It defines a switch platform for state toggling and a service for pairing.

I've tested this project with an ESP8266 module (NodeMCU). It should work with the ESP32 as well, since protocol timing critical part is done by the CC1101 modem.

Current tested compatible ESPHome version: v2021.10.3

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

## ESPHome setup

If you are not familiar with ESPHome and its integration with Home Assistant, please read it first in the [official manual](https://esphome.io/guides/getting_started_hassio.html).

Add this component using the following configuration in your node's yaml file:

    external_components:
      - source: github://nistvan86/esphome-q7rf@main
        components: [ q7rf ]

    switch:
      - platform: q7rf
        name: Q7RF switch
        cs_pin: D8
        q7rf_device_id: 0x6ed5
        q7rf_resend_interval: 60000
        q7rf_turn_on_watchdog_interval: 0

    spi:
      clk_pin: D5
      miso_pin: D6
      mosi_pin: D7

Where:
* `q7rf_device_id` (required): is a 16 bit transmitter specific ID and learnt by the receiver in the pairing process. If you operate multiple furnaces in the vicinity you must specify unique IDs for each transmitter. You can generate random identifiers with [random-hex](https://www.browserling.com/tools/random-hex) (use 4 digits).

* `q7rf_resend_interval` (optional): specifies how often to repeat the last command in milliseconds. Since this is a simplex protocol, there's no response arriving from the receiver and we need to compensate for corrupt or lost messages by repeating them.

  Default is: 60000 ms (1 minute)

* `q7rf_turn_on_watchdog_interval` (optional): specifies how long the furnace can stay turned on after the last `write_state` call arrived for the switch component in milliseconds. This can be used for example in conjunction with the `keep_alive` setting of Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) component.

  Default is: 0 ms (no watchdog).

  Example: 900000 ms (15 minutes) and generic thermostat `keep_alive` set to 3 minutes

Once flashed, check the logs (or the UART output of the ESP8266) to see if configuration was successful.

You should see similar lines (note: C/D lines are only visible if you left the logger's level at the default DEBUG or lower):

During the `setup()` initialization:

    [I][q7rf.switch:x]: CC1101 initialized.

During configuration print:

    [C][q7rf.switch:x]: Q7RF:
    [C][q7rf.switch:x]:   CC1101 CS Pin: GPIO15
    [C][q7rf.switch:x]:   Q7RF Device ID: 0x6ed5
    [C][q7rf.switch:x]:   Q7RF Resend interval: 60000 ms
    [C][q7rf.switch:x]:   Q7RF Turn on watchdog interval: 0 ms

In Home Assistant under _Configuration_ → _Entities_ you should have a new switch with the same name you have specified ("Q7RF switch" in this example). In case you have disabled the automatic dashboard, add the switch to one of your dashboards. Find it and try toggling it. In the ESPHome log output you should see the component reacting:

    [D][switch:x]: 'Q7RF switch' Turning ON.
    [D][switch:x]: 'Q7RF switch': Sending state ON
    [D][q7rf.switch:x]: Handling prioritized message.
    [D][q7rf.switch:x]: Sending message: HEAT ON

## Pairing with the receiver

In order to make the receiver recognize the transmitter, we need to execute the pairing process.

Go to Home Assistant's _Developer tools_ → _Services_ and select the service `esphome.<NODE_NAME>_q7rf_pair`. Press and hold the M/A button on the receiver until it starts flashing green. Now press _Call service_ in the _Services_ page. The receiver should stop flashing, and the pairing is now complete. The receiver should react now if you try toggling the associated Home Assistant UI switch.

If you wish to reset and use your original wireless thermostat, once again set the receiver into learning mode with the M/A button, then hold the SET + DAY button on your wireless thermostat until the blinking stops. The receiver only listens to the device currently paired.

## Usage example

You can configure Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) to control the furnace (use the new switch as the `heater`).

## Resources

* The [cc1101-ook library](https://github.com/martyrs/cc1101-ook) which functioned as a template for the communication best practices with the modem.
* [denx's awesome article series](https://ardu.blog.hu/2019/04/17/computherm_q8rf_uj_kihivas_part) about reverse engineering the Q8RF's protocol. Unfortunatelly it's only
  available in Hungarian.
* [CC1101 product manual](http://www.ti.com/lit/ds/symlink/cc1101.pdf) from TI
