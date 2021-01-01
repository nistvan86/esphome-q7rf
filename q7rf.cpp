#include "esphome/core/log.h"
#include "q7rf.h"

namespace esphome {
namespace q7rf {

static const char *TAG = "q7rf.switch";

bool Q7RF::reset_cc() {
  // CS wiggle (CC1101 manual page 45)
  this->disable();
  delayMicroseconds(5);
  this->enable();
  delayMicroseconds(10);
  this->disable();
  delayMicroseconds(41);

  this->enable();
  this->transfer_byte(0x30);  // SRES command
  this->disable();

  uint8_t partnum;
  this->read_cc_register(0xf0, &partnum);  // PARTNUM

  uint8_t version;
  this->read_cc_register(0xf1, &version);  // VERSION

  if (partnum != 0 || version != 20) {
    return false;
  }

  return true;
}

void Q7RF::read_cc_register(uint8_t reg, uint8_t *value) {
  this->enable();
  this->transfer_byte(reg);
  *value = this->transfer_byte(0);
  this->disable();
}

void Q7RF::setup() {
  this->spi_setup();
  if (this->reset_cc()) {
    ESP_LOGI(TAG, "CC1101 reset successful.");
    this->initialized = true;
  } else {
    ESP_LOGE(TAG, "Failed to reset CC1101 modem. Check connection.");
  }
}

void Q7RF::write_state(bool state) {}

void Q7RF::dump_config() { ESP_LOGCONFIG(TAG, "Empty custom switch"); }

}  // namespace q7rf
}  // namespace esphome