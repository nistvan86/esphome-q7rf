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

// Largest TX FIFO load (extended framing): 2 bursts with stop gaps (see compile_msg).
static const uint8_t Q7RF_MSG_MAX_LENGTH = 47;

class Q7RFSwitch : public switch_::Switch,
                   public api::CustomAPIDevice,
                   public PollingComponent,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                         spi::DATA_RATE_1MHZ> {
 protected:
  uint16_t q7rf_device_id_ = 0;
  uint8_t q7rf_zone_ = 1;
  bool q7rf_extended_framing_ = false;
  uint32_t q7rf_freq_word_ = 0x216544;  // 868.285 MHz @ 26 MHz XOSC
  uint32_t q7rf_resend_interval_ = 60000;
  uint32_t q7rf_turn_on_watchdog_interval_ = 0;

  bool initialized_ = false;
  uint8_t msg_length_ = 0;  // bytes per FIFO load, set by setup()
  uint8_t tx_packets_ = 1;  // FIFO loads per message
  uint8_t msg_pair_[Q7RF_MSG_MAX_LENGTH];
  uint8_t msg_heat_on_[Q7RF_MSG_MAX_LENGTH];
  uint8_t msg_heat_off_[Q7RF_MSG_MAX_LENGTH];
  bool state_ = false;
  uint8_t pending_msg_ = MSG_NONE;
  unsigned long last_msg_time_ = 0;
  unsigned long last_turn_on_time_ = 0;
  uint8_t msg_errors_ = 0;

  // Non-blocking repetition of the current message (see loop()).
  bool tx_active_ = false;
  const uint8_t *tx_msg_ = nullptr;
  const char *tx_msg_name_ = nullptr;
  uint8_t tx_packets_left_ = 0;
  unsigned long tx_packet_start_ = 0;

 private:
  bool reset_cc();
  void send_cc_cmd(uint8_t cmd);
  void read_cc_register(uint8_t reg, uint8_t *value);
  void read_cc_config_register(uint8_t reg, uint8_t *value);
  void write_cc_register(uint8_t reg, uint8_t *value, size_t length);
  void write_cc_config_register(uint8_t reg, uint8_t value);
  bool write_verify_cc_config_register(uint8_t reg, uint8_t value);
  bool send_cc_data(const uint8_t *data, size_t length);
  bool tx_next_packet();
  void handle_send_error();

  bool send_msg(uint8_t msg);
  void set_state(bool state);

 public:
  Q7RFSwitch() : PollingComponent(1000) {}
  void setup() override;
  void loop() override;
  void write_state(bool state) override;
  void dump_config() override;
  void update() override;

  void set_q7rf_device_id(uint16_t id);
  void set_q7rf_zone(uint8_t zone);
  void set_q7rf_extended_framing(bool extended);
  void set_q7rf_frequency(float frequency_hz);
  void set_q7rf_resend_interval(uint32_t interval);
  void set_q7rf_turn_on_watchdog_interval(uint32_t interval);
  void on_pairing();
};

}  // namespace q7rf
}  // namespace esphome
