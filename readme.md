# q7rf-esphome

This is an ESPHome custom component which allows you to control a Computherm/Delta Q7RF/Q8RF receiver equiped furnace using a TI CC1101 transceiver module. This component defines a switch platform for state toggling and a service for pairing.

I've tested this project with an ESP8266 module (NodeMCU). It should work with the ESP32 as well, since protocol timing critical part is done by the CC1101 modem, but haven't tried it yet.

Tested ESPHome version: 1.15.3

**Use this project at your own risk. Reporting and/or fixing issues is always welcome.**

## Hardware
You need a CC1101 module which is assembled for 868 MHz. The chip on its own can be configured for many targets, but the antenna design on the board needs to be tuned for the specific frequency in mind.

CC1101 module needs to be connected to the standard SPI pins of ESP8266 (secondary SPI PINs, the first set is used by the SPI flash chip).

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

If you are not familiar with ESPHome and its integration with Home Assistant, please read it first in the [official manual](https://esphome.io/guides/getting_started_hassio.html). You need to have a node preconfigured and visible in Home Assistant before following the next steps:

- In your configuration directory create a `custom_components` subfolder and enter it.
- Clone this project with the command: `git clone https://github.com/nistvan86/q7rf-esphome.git q7rf`
- Add the necessary configuration blocks to your node's config yaml file:

        switch:
          - platform: q7rf
            name: Q7RF switch
            cs_pin: D8
            q7rf_device_id: 0x6ed5
            q7rf_resend_interval: 60000

        spi:
          clk_pin: D5
          miso_pin: D6
          mosi_pin: D7

Where:
- `q7rf_device_id` (required): is a 16 bit transmitter specific ID and learnt by the receiver in the pairing process. If you operate multiple furnaces in the vicinity you must specify unique IDs for each transmitter to control them.
- `q7rf_resend_interval` (optional): specifies how often to repeat the last state set command. Since this is a simplex protocol, there's no response coming for messages and we need to compensate for corrupt or lost messages by repeating them. Default is: 60000 ms

Once flashed, check the ESPHome logs (or the UART output of the ESP8266) to see if configuration was successful. You should see similar lines (note: C/D lines are only visible if you left the logger's loglevel at the default DEBUG or lower):

During the `setup()` initialization:

    [I][q7rf.switch:316]: CC1101 reset successful.

During configuration print:

    [C][q7rf.switch:351]: Q7RF:
    [C][q7rf.switch:352]:   CC1101 CS Pin: 15
    [C][q7rf.switch:353]:   Q7RF Device ID: 0x6ed5
    [C][q7rf.switch:354]:   Q7RF Resend interval: 60000 ms

In Home Assistant under _Configuration_ → _Entities_ you should have a new switch with the same name you have specified ("Q7RF switch" in this example). In case you have disabled the automatic dashboard, add the switch to one of your dashboards. Find it and try toggling it. In the ESPHome log output you should see the component reacting:

    [D][switch:021]: 'Q7RF switch' Turning ON.
    [D][switch:045]: 'Q7RF switch': Sending state ON
    [D][q7rf.switch:360]: Handling prioritized message: 0x01
    [D][q7rf.switch:278]: Sending message: 0x01

## Pairing with the receiver

In order to make the receiver recognize the transmitter, we need to execute the pairing process.

Go to Home Assistant's _Developer tools_ → _Services_ and select the service `esphome.<node_name>_<switch_name>.pair`. Press and hold the M/A button on the receiver until it starts flashing green. Now press _Call service_ in the _Services_ page. The receiver should stop flashing, and the pairing is now complete. The receiver should react now if you try toggling the associated Home Assistant UI switch.

If you wish to reset and use your original wireless thermostat, once again set the receiver into learning mode with the M/A button, then hold the SET + DAY button on your wireless thermostat until the blinking stops. The receiver only listens to the device currently paired.

## Usage example

You can configure Home Assistant's [generic thermostat](https://www.home-assistant.io/integrations/generic_thermostat/) to control the furnace (use the new switch as the `heater`).

## Resources

* The cc1101-ook library which functioned as a template for the communication best practices with the modem (https://github.com/martyrs/cc1101-ook)
* denx's awesome article series about reverse engineering the Q8RF's protocol. Unfortunatelly it's only
  available in Hungarian. (https://ardu.blog.hu/2019/04/17/computherm_q8rf_uj_kihivas_part)
* CC1101 product manual from Ti: http://www.ti.com/lit/ds/symlink/cc1101.pdf
