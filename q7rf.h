#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/api/custom_api_device.h"

namespace esphome {
namespace q7rf {

static const uint8_t MSG_NONE = 0;
static const uint8_t MSG_HEAT_ON = 1;
static const uint8_t MSG_HEAT_OFF = 2;
static const uint8_t MSG_PAIR = 3;

class Q7RFSwitch : public switch_::Switch,
                   public api::CustomAPIDevice,
                   public PollingComponent,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                         spi::DATA_RATE_1KHZ> {
 protected:
  uint16_t q7rf_device_id_ = 0;
  uint16_t q7rf_resend_interval_ = 60000;

  bool initialized_ = false;
  uint8_t msg_pair_[45];
  uint8_t msg_heat_on_[45];
  uint8_t msg_heat_off_[45];
  bool state_ = false;
  uint8_t pending_msg_ = MSG_NONE;
  unsigned long last_msg_time_ = 0;
  uint8_t msg_errors_ = 0;

 private:
  bool reset_cc();
  void send_cc_cmd(uint8_t cmd);
  void read_cc_register(uint8_t reg, uint8_t* value);
  void read_cc_config_register(uint8_t reg, uint8_t* value);
  void write_cc_register(uint8_t reg, uint8_t* value, size_t length);
  void write_cc_config_register(uint8_t reg, uint8_t value);
  bool send_cc_data(const uint8_t* data, size_t length);

  void encode_bits(uint16_t byte, uint8_t pad_to_length, char** dest);
  void get_msg(uint8_t cmd, uint8_t* msg);

  bool send_msg(uint8_t msg);

 public:
  Q7RFSwitch() : PollingComponent(1000) {}
  void setup() override;
  void write_state(bool state) override;
  void dump_config() override;
  void update() override;

  void set_q7rf_device_id(uint16_t id);
  void set_q7rf_resend_interval(uint16_t interval);
  void on_pairing();
};

}  // namespace q7rf
}  // namespace esphome