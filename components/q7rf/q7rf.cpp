#include "esphome/core/log.h"
#include "q7rf.h"

namespace esphome {
namespace q7rf {

static const char *TAG = "q7rf.switch";

static const char *Q7RF_PREAMBLE_DATA = "111000111";
static const char *Q7RF_ZERO_BIT_DATA = "011";
static const char *Q7RF_ONE_BIT_DATA = "001";
// Silence after each burst. Legacy (upstream) framing uses a 3 tick gap and a
// single 2-burst packet, which Q7RF receivers accept. The original thermostats
// frame each burst with a >= 7 tick low "stop" and repeat it many times; some
// Q8RF receivers only accept that (extended framing).
static const char *Q7RF_LEGACY_GAP_DATA = "000";
static const char *Q7RF_STOP_DATA = "000000000";

// Q8RF zone selector nibble (MSB-first on air). Q7RF only has zone 1.
static const uint8_t Q7RF_ZONE_NIBBLE[] = {0x8, 0x4, 0xC, 0x2};

// FIFO loads per message, 2 bursts each. Extended: 16 bursts (~670 ms).
static const uint8_t Q7RF_LEGACY_TX_PACKETS = 1;
static const uint8_t Q7RF_EXTENDED_TX_PACKETS = 8;
// A packet takes ~84 ms on air; anything far beyond that means the modem is stuck.
static const uint32_t TX_PACKET_TIMEOUT_MS = 250;

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

static const uint8_t MARCSTATE_TX = 0x13;
static const uint8_t MARCSTATE_TX_END = 0x14;
static const uint8_t MARCSTATE_RXTX_SWITCH = 0x15;
static const uint8_t MARCSTATE_TXFIFO_UNDERFLOW = 0x16;

// IDLE -> TX includes synthesizer autocalibration (~720us per datasheet).
static const uint32_t TX_START_TIMEOUT_US = 5000;

static const uint8_t MSG_SEND_ERRORS_RESET_LIMIT = 3;

/* Each symbol takes 220us. Computherm/Delta Q7RF uses PWM modulation.
   Every data bit is encoded as 3 bit inside the buffer.
   001 = 1, 011 = 0, 111000111 = preamble */
static const uint8_t Q7RF_REG_CONFIG[] = {
    CREG_FIFOTHR,  0x00,  // TX FIFO length = 61, others default
    CREG_PKTCTRL1, 0x00,  // Disable RSSI/LQ payload sending, no address check
    CREG_PKTCTRL0, 0x00,  // fixed packet length, no CRC -> modem returns to IDLE when done
    CREG_MDMCFG4,  0xf7,  // baud exponent = 7 (lower 4 bits)
    CREG_MDMCFG3,  0x6B,  // baud mantissa = 107 -> (((256+107) * 2^7)/2^28) * 26 MHz OSC = 4.5 kBaud
    CREG_MDMCFG2,  0x30,  // DC filter on, ASK/OOK modulation, no manchester coding, no preamble/sync
    CREG_MDMCFG1,  0x00,  // no FEC, channel spacing exponent = 0 (last two bit)
    CREG_MDMCFG0,  0xf8,  // channel spacing mantissa = 248 -> 6.000.000 / 2^18 * (256 + 248) * 2^0 ~= 50kHz
    CREG_MCSM0,    0x10,  // autocalibrate synthesizer when switching from IDLE to RX/TX state
    CREG_FOCCFG,   0x00,  // ASK/OOK has no frequency offset compensation
    CREG_FREND0,   0x11   // ASK/OOK PATABLE (power level) settings = up to index 1
};
// PKTLEN (= one FIFO load) and FREQ2/1/0 are written separately in reset_cc().

// 0xc0 = +12dB max power setting
static const uint8_t Q7RF_PA_TABLE[] = {0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

unsigned long elapsed(unsigned long since, unsigned long now) {
  if (since > now) {
    // millis() overflows every ~50 days
    return (ULONG_MAX - since) + now;
  } else {
    return now - since;
  }
}

uint8_t state_to_msg(bool state) { return state ? MSG_HEAT_ON : MSG_HEAT_OFF; }

void encode_bits(uint16_t value, uint8_t pad_to_length, char **dest) {
  // Emits value MSB-first as 3-symbol codes, zero-padded to pad_to_length bits.
  // (The original itoa() version overflowed a 9-byte buffer for 16-bit IDs.)
  uint8_t num_bits = pad_to_length;
  while (num_bits < 16 && (value >> num_bits) != 0)
    num_bits++;

  for (int b = num_bits - 1; b >= 0; b--) {
    const char *symbol = ((value >> b) & 1) ? Q7RF_ONE_BIT_DATA : Q7RF_ZERO_BIT_DATA;
    memcpy(*dest, symbol, strlen(symbol));
    *dest += strlen(symbol);
  }
}

// Builds one FIFO load (2 bursts) into msg and returns its length in bytes.
uint8_t compile_msg(uint16_t device_id, uint8_t zone_nibble, uint8_t cmd, const char *gap, uint8_t *msg) {
  char binary_msg[Q7RF_MSG_MAX_LENGTH * 8];
  char *cursor = binary_msg;

  // Burst: preamble + payload twice + gap
  char *burst_start = cursor;
  memcpy(cursor, Q7RF_PREAMBLE_DATA, strlen(Q7RF_PREAMBLE_DATA));
  cursor += strlen(Q7RF_PREAMBLE_DATA);

  char *payload_start = cursor;
  encode_bits(device_id, 16, &cursor);
  encode_bits(zone_nibble, 4, &cursor);
  encode_bits(cmd, 8, &cursor);

  size_t payload_len = cursor - payload_start;
  memcpy(cursor, payload_start, payload_len);
  cursor += payload_len;

  memcpy(cursor, gap, strlen(gap));
  cursor += strlen(gap);

  // Second burst in the same FIFO load
  size_t burst_len = cursor - burst_start;
  memcpy(cursor, burst_start, burst_len);
  cursor += burst_len;

  // Pad to a whole byte with silence
  while ((cursor - binary_msg) % 8 != 0)
    *cursor++ = '0';

  // Convert msg to bytes
  uint8_t length = (cursor - binary_msg) / 8;
  cursor = binary_msg;
  for (int b = 0; b < length; b++) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
      byte = (byte << 1) | (*cursor++ == '1');
    msg[b] = byte;
  }
  return length;
}

bool Q7RFSwitch::reset_cc() {
  // Chip reset sequence. CS wiggle (CC1101 manual page 45)
  // Toggle CS directly: on ESP-IDF, enable()/disable() acquire/release the SPI
  // bus and releasing an unacquired bus asserts.
  this->cs_->digital_write(true);
  delayMicroseconds(5);
  this->cs_->digital_write(false);
  delayMicroseconds(10);
  this->cs_->digital_write(true);
  delayMicroseconds(41);

  this->send_cc_cmd(CMD_SRES);
  // Give the CC1101 time to finish the reset before talking to it again.
  delay(1);
  ESP_LOGD(TAG, "Issued CC1101 reset sequence.");

  // Read part number and version
  uint8_t partnum;
  this->read_cc_register(REG_PARTNUM, &partnum);

  uint8_t version;
  this->read_cc_register(REG_VERSION, &version);

  ESP_LOGI(TAG, "CC1101 found with partnum: %02x and version: %02x", partnum, version);

  // Setup config registers
  for (int i = 0; i < sizeof(Q7RF_REG_CONFIG); i += 2) {
    if (!this->write_verify_cc_config_register(Q7RF_REG_CONFIG[i], Q7RF_REG_CONFIG[i + 1]))
      return false;
  }
  if (!this->write_verify_cc_config_register(CREG_PKTLEN, this->msg_length_) ||
      !this->write_verify_cc_config_register(CREG_FREQ2, (this->q7rf_freq_word_ >> 16) & 0xff) ||
      !this->write_verify_cc_config_register(CREG_FREQ1, (this->q7rf_freq_word_ >> 8) & 0xff) ||
      !this->write_verify_cc_config_register(CREG_FREQ0, this->q7rf_freq_word_ & 0xff))
    return false;

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

bool Q7RFSwitch::write_verify_cc_config_register(uint8_t reg, uint8_t value) {
  uint8_t verify_value;
  this->write_cc_config_register(reg, value);
  this->read_cc_config_register(reg, &verify_value);
  if (verify_value != value) {
    ESP_LOGE(TAG, "Failed to write CC1101 config register. reg: %02x write: %02x read: %02x", reg, value,
             verify_value);
    return false;
  }
  ESP_LOGV(TAG, "Written CC1101 config register. reg: %02x value: %02x", reg, value);
  return true;
}

bool Q7RFSwitch::send_cc_data(const uint8_t *data, size_t length) {
  uint8_t buffer[length];
  for (int i = 0; i < length; i++) {
    buffer[i] = *data;
    data++;
  }

  this->send_cc_cmd(CMD_SIDLE);
  this->send_cc_cmd(CMD_SFRX);
  this->send_cc_cmd(CMD_SFTX);

  this->enable();
  this->write_byte(CFIFO_TX_BURST);
  this->write_array(buffer, length);
  this->disable();

  this->send_cc_cmd(CMD_STX);

  // Poll until the modem leaves calibration/settling and enters TX.
  uint8_t state;
  uint32_t start = micros();
  do {
    this->read_cc_register(SREG_MARCSTATE, &state);
    state &= 0x1f;
    if (state == MARCSTATE_TX || state == MARCSTATE_TX_END || state == MARCSTATE_RXTX_SWITCH ||
        state == MARCSTATE_TXFIFO_UNDERFLOW)
      break;
    delayMicroseconds(50);
  } while (micros() - start < TX_START_TIMEOUT_US);

  if (state != MARCSTATE_TX && state != MARCSTATE_TX_END && state != MARCSTATE_RXTX_SWITCH) {
    ESP_LOGE(TAG, "CC1101 in invalid state after sending, returning to idle. State: 0x%02x", state);
    this->send_cc_cmd(CMD_SIDLE);
    return false;
  }

  return true;
}

bool Q7RFSwitch::send_msg(uint8_t msg) {
  if (msg == MSG_NONE)
    return false;

  switch (msg) {
    case MSG_HEAT_ON:
      this->tx_msg_ = this->msg_heat_on_;
      this->tx_msg_name_ = "HEAT ON";
      break;
    case MSG_HEAT_OFF:
      this->tx_msg_ = this->msg_heat_off_;
      this->tx_msg_name_ = "HEAT OFF";
      break;
    case MSG_PAIR:
      this->tx_msg_ = this->msg_pair_;
      this->tx_msg_name_ = "PAIR";
      break;
    default:
      return false;
  }

  this->tx_packets_left_ = this->tx_packets_;
  if (!this->tx_next_packet()) {
    this->handle_send_error();
    return false;
  }

  ESP_LOGD(TAG, "Sending message: %s (%u bursts)", this->tx_msg_name_, this->tx_packets_ * 2);
  this->tx_active_ = true;
  this->last_msg_time_ = millis();
  return true;
}

bool Q7RFSwitch::tx_next_packet() {
  if (!this->send_cc_data(this->tx_msg_, this->msg_length_)) {
    this->tx_packets_left_ = 0;
    return false;
  }
  this->tx_packets_left_--;
  this->tx_packet_start_ = millis();
  return true;
}

void Q7RFSwitch::handle_send_error() {
  this->msg_errors_++;
  if (this->msg_errors_ >= MSG_SEND_ERRORS_RESET_LIMIT) {
    ESP_LOGE(TAG, "Multiple message send errors occured, forcing CC1101 reset.");
    this->reset_cc();
    this->msg_errors_ = 0;
  }
}

void Q7RFSwitch::loop() {
  if (!this->tx_active_)
    return;

  uint8_t state;
  this->read_cc_register(SREG_MARCSTATE, &state);
  state &= 0x1f;
  if (state == MARCSTATE_TX || state == MARCSTATE_TX_END) {
    if (elapsed(this->tx_packet_start_, millis()) < TX_PACKET_TIMEOUT_MS)
      return;
    ESP_LOGW(TAG, "CC1101 stuck in TX, forcing idle.");
    this->send_cc_cmd(CMD_SIDLE);
  }

  if (this->tx_packets_left_ > 0) {
    if (!this->tx_next_packet()) {
      this->tx_active_ = false;
      this->handle_send_error();
    }
    return;
  }

  this->tx_active_ = false;
  ESP_LOGD(TAG, "Sent message: %s", this->tx_msg_name_);
}

void Q7RFSwitch::set_state(bool state) {
  this->state_ = state;
  this->publish_state(state);
}

void Q7RFSwitch::setup() {
  // Revert switch to off state
  this->set_state(false);

  // Compile messages
  uint8_t zone_nibble = Q7RF_ZONE_NIBBLE[this->q7rf_zone_ - 1];
  const char *gap = this->q7rf_extended_framing_ ? Q7RF_STOP_DATA : Q7RF_LEGACY_GAP_DATA;
  this->tx_packets_ = this->q7rf_extended_framing_ ? Q7RF_EXTENDED_TX_PACKETS : Q7RF_LEGACY_TX_PACKETS;
  this->msg_length_ = compile_msg(this->q7rf_device_id_, zone_nibble, Q7RF_MSG_CMD_PAIR, gap, this->msg_pair_);
  compile_msg(this->q7rf_device_id_, zone_nibble, Q7RF_MSG_CMD_TURN_ON_HEATING, gap, this->msg_heat_on_);
  compile_msg(this->q7rf_device_id_, zone_nibble, Q7RF_MSG_CMD_TURN_OFF_HEATING, gap, this->msg_heat_off_);

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

void Q7RFSwitch::on_pairing() {
  if (this->initialized_) {
    this->pending_msg_ = MSG_PAIR;
    ESP_LOGI(TAG, "Enqueued pairing.");
  } else {
    ESP_LOGW(TAG, "Pairing ignored: CC1101 not initialized.");
  }
}

void Q7RFSwitch::write_state(bool state) {
  if (this->initialized_) {
    if (state) {
      this->last_turn_on_time_ = millis();
    }

    if (this->state_ != state) {
      this->set_state(state);
      this->pending_msg_ = state_to_msg(state);
    }
  }
}

void Q7RFSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "Q7RF:");
  LOG_PIN("  CC1101 CS Pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  Q7RF Device ID: 0x%04x", this->q7rf_device_id_);
  ESP_LOGCONFIG(TAG, "  Q7RF Zone: %u", this->q7rf_zone_);
  ESP_LOGCONFIG(TAG, "  Q7RF Frequency: %.3f MHz (FREQ=0x%06" PRIx32 ")",
                this->q7rf_freq_word_ * (26.0 / 65536.0), this->q7rf_freq_word_);
  ESP_LOGCONFIG(TAG, "  Q7RF Framing: %s, %u bursts per message", this->q7rf_extended_framing_ ? "extended" : "legacy",
                this->tx_packets_ * 2);
  ESP_LOGCONFIG(TAG, "  Q7RF Resend interval: %d ms", this->q7rf_resend_interval_);
  ESP_LOGCONFIG(TAG, "  Q7RF Turn on watchdog interval: %d ms", this->q7rf_turn_on_watchdog_interval_);

  const struct {
    const char *name;
    const uint8_t *msg;
  } msgs[] = {{"PAIR", this->msg_pair_}, {"HEAT ON", this->msg_heat_on_}, {"HEAT OFF", this->msg_heat_off_}};
  for (const auto &m : msgs) {
    char hex[sizeof(this->msg_pair_) * 2 + 1];
    for (size_t i = 0; i < this->msg_length_; i++)
      snprintf(hex + i * 2, 3, "%02x", m.msg[i]);
    ESP_LOGCONFIG(TAG, "  Encoded %s: %s", m.name, hex);
  }
}

void Q7RFSwitch::update() {
  // Pending messages wait until the current multi-packet transmission is done.
  if (this->initialized_ && !this->tx_active_) {
    if (this->pending_msg_ != MSG_NONE) {
      ESP_LOGD(TAG, "Handling prioritized message.");
      // Send prioritized message
      this->send_msg(this->pending_msg_);
      this->pending_msg_ = MSG_NONE;
    } else {
      unsigned long now = millis();

      if (this->state_ && this->q7rf_turn_on_watchdog_interval_ > 0 &&
          elapsed(this->last_turn_on_time_, now) > this->q7rf_turn_on_watchdog_interval_) {
        ESP_LOGD(TAG, "Turn on watch dog triggered, turning off furnace.");
        this->set_state(false);
        this->send_msg(MSG_HEAT_OFF);
        return;
      }

      // Check if we have to resend current state by now
      if (elapsed(this->last_msg_time_, now) > this->q7rf_resend_interval_) {
        ESP_LOGD(TAG, "Resending last state.");
        uint8_t msg = state_to_msg(this->state_);
        this->send_msg(msg);
      }
    }
  }
}

void Q7RFSwitch::set_q7rf_device_id(uint16_t id) { this->q7rf_device_id_ = id; }

void Q7RFSwitch::set_q7rf_zone(uint8_t zone) { this->q7rf_zone_ = zone; }

void Q7RFSwitch::set_q7rf_extended_framing(bool extended) { this->q7rf_extended_framing_ = extended; }

void Q7RFSwitch::set_q7rf_frequency(float frequency_hz) {
  this->q7rf_freq_word_ = static_cast<uint32_t>(frequency_hz * (65536.0 / 26000000.0) + 0.5);
}

void Q7RFSwitch::set_q7rf_resend_interval(uint32_t interval) { this->q7rf_resend_interval_ = interval; }

void Q7RFSwitch::set_q7rf_turn_on_watchdog_interval(uint32_t interval) {
  this->q7rf_turn_on_watchdog_interval_ = interval;
}

}  // namespace q7rf
}  // namespace esphome
