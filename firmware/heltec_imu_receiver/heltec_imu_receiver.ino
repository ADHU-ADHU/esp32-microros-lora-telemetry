#include "LoRaWan_APP.h"
#include "Arduino.h"

// ==================== LoRa settings ====================
#define RF_FREQUENCY            433000000 // Hz , LoRa transmitter and receiver frequency 433MHz
#define LORA_BANDWIDTH          0         // 0=125kHz
#define LORA_SPREADING_FACTOR   7         // SF7
#define LORA_CODINGRATE         1         // 1=4/5
#define LORA_PREAMBLE_LENGTH    8
#define LORA_SYMBOL_TIMEOUT     0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON    false

static RadioEvents_t RadioEvents;
bool lora_idle = true;

// ==================== IMU Packet (16 bytes) ====================
struct __attribute__((packed)) ImuPkt {
  uint8_t sync;     // 0xAA
  uint8_t type;     // 0x01
  uint8_t seq;      // increments
  int16_t lin_x, lin_y, lin_z; // *1000
  int16_t ang_x, ang_y, ang_z; // *1000
  uint8_t crc;      // crc8 over bytes [0..14]
};
// CRC-8 poly 0x07 init 0x00
uint8_t crc8_07(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
      else            crc <<= 1;
    }
  }
  return crc;
}

static void print_hex(const uint8_t* p, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    if (p[i] < 16) Serial.print("0");
    Serial.print(p[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Forward declare callback
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

void setup() {
  Serial.begin(115200);
  delay(1500);

 Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

 // Setup RX callback
 RadioEvents.RxDone = OnRxDone;
 Radio.Init(&RadioEvents);
 Radio.SetChannel(RF_FREQUENCY);


 Radio.SetPublicNetwork(true);

 // Configure LoRa RX (matches sender)
 Radio.SetRxConfig(MODEM_LORA,
                   LORA_BANDWIDTH,
                   LORA_SPREADING_FACTOR,
                   LORA_CODINGRATE,
                   0,                     // AFC bandwidth (FSK only)
                   LORA_PREAMBLE_LENGTH,
                   LORA_SYMBOL_TIMEOUT,
                   LORA_FIX_LENGTH_PAYLOAD_ON,
                   0,                     // payload length (0=variable)
                   true,                  // CRC ON
                     0,                    // hop on
                     0,                    // hop period
                     LORA_IQ_INVERSION_ON,
                     true);                // rxContinuous

    Serial.println("Heltec IMU RX ready. Entering RX mode...");
}

void loop() {
  if (lora_idle) {
    lora_idle = false;
    Radio.Rx(0); // continuous RX
  }
  Radio.IrqProcess();
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Serial.printf("\nRX done: size=%u RSSI=%d SNR=%d\n", size, rssi, snr);
  Serial.print("RAW: ");
  print_hex(payload, size);

    if (size != sizeof(ImuPkt)) {
      Serial.printf("Bad size (expected %u)\n", (unsigned)sizeof(ImuPkt));
      lora_idle = true;
      return;
    }

    ImuPkt pkt;
    memcpy(&pkt, payload, sizeof(ImuPkt));

  // Basic header check
  Serial.printf("hdr: sync=0x%02X type=0x%02X seq=%u\n", pkt.sync,
pkt.type, pkt.seq);

    // CRC check
    uint8_t calc = crc8_07((uint8_t*)&pkt, sizeof(ImuPkt) - 1);
    Serial.printf("crc: got=0x%02X calc=0x%02X\n", pkt.crc, calc);

    if (pkt.sync != 0xAA || pkt.type != 0x01 || pkt.crc != calc) {
      Serial.println("Packet received but header/CRC did not match.");
      lora_idle = true;
      return;
    }

    // Decode back to floats
    float lin_x = pkt.lin_x / 1000.0f;
    float lin_y = pkt.lin_y / 1000.0f;
    float lin_z = pkt.lin_z / 1000.0f;

    float ang_x = pkt.ang_x / 1000.0f;
    float ang_y = pkt.ang_y / 1000.0f;
    float ang_z = pkt.ang_z / 1000.0f;

  Serial.printf("IMU OK seq=%u | lin(%.3f %.3f %.3f) ang(%.3f %.3f %.3f)\n",
                pkt.seq, lin_x, lin_y, lin_z, ang_x, ang_y, ang_z);


    lora_idle = true;
}
