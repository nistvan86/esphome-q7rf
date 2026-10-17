#include "esphome/core/log.h"
#include "q7rf.h"

namespace esphome {
namespace q7rf {

static const char *TAG = "q7rf.switch";

static const char *Q7RF_PREAMBLE_DATA = "111000111";
static const char *Q7RF_ZERO_BIT_DATA = "011";
static const char *Q7RF_ONE_BIT_DATA = "001";
static const char *Q7RF_GAP_DATA = "000";

static const uint8_t Q7RF_MSG_CMD_PAIR = 0x00;
static const uint8_t Q7RF_MSG_CMD_TURN_ON_HEATING = 0xFF;
static const uint8_t Q7RF_MSG_CMD_TURN_OFF_HEATING = 0x0F;

static const uint8_t CMD_SRES = 0x30;
static const uint8_t CMD_STX = 0x35;
static const uint8_t CMD_SIDLE = 0x36;
static const uint8_t CMD_SFRX = 0x3a;
static const uint8_t CMD_SFTX = 0x3b;

static const uint8_t REG_PARTNUM = 0xf0;
static const uint8_t REG_VERSION = 0xf1;

static const uint8_t CREG_FIFOTHR = 0x03;
static const uint8_t CREG_PKTLEN = 0x06;
static const uint8_t CREG_PKTCTRL1 = 0x07;
static const uint8_t CREG_PKTCTRL0 = 0x08;
static const uint8_t CREG_FREQ2 = 0x0d;
static const uint8_t CREG_FREQ1 = 0x0e;
static const uint8_t CREG_FREQ0 = 0x0f;
static const uint8_t CREG_MDMCFG4 = 0x10;
static const uint8_t CREG_MDMCFG3 = 0x11;
static const uint8_t CREG_MDMCFG2 = 0x12;
static const uint8_t CREG_MDMCFG1 = 0x13;
static const uint8_t CREG_MDMCFG0 = 0x14;
static const uint8_t CREG_MCSM0 = 0x18;
static const uint8_t CREG_FOCCFG = 0x19;
static const uint8_t CREG_FREND0 = 0x22;

static const uint8_t SREG_MARCSTATE = 0xf5;

static const uint8_t CREG_PATABLE_BURST_WRITE = 0x7e;
static const uint8_t CREG_PATABLE_BURST_READ = 0xfe;
static const uint8_t CFIFO_TX_BURST = 0x7f;

static const uint8_t MARCSTATE_IDLE = 0x01;
static const uint8_t MARCSTATE_TXFIFO_UNDERFLOW = 0x16;

static const uint8_t MSG_SEND_ERRORS_RESET_LIMIT = 3;
// One compiled packet contains two complete RF bursts. Four packet
// transmissions therefore produce eight bursts, similar to the thermostat.
static const uint8_t Q7RF_PACKET_REPETITIONS = 4;
static const uint32_t Q7RF_TX_MIN_DURATION = 60;
static const uint32_t Q7RF_TX_TIMEOUT = 150;
static const uint32_t Q7RF_TX_GAP = 10;

/* Each symbol takes 220us. Computherm/Delta Q7RF uses PWM modulation.
   Every data bit is encoded as 3 bit inside the buffer.
   001 = 1, 011 = 0, 111000111 = preamble */
static const uint8_t Q7RF_REG_CONFIG[] = {
    CREG_FIFOTHR,  0x00,  // TX FIFO length = 61, others default
    CREG_PKTLEN,   0x2d,  // 45 byte fixed-length packets
    CREG_PKTCTRL1, 0x00,  // Disable RSSI/LQ payload sending, no address check
    CREG_PKTCTRL0, 0x00,  // fixed packet length, no CRC calculation
    CREG_FREQ2,    0x21,  // FREQ2, FREQ=0x216544 => 2.188.612 * (26 MHz OSC / 2^16) ~= 868.285 MHz
    CREG_FREQ1,    0x65,  // ^FREQ1
    CREG_FREQ0,    0x44,  // ^FREQ0
    CREG_MDMCFG4,  0xf7,  // baud exponent = 7 (lower 4 bits)
    CREG_MDMCFG3,  0x6B,  // baud mantissa = 107 -> (((256+107) * 2^7)/2^28) * 26 MHz OSC = 4.5 kBaud
    CREG_MDMCFG2,  0x30,  // DC filter on, ASK/OOK modulation, no manchester coding, no preamble/sync
    CREG_MDMCFG1,  0x00,  // no FEC, channel spacing exponent = 0 (last two bit)
    CREG_MDMCFG0,  0xf8,  // channel spacing mantissa = 248 -> 6.000.000 / 2^18 * (256 + 248) * 2^0 ~= 50kHz
    CREG_MCSM0,    0x10,  // autocalibrate synthesizer when switching from IDLE to RX/TX state
    CREG_FOCCFG,   0x00,  // ASK/OOK has no frequency offset compensation
    CREG_FREND0,   0x11   // ASK/OOK PATABLE (power level) settings = up to index 1
};

// 0xc0 = +12dB max power setting
static const uint8_t Q7RF_PA_TABLE[] = {0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

unsigned long elapsed(unsigned long since, unsigned long now) {
  // Unsigned subtraction handles millis() rollover correctly.
  return now - since;
}

uint8_t state_to_msg(bool state) { return state ? MSG_HEAT_ON : MSG_HEAT_OFF; }

void encode_bits(uint16_t byte, uint8_t pad_to_length, char **dest) {
  char binary[9];
  itoa(byte, binary, 2);
  int binary_len = strlen(binary);

  if (binary_len < pad_to_length) {
    for (int p = 0; p < pad_to_length - binary_len; p++) {
      memcpy(*dest, Q7RF_ZERO_BIT_DATA, strlen(Q7RF_ZERO_BIT_DATA));
      *dest += strlen(Q7RF_ZERO_BIT_DATA);
    }
  }

  for (int b = 0; b < binary_len; b++) {
    memcpy(*dest, binary[b] == '1' ? Q7RF_ONE_BIT_DATA : Q7RF_ZERO_BIT_DATA, strlen(Q7RF_ONE_BIT_DATA));
    *dest += strlen(Q7RF_ZERO_BIT_DATA);
  }
}

void compile_msg(uint16_t device_id, uint8_t cmd, uint8_t *msg) {
  char binary_msg[360];
  char *cursor = binary_msg;

  // Preamble
  char *preamble_start = cursor;
  memcpy(cursor, Q7RF_PREAMBLE_DATA, strlen(Q7RF_PREAMBLE_DATA));
  cursor += strlen(Q7RF_PREAMBLE_DATA);

  char *payload_start = cursor;

  // Command
  encode_bits(device_id, 16, &cursor);
  encode_bits(8, 4, &cursor);
  encode_bits(cmd, 8, &cursor);

  // Repeat the command once more
  memcpy(cursor, payload_start, cursor - payload_start);
  cursor += cursor - payload_start;

  // Add a gap
  memcpy(cursor, Q7RF_GAP_DATA, strlen(Q7RF_GAP_DATA));
  cursor += strlen(Q7RF_GAP_DATA);

  // Repeat the whole burst
  memcpy(cursor, preamble_start, cursor - preamble_start);
  cursor += cursor - preamble_start;

  // Convert msg to bytes
  cursor = binary_msg;  // Reset cursor
  uint8_t *cursor_msg = msg;
  char binary_byte[9];
  binary_byte[8] = '\0';
  for (int b = 0; b < 45; b++) {
    strncpy(binary_byte, cursor, 8);
    cursor += 8;
    *cursor_msg = strtoul(binary_byte, 0, 2);
    cursor_msg++;
  }

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  // Assemble debug print
  char debug[91];
  cursor = debug;
  cursor_msg = msg;
  for (int b = 0; b < 45; b++) {
    sprintf(cursor, "%x", *cursor_msg);
    cursor += 2;
    cursor_msg++;
  }
  ESP_LOGV(TAG, "Encoded msg: 0x%02x as 0x%s", cmd, debug);
#endif
}

bool Q7RFSwitch::reset_cc() {
  // Chip reset sequence. CS wiggle (CC1101 manual page 45)
  this->disable();
  delayMicroseconds(5);
  this->enable();
  delayMicroseconds(10);
  this->disable();
  delayMicroseconds(41);

  // ESPHome's SPIDevice does not expose MISO for the datasheet's CHIP_RDYn
  // polling sequence. Keep CS asserted and use conservative delays around
  // SRES so this remains reliable after increasing the SPI clock.
  this->enable();
  delayMicroseconds(1000);
  this->transfer_byte(CMD_SRES);
  delayMicroseconds(1000);
  this->disable();
  ESP_LOGD(TAG, "Issued CC1101 reset sequence.");

  // Read part number and version
  uint8_t partnum;
  this->read_cc_register(REG_PARTNUM, &partnum);

  uint8_t version;
  this->read_cc_register(REG_VERSION, &version);

  ESP_LOGI(TAG, "CC1101 found with partnum: %02x and version: %02x", partnum, version);

  // Setup config registers
  uint8_t verify_value;
  for (int i = 0; i < sizeof(Q7RF_REG_CONFIG); i += 2) {
    this->write_cc_config_register(Q7RF_REG_CONFIG[i], Q7RF_REG_CONFIG[i + 1]);
    this->read_cc_config_register(Q7RF_REG_CONFIG[i], &verify_value);
    if (verify_value != Q7RF_REG_CONFIG[i + 1]) {
      ESP_LOGE(TAG, "Failed to write CC1101 config register. reg: %02x write: %02x read: %02x", Q7RF_REG_CONFIG[i],
               Q7RF_REG_CONFIG[i + 1], verify_value);
      return false;
    }
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    ESP_LOGV(TAG, "Written CC1101 config register. reg: %02x value: %02x", Q7RF_REG_CONFIG[i], Q7RF_REG_CONFIG[i + 1]);
#endif
  }

  // Write PATable
  uint8_t pa_table[sizeof(Q7RF_PA_TABLE)];
  for (int i = 0; i < sizeof(Q7RF_PA_TABLE); i++)
    pa_table[i] = Q7RF_PA_TABLE[i];

  this->enable();
  this->write_byte(CREG_PATABLE_BURST_WRITE);
  this->write_array(pa_table, sizeof(Q7RF_PA_TABLE));
  this->disable();

  this->enable();
  this->write_byte(CREG_PATABLE_BURST_READ);
  this->read_array(pa_table, sizeof(Q7RF_PA_TABLE));
  this->disable();

  for (int i = 0; i < sizeof(Q7RF_PA_TABLE); i++) {
    if (pa_table[i] != Q7RF_PA_TABLE[i]) {
      ESP_LOGE(TAG, "Failed to write CC1101 PATABLE.");
      return false;
    }
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    ESP_LOGV(TAG, "Written CC1101 PATABLE[%d]: %02x", i, Q7RF_PA_TABLE[i]);
#endif
  }

  return true;
}

void Q7RFSwitch::send_cc_cmd(uint8_t cmd) {
  this->enable();
  this->transfer_byte(cmd);
  this->disable();
}

void Q7RFSwitch::read_cc_register(uint8_t reg, uint8_t *value) {
  this->enable();
  this->transfer_byte(reg);
  *value = this->transfer_byte(0);
  this->disable();
}

void Q7RFSwitch::read_cc_config_register(uint8_t reg, uint8_t *value) { this->read_cc_register(reg + 0x80, value); }

void Q7RFSwitch::write_cc_register(uint8_t reg, uint8_t *value, size_t length) {
  this->enable();
  this->transfer_byte(reg);
  this->transfer_array(value, length);
  this->disable();
}

void Q7RFSwitch::write_cc_config_register(uint8_t reg, uint8_t value) {
  uint8_t arr[1] = {value};
  this->write_cc_register(reg, arr, 1);
}

void Q7RFSwitch::start_cc_data(const uint8_t *data, size_t length) {
  this->send_cc_cmd(CMD_SIDLE);
  this->send_cc_cmd(CMD_SFRX);
  this->send_cc_cmd(CMD_SFTX);

  this->enable();
  this->write_byte(CFIFO_TX_BURST);
  this->write_array(data, length);
  this->disable();

  this->send_cc_cmd(CMD_STX);
  this->tx_started_time_ = millis();
  this->tx_in_progress_ = true;
}

bool Q7RFSwitch::start_msg(uint8_t msg) {
  if (msg == MSG_NONE)
    return false;

  const char *text_msg = NULL;
  switch (msg) {
    case MSG_HEAT_ON:
      this->start_cc_data(this->msg_heat_on_, sizeof(this->msg_heat_on_));
      text_msg = "HEAT ON";
      break;
    case MSG_HEAT_OFF:
      this->start_cc_data(this->msg_heat_off_, sizeof(this->msg_heat_off_));
      text_msg = "HEAT OFF";
      break;
    case MSG_PAIR:
      this->start_cc_data(this->msg_pair_, sizeof(this->msg_pair_));
      text_msg = "PAIR";
      break;
    default:
      return false;
  }

  this->active_msg_ = msg;
  ESP_LOGD(TAG, "Started transmission: %s", text_msg);
  return true;
}

void Q7RFSwitch::finish_msg(bool success) {
  uint8_t finished_msg = this->active_msg_;
  this->tx_in_progress_ = false;
  this->active_msg_ = MSG_NONE;
  this->last_tx_finish_time_ = millis();

  if (success) {
    this->last_msg_time_ = millis();
    this->msg_errors_ = 0;
    this->tx_attempts_ = 0;

    // A newer command may have replaced the one that just completed.
    if (this->pending_msg_ == finished_msg && this->pending_msg_repetitions_ > 0 &&
        --this->pending_msg_repetitions_ == 0) {
      this->pending_msg_ = MSG_NONE;
    }
  } else {
    this->msg_errors_++;
    if (this->pending_msg_ == finished_msg) {
      this->tx_attempts_++;
    } else {
      this->tx_attempts_ = 0;
    }
    this->send_cc_cmd(CMD_SIDLE);
    this->send_cc_cmd(CMD_SFTX);

    if (this->msg_errors_ >= MSG_SEND_ERRORS_RESET_LIMIT) {
      ESP_LOGE(TAG, "Multiple message send errors occurred, forcing CC1101 reset.");
      if (!this->reset_cc()) {
        ESP_LOGE(TAG, "Failed to recover CC1101 modem.");
        this->initialized_ = false;
      }
      this->msg_errors_ = 0;
    }

    if (this->tx_attempts_ >= MSG_SEND_ERRORS_RESET_LIMIT && this->pending_msg_ == finished_msg) {
      ESP_LOGE(TAG, "Dropping message after repeated transmission failures.");
      this->pending_msg_ = MSG_NONE;
      this->pending_msg_repetitions_ = 0;
      this->tx_attempts_ = 0;
    }
  }
}

void Q7RFSwitch::enqueue_msg(uint8_t msg) {
  this->pending_msg_ = msg;
  this->pending_msg_repetitions_ = Q7RF_PACKET_REPETITIONS;
  this->tx_attempts_ = 0;
}

void Q7RFSwitch::set_state(bool state) {
  this->state_ = state;
  this->publish_state(state);
}

void Q7RFSwitch::setup() {
  // Revert switch to off state
  this->set_state(false);

  // Compile messages
  compile_msg(this->q7rf_device_id_, Q7RF_MSG_CMD_PAIR, this->msg_pair_);
  compile_msg(this->q7rf_device_id_, Q7RF_MSG_CMD_TURN_ON_HEATING, this->msg_heat_on_);
  compile_msg(this->q7rf_device_id_, Q7RF_MSG_CMD_TURN_OFF_HEATING, this->msg_heat_off_);

  // Register the pairing service
  register_service(&Q7RFSwitch::on_pairing, "q7rf_pair");

  this->spi_setup();
  if (this->reset_cc()) {
    ESP_LOGI(TAG, "CC1101 initialized.");
  } else {
    ESP_LOGE(TAG, "Failed to reset CC1101 modem. Check connection.");
    return;
  }

  this->initialized_ = true;
}

void Q7RFSwitch::loop() {
  if (!this->initialized_)
    return;

  unsigned long now = millis();

  if (this->tx_in_progress_) {
    uint8_t state;
    this->read_cc_register(SREG_MARCSTATE, &state);
    state &= 0x1f;

    if (state == MARCSTATE_TXFIFO_UNDERFLOW) {
      ESP_LOGW(TAG, "CC1101 TX FIFO underflow.");
      this->finish_msg(false);
    } else if (state == MARCSTATE_IDLE) {
      if (elapsed(this->tx_started_time_, now) >= Q7RF_TX_MIN_DURATION) {
        this->finish_msg(true);
      } else {
        ESP_LOGW(TAG, "CC1101 returned to IDLE before a complete packet was transmitted.");
        this->finish_msg(false);
      }
    } else if (elapsed(this->tx_started_time_, now) > Q7RF_TX_TIMEOUT) {
      ESP_LOGW(TAG, "CC1101 transmission timed out in state 0x%02x.", state);
      this->finish_msg(false);
    }
    return;
  }

  if (this->pending_msg_ != MSG_NONE && elapsed(this->last_tx_finish_time_, now) >= Q7RF_TX_GAP) {
    this->start_msg(this->pending_msg_);
  }
}

void Q7RFSwitch::on_pairing() {
  if (this->initialized_) {
    this->enqueue_msg(MSG_PAIR);
    ESP_LOGI(TAG, "Enqueued pairing.");
  }
}

void Q7RFSwitch::write_state(bool state) {
  if (this->initialized_) {
    if (state) {
      this->last_turn_on_time_ = millis();
    }

    if (this->state_ != state) {
      this->set_state(state);
      this->enqueue_msg(state_to_msg(state));
    }
  }
}

void Q7RFSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "Q7RF:");
  LOG_PIN("  CC1101 CS Pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  Q7RF Device ID: 0x%04x", this->q7rf_device_id_);
  ESP_LOGCONFIG(TAG, "  Q7RF Resend interval: %d ms", this->q7rf_resend_interval_);
  ESP_LOGCONFIG(TAG, "  Q7RF Turn on watchdog interval: %d ms", this->q7rf_turn_on_watchdog_interval_);
}

void Q7RFSwitch::update() {
  if (!this->initialized_)
    return;

  unsigned long now = millis();

  if (this->state_ && this->q7rf_turn_on_watchdog_interval_ > 0 &&
      elapsed(this->last_turn_on_time_, now) > this->q7rf_turn_on_watchdog_interval_) {
    ESP_LOGD(TAG, "Turn on watch dog triggered, turning off furnace.");
    this->set_state(false);
    this->enqueue_msg(MSG_HEAT_OFF);
    return;
  }

  // Do not replace a command whose repetitions are still being transmitted.
  if (this->pending_msg_ == MSG_NONE && !this->tx_in_progress_ &&
      elapsed(this->last_msg_time_, now) > this->q7rf_resend_interval_) {
    ESP_LOGD(TAG, "Resending last state.");
    this->enqueue_msg(state_to_msg(this->state_));
  }
}

void Q7RFSwitch::set_q7rf_device_id(uint16_t id) { this->q7rf_device_id_ = id; }

void Q7RFSwitch::set_q7rf_resend_interval(uint32_t interval) { this->q7rf_resend_interval_ = interval; }

void Q7RFSwitch::set_q7rf_turn_on_watchdog_interval(uint32_t interval) {
  this->q7rf_turn_on_watchdog_interval_ = interval;
}

}  // namespace q7rf
}  // namespace esphome
