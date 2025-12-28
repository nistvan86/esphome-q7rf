# esphome-q7rf

This is an ESPHome custom component which allows you to control a Computherm/Delta Q7RF/Q8RF receiver equiped furnace using a TI CC1101 transceiver module. It defines a switch platform for state toggling and a service for pairing.

I've tested this project with an ESP8266 module (NodeMCU). It should work with the ESP32 as well, since protocol timing critical part is done by the CC1101 modem.

Current tested compatible ESPHome version: v2025.12.2

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

If you're not familiar with ESPHome and its integration with Home Assistant, please read the relevant section in the [official manual](https://esphome.io/guides/getting_started_hassio.html).

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

    api:
	  custom_services: true

Where:
* `q7rf_device_id` (required): is a 16 bit transmitter specific ID and learnt by the receiver in the pairing process. If you operate multiple furnaces in the vicinity you must specify unique IDs for each transmitter. You can generate random identifiers for example with [random-hex](https://www.browserling.com/tools/random-hex) (use 4 digits).

* `q7rf_resend_interval` (optional): specifies how often to repeat the last state in milliseconds. Since this is a simplex protocol, there's no response arriving from the receiver and we need to compensate for corrupt or lost messages by repeating them.

  Default is: 60000 ms (1 minute)

* `q7rf_turn_on_watchdog_interval` (optional): specifies how long the furnace can stay turned on after the last `write_state` call arrived for the switch component in milliseconds. This can be used for example in conjunction with the `keep_alive` setting of Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) component to add an additional safe guard against the crash of HA and to prevent excessive heating costs.

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

In Home Assistant under _Settings_ → _Entities_ you should see a new switch with the same name you have specified ("Q7RF switch" in this example). In case you have disabled the automatic dashboard, add the switch to one of your dashboards. Find it and try toggling it. In the ESPHome log output you should see the component reacting:

    [D][switch:x]: 'Q7RF switch' Turning ON.
    [D][switch:x]: 'Q7RF switch': Sending state ON
    [D][q7rf.switch:x]: Handling prioritized message.
    [D][q7rf.switch:x]: Sending message: HEAT ON

## Pairing with the receiver

In order to make the receiver recognize the transmitter, we need to execute the pairing process.

Go to Home Assistant's _Developer tools_ → _Actions_ and select the service `esphome.<NODE_NAME>_q7rf_pair`. Press and hold the M/A button on the receiver until it starts flashing green. Now press _Perform action_ in the _Actions_ page. ESPHome log will show a similar output:

    [I][q7rf.switch:x]: Enqueued pairing.
    [D][q7rf.switch:x]: Handling prioritized message.
    [D][q7rf.switch:x]: Sent message: PAIR

The receiver should stop flashing, and the pairing is now complete. Test if the associated Home Assistant switch controls the furnace correctly by manually toggling it.

## Usage example

You can configure Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) to control the furnace (use the new switch as the `heater`).

## Q&A

**How can I revert back to the bundled wireless thermostat?**

Set the receiver into learning mode with the M/A button, then hold the SET + DAY button on your wireless thermostat until the blinking stops.

**Can I use this simultaneously with the wireless thermostat the receiver came with?**

No. Both transmitters should have their own device IDs. The receiver only listens to the currently paired device. Even if you manage to clone the wireless thermostat's ID, transmitters will not know about each other and switch the furnace in a hectic way (especially with the message repeating in place).

Since the CC1101 is a transreceiver it would be possible to extend this project to allow pairing the wireless thermostat to this ESPHome component (using a separate device ID pair), then listen and forward it's messages. In my opinion the protocol is too limited to do anything useful with the original thermostat in a home automation environment: no temperature or scheduling is transmitted, only on/off signal is sent. It's better to replace it completely.

## Resources

* The [cc1101-ook library](https://github.com/martyrs/cc1101-ook) which functioned as a template for the communication best practices with the modem.
* [denx's awesome article series](https://ardu.blog.hu/2019/04/17/computherm_q8rf_uj_kihivas_part) about reverse engineering the Q8RF's protocol. Unfortunatelly it's only
  available in Hungarian.
* [CC1101 product manual](http://www.ti.com/lit/ds/symlink/cc1101.pdf) from TI
