#include "esphome/core/log.h"
#include "q7rf.h"

namespace esphome {
namespace q7rf {

static const char *TAG = "q7rf.switch";

static const char *Q7RF_PREAMBLE = "111000111";
static const char *Q7RF_ZERO_BIT = "011";
static const char *Q7RF_ONE_BIT = "001";
static const char *Q7RF_GAP_BIT = "000";

static const uint8_t Q7RF_MSG_CMD_PAIR = 0x00;
static const uint8_t Q7RF_MSG_CMD_TURN_ON_HEATING = 0xFF;
static const uint8_t Q7RF_MSG_CMD_TURN_OFF_HEATING = 0x0F;

static const uint8_t EXPECTED_CC1101_PARTNUM = 0;
static const uint8_t EXPECTED_CC1101_VERSION = 0x14;

static const uint8_t CMD_SRES = 0x30;

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

static const uint8_t CREG_PATABLE_BURST_WRITE = 0x7e;
static const uint8_t CREG_PATABLE_BURST_READ = 0xfe;

/* Each symbol takes 220us. Computherm/Delta Q7RF uses PWM modulation.
   Every data bit is encoded as 3 bit inside the buffer.
   001 = 1, 011 = 0, 111000111 = preamble */
static const uint8_t Q7RF_REG_CONFIG[] = {
    CREG_FIFOTHR,  0x00,  // TX FIFO length = 61, others default
    CREG_PKTLEN,   0x3d,  // 61 byte packets
    CREG_PKTCTRL1, 0x00,  // Disable RSSI/LQ payload sending, no address check
    CREG_PKTCTRL0, 0x01,  // variable packet length, no CRC calculation
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

bool Q7RFSwitch::reset_cc() {
  // Chip reset sequence. CS wiggle (CC1101 manual page 45)
  this->disable();
  delayMicroseconds(5);
  this->enable();
  delayMicroseconds(10);
  this->disable();
  delayMicroseconds(41);

  this->send_cc_cmd(CMD_SRES);
  ESP_LOGD(TAG, "Issued CC1101 reset sequence.");

  // Read part number and version
  uint8_t partnum;
  this->read_cc_register(REG_PARTNUM, &partnum);

  uint8_t version;
  this->read_cc_register(REG_VERSION, &version);

  if (partnum != EXPECTED_CC1101_PARTNUM || version != EXPECTED_CC1101_VERSION) {
    ESP_LOGE(TAG, "Invalid CC1101 partnum: %02x and/or version: %02x.", partnum, version);
    return false;
  }

  ESP_LOGD(TAG, "CC1101 found with partnum: %02x and version: %02x", partnum, version);

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
    ESP_LOGD(TAG, "Written CC1101 config register. reg: %02x value: %02x", Q7RF_REG_CONFIG[i], Q7RF_REG_CONFIG[i + 1]);
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
    ESP_LOGD(TAG, "Written CC1101 PATABLE[%d]: %02x", i, Q7RF_PA_TABLE[i]);
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

void Q7RFSwitch::encode_bits(uint16_t byte, uint8_t pad_to_length, char **dest) {
  char binary[9];
  itoa(byte, binary, 2);
  int binary_len = strlen(binary);

  if (binary_len < pad_to_length) {
    for (int p = 0; p < pad_to_length - binary_len; p++) {
      strncpy(*dest, Q7RF_ZERO_BIT, strlen(Q7RF_ZERO_BIT));
      *dest += strlen(Q7RF_ZERO_BIT);
    }
  }

  for (int b = 0; b < binary_len; b++) {
    strncpy(*dest, binary[b] == '1' ? Q7RF_ONE_BIT : Q7RF_ZERO_BIT, strlen(Q7RF_ONE_BIT));
    *dest += strlen(Q7RF_ZERO_BIT);
  }
}

void Q7RFSwitch::get_msg(uint8_t cmd, uint8_t *msg) {
  char binary_msg[360];
  char *cursor = binary_msg;

  // Preamble
  char *preamble_start = cursor;
  strncpy(cursor, Q7RF_PREAMBLE, strlen(Q7RF_PREAMBLE));
  cursor += strlen(Q7RF_PREAMBLE);

  char *payload_start = cursor;

  // Command
  this->encode_bits(this->q7rf_device_id_, 16, &cursor);
  this->encode_bits(8, 4, &cursor);
  this->encode_bits(cmd, 8, &cursor);

  // Repeat the command once more
  strncpy(cursor, payload_start, cursor - payload_start);
  cursor += cursor - payload_start;

  // Add a gap
  strncpy(cursor, Q7RF_GAP_BIT, strlen(Q7RF_GAP_BIT));
  cursor += strlen(Q7RF_GAP_BIT);

  // Repeat the whole burst
  strncpy(cursor, preamble_start, cursor - preamble_start);
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

  // Assemble debug print
  char debug[91];
  cursor = debug;
  cursor_msg = msg;
  for (int b = 0; b < 45; b++) {
    sprintf(cursor, "%x", *cursor_msg);
    cursor += 2;
    cursor_msg++;
  }
  ESP_LOGD(TAG, "Encoded msg: 0x%02x as 0x%s", cmd, debug);
}

void Q7RFSwitch::setup() {
  this->spi_setup();
  if (this->reset_cc()) {
    ESP_LOGI(TAG, "CC1101 reset successful.");
  } else {
    ESP_LOGE(TAG, "Failed to reset CC1101 modem. Check connection.");
    return;
  }

  ESP_LOGD(TAG, "Q7RF Device ID is: 0x%04x", this->q7rf_device_id_);

  this->get_msg(Q7RF_MSG_CMD_PAIR, this->msg_pair_);
  this->get_msg(Q7RF_MSG_CMD_TURN_ON_HEATING, this->msg_heat_on_);
  this->get_msg(Q7RF_MSG_CMD_TURN_OFF_HEATING, this->msg_heat_off_);

  this->initialized_ = true;
}

void Q7RFSwitch::write_state(bool state) {
  if (this->initialized_) {
    // TODO: send state toggle
    this->publish_state(state);
  }
}

void Q7RFSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "Q7RF:");
  ESP_LOGCONFIG(TAG, "  CC1101 CS Pin: %u", this->cs_->get_pin());
  ESP_LOGCONFIG(TAG, "  Q7RF Device ID: 0x%04x", this->q7rf_device_id_);
}

void Q7RFSwitch::set_q7rf_device_id(uint16_t id) { this->q7rf_device_id_ = id; }

}  // namespace q7rf
}  // namespace esphome