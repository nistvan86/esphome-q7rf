#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace q7rf {

class Q7RFSwitch : public switch_::Switch,
                   public Component,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                         spi::DATA_RATE_1KHZ> {
 protected:
  bool initialized_ = false;
  uint16_t q7rf_device_id_ = 0;
  uint8_t msg_pair_[45];
  uint8_t msg_heat_on_[45];
  uint8_t msg_heat_off_[45];

 private:
  bool reset_cc();
  void send_cc_cmd(uint8_t cmd);
  void read_cc_register(uint8_t reg, uint8_t* value);
  void read_cc_config_register(uint8_t reg, uint8_t* value);
  void write_cc_register(uint8_t reg, uint8_t* value, size_t length);
  void write_cc_config_register(uint8_t reg, uint8_t value);
  // void send_cc_data(uint8_t* value);

  void encode_bits(uint16_t byte, uint8_t pad_to_length, char** dest);
  void get_msg(uint8_t cmd, uint8_t msg[45]);

 public:
  void setup() override;
  void write_state(bool state) override;
  void dump_config() override;
  void set_q7rf_device_id(uint16_t id);
};

}  // namespace q7rf
}  // namespace esphome