#include "sts.h"

namespace sts {

namespace {

constexpr uint8_t kPing = 0x01;
constexpr uint8_t kRead = 0x02;
constexpr uint8_t kWrite = 0x03;

}  // namespace

void Bus::begin(HardwareSerial& port, int rxPin, int txPin, uint32_t baud) {
  port_ = &port;
  port_->begin(baud, SERIAL_8N1, rxPin, txPin);
  port_->setTimeout(5);
}

bool Bus::transact(uint8_t id, uint8_t instr, const uint8_t* params, uint8_t n, uint8_t* reply,
                   uint8_t replyLen) {
  if (!port_) return false;
  uint8_t pkt[64];
  const uint8_t len = n + 2;
  pkt[0] = 0xFF;
  pkt[1] = 0xFF;
  pkt[2] = id;
  pkt[3] = len;
  pkt[4] = instr;
  uint8_t sum = id + len + instr;
  for (uint8_t i = 0; i < n; ++i) {
    pkt[5 + i] = params[i];
    sum += params[i];
  }
  pkt[5 + n] = ~sum;
  const size_t pktLen = 6 + n;

  while (port_->available()) port_->read();  // drop anything stale
  port_->write(pkt, pktLen);
  port_->flush();
  if (id == kBroadcast) return true;  // nobody answers a broadcast

  // The bus is one wire: depending on the board we may hear our own packet first. Collect
  // bytes until a status packet from `id` turns up whole, skipping an exact echo.
  uint8_t buf[96];
  size_t got = 0;
  const uint32_t start = micros();
  const size_t want = 6 + replyLen;
  while (micros() - start < timeoutUs_ && got < sizeof buf) {
    while (port_->available() && got < sizeof buf) buf[got++] = port_->read();
    size_t from = (got >= pktLen && memcmp(buf, pkt, pktLen) == 0) ? pktLen : 0;
    for (size_t i = from; i + want <= got; ++i) {
      if (buf[i] != 0xFF || buf[i + 1] != 0xFF || buf[i + 2] != id || buf[i + 3] != replyLen + 2)
        continue;
      uint8_t s = buf[i + 2] + buf[i + 3] + buf[i + 4];
      for (uint8_t k = 0; k < replyLen; ++k) s += buf[i + 5 + k];
      if (static_cast<uint8_t>(~s) != buf[i + 5 + replyLen]) continue;
      lastError_ = buf[i + 4];
      if (reply) memcpy(reply, buf + i + 5, replyLen);
      return true;
    }
  }
  return false;
}

bool Bus::ping(uint8_t id) { return transact(id, kPing, nullptr, 0, nullptr, 0); }

bool Bus::read(uint8_t id, uint8_t addr, uint8_t* out, uint8_t len) {
  const uint8_t p[2] = {addr, len};
  return transact(id, kRead, p, 2, out, len);
}

bool Bus::write(uint8_t id, uint8_t addr, const uint8_t* data, uint8_t len) {
  uint8_t p[40];
  if (len > sizeof p - 1) return false;
  p[0] = addr;
  memcpy(p + 1, data, len);
  return transact(id, kWrite, p, len + 1, nullptr, 0);
}

bool Bus::readByte(uint8_t id, uint8_t addr, uint8_t* v) { return read(id, addr, v, 1); }

bool Bus::readWord(uint8_t id, uint8_t addr, uint16_t* v) {
  uint8_t b[2];
  if (!read(id, addr, b, 2)) return false;
  *v = b[0] | (b[1] << 8);
  return true;
}

bool Bus::writeByte(uint8_t id, uint8_t addr, uint8_t v) { return write(id, addr, &v, 1); }

bool Bus::writeWord(uint8_t id, uint8_t addr, uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
  return write(id, addr, b, 2);
}

bool Bus::moveTo(uint8_t id, int16_t position, uint16_t speed, uint8_t acc) {
  const uint16_t pos = position < 0 ? (0x8000 | static_cast<uint16_t>(-position)) : position;
  // acc, goal position, goal time (unused), goal speed: one write starting at kAcc.
  const uint8_t b[7] = {acc,
                        static_cast<uint8_t>(pos & 0xFF),
                        static_cast<uint8_t>(pos >> 8),
                        0,
                        0,
                        static_cast<uint8_t>(speed & 0xFF),
                        static_cast<uint8_t>(speed >> 8)};
  return write(id, kAcc, b, sizeof b);
}

}  // namespace sts
