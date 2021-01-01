#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace q7rf {

class Q7RF : public switch_::Switch,
             public Component,
             public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                   spi::DATA_RATE_1KHZ> {
 private:
  bool initialized = false;
  bool reset_cc();
  void read_cc_register(uint8_t reg, uint8_t* value);

 public:
  void setup() override;
  void write_state(bool state) override;
  void dump_config() override;
};

}  // namespace q7rf
}  // namespace esphome