// Feetech STS serial bus servos (STS3215 and friends): the half-duplex packet protocol.
//
// Instruction packet: FF FF id len instr params... checksum
// Status packet:      FF FF id len error params... checksum
// len counts the bytes after itself; checksum = ~(id + len + instr/error + params).
// Multi-byte registers are little-endian. Positions are 0-4095 per turn.
#pragma once

#include <Arduino.h>

namespace sts {

// Register addresses (STS memory table)
enum Reg : uint8_t {
  kModel = 3,           // 2 bytes
  kId = 5,
  kBaud = 6,
  kMinAngle = 9,        // 2 bytes
  kMaxAngle = 11,       // 2 bytes
  kMaxTemp = 13,
  kMaxVoltage = 14,     // 0.1 V
  kMinVoltage = 15,     // 0.1 V
  kMode = 33,           // 0 position, 1 wheel (speed), 3 step
  kTorqueEnable = 40,
  kAcc = 41,
  kGoalPosition = 42,   // 2 bytes
  kGoalSpeed = 46,      // 2 bytes, steps per second
  kLock = 55,           // 0 lets EEPROM registers be written
  kPosition = 56,       // 2 bytes
  kSpeed = 58,          // 2 bytes, sign in bit 15
  kLoad = 60,           // 2 bytes, 0.1 %, sign in bit 10
  kVoltage = 62,        // 0.1 V
  kTemperature = 63,    // degrees C
  kMoving = 66,
  kCurrent = 69,        // 2 bytes, about 6.5 mA per unit
};

constexpr uint8_t kBroadcast = 0xFE;
constexpr int kStepsPerTurn = 4096;

class Bus {
 public:
  void begin(HardwareSerial& port, int rxPin, int txPin, uint32_t baud = 1000000);

  bool ping(uint8_t id);
  bool read(uint8_t id, uint8_t addr, uint8_t* out, uint8_t len);
  bool write(uint8_t id, uint8_t addr, const uint8_t* data, uint8_t len);

  bool readByte(uint8_t id, uint8_t addr, uint8_t* v);
  bool readWord(uint8_t id, uint8_t addr, uint16_t* v);
  bool writeByte(uint8_t id, uint8_t addr, uint8_t v);
  bool writeWord(uint8_t id, uint8_t addr, uint16_t v);

  // Move to `position` (0-4095) at `speed` steps/s with acceleration `acc` (units of 100 steps/s^2).
  bool moveTo(uint8_t id, int16_t position, uint16_t speed, uint8_t acc = 50);

  // How long to wait for a reply. Servos answer within ~1 ms, except while saving EEPROM.
  void setTimeoutUs(uint32_t us) { timeoutUs_ = us; }

  uint8_t lastError() const { return lastError_; }  // the servo's error byte from its last reply

 private:
  bool transact(uint8_t id, uint8_t instr, const uint8_t* params, uint8_t n, uint8_t* reply,
                uint8_t replyLen);
  HardwareSerial* port_ = nullptr;
  uint8_t lastError_ = 0;
  uint32_t timeoutUs_ = 3000;
};

// Signed values: these registers keep the sign in a high bit instead of two's complement.
inline int signedSpeed(uint16_t raw) { return (raw & 0x8000) ? -int(raw & 0x7FFF) : int(raw); }
inline int signedLoad(uint16_t raw) { return (raw & 0x0400) ? -int(raw & 0x03FF) : int(raw); }

}  // namespace sts
