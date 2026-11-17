#include "ble_time.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <ctime>

#include "setup_mode.h"
#include "sun.h"
#include "time_status.h"

namespace ble_time {

namespace {

constexpr uint16_t kCurrentTimeService = 0x1805;
constexpr uint16_t kCurrentTimeChar = 0x2A2B;
constexpr uint16_t kLocalTimeInfoChar = 0x2A0F;
constexpr uint16_t kAppearanceClock = 0x0100;
constexpr uint32_t kResyncMs = 6UL * 60 * 60 * 1000;  // let the phone set the time every 6 hours

NimBLEServer* server = nullptr;
uint32_t pairingUntil = 0;
uint32_t lastSync = 0;
bool everSynced = false;
const char* statusText = "";

// Hand-off from the BLE host task to the reader task, and from the reader to loop().
volatile int pendingConn = -1;  // connection to read the time from
TaskHandle_t readerTask = nullptr;
volatile bool resultReady = false;
volatile long resultUtc = 0;
volatile bool resultHasOffset = false;

bool pairingOpen() { return pairingUntil && static_cast<int32_t>(pairingUntil - millis()) > 0; }

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    Serial.printf("ble: connect from %s\n", info.getAddress().toString().c_str());
    NimBLEDevice::startSecurity(info.getConnHandle());  // encrypt (and bond, first time)
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (!info.isEncrypted()) {
      Serial.println("ble: encryption failed");
      server->disconnect(info.getConnHandle());
      return;
    }
    // New pairings are only accepted while pairing mode is open.
    if (!pairingOpen() && !info.isBonded()) {
      server->disconnect(info.getConnHandle());
      return;
    }
    pendingConn = info.getConnHandle();
    if (readerTask) xTaskNotifyGive(readerTask);
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    Serial.printf("ble: disconnected (reason %d)\n", reason);
  }
};

// Current Time characteristic: year (LE u16), month, day, hours, minutes, seconds, weekday,
// 1/256 s, adjust reason. iOS sends local time; Local Time Information gives the offset:
// time zone in 15-minute steps (int8) and DST offset code (0, 2, 4, 8 = +0, 0.5, 1, 2 h).
void readerLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const int conn = pendingConn;
    if (conn < 0) continue;
    NimBLEClient* client = server->getClient(static_cast<uint16_t>(conn));
    bool ok = false;
    if (NimBLERemoteService* cts = client->getService(NimBLEUUID(kCurrentTimeService))) {
      NimBLERemoteCharacteristic* ct = cts->getCharacteristic(NimBLEUUID(kCurrentTimeChar));
      const NimBLEAttValue v = ct ? ct->readValue() : NimBLEAttValue();
      if (v.size() >= 7) {
        const uint8_t* d = v.data();
        const int year = d[0] | d[1] << 8;
        const int64_t local = planetary::daysFromCivil(year, d[2], d[3]) * 86400 + d[4] * 3600 + d[5] * 60 + d[6];
        long offset = 0;
        bool hasOffset = false;
        if (NimBLERemoteCharacteristic* lti = cts->getCharacteristic(NimBLEUUID(kLocalTimeInfoChar))) {
          const NimBLEAttValue o = lti->readValue();
          if (o.size() >= 2 && static_cast<int8_t>(o.data()[0]) != -128) {
            const uint8_t dst = o.data()[1];
            offset = static_cast<int8_t>(o.data()[0]) * 15 * 60 + (dst == 255 ? 0 : dst * 15 * 60);
            hasOffset = true;
          }
        }
        Serial.printf("ble: phone time %04d-%02d-%02d %02d:%02d:%02d offset %ld s%s\n", year, d[2], d[3], d[4],
                      d[5], d[6], offset, hasOffset ? "" : " (unknown)");
        resultUtc = static_cast<long>(local) - offset;
        resultHasOffset = hasOffset;
        resultReady = year >= 2024;
        ok = resultReady;
      }
    }
    if (!ok) Serial.println("ble: phone has no Current Time Service");
    server->disconnect(static_cast<uint16_t>(conn));
    pendingConn = -1;
  }
}

void setAdvertising(bool on) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (on && !adv->isAdvertising()) adv->start();
  if (!on && adv->isAdvertising()) adv->stop();
}

}  // namespace

void begin(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bond, no MITM ("Just Works"), secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new Callbacks());
  server->advertiseOnDisconnect(false);

  // Soliciting the Current Time Service (and Apple's notification service, which watches
  // use) is what makes iOS list the clock in Settings > Bluetooth.
  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  const uint8_t solicit16[] = {3, 0x14, kCurrentTimeService & 0xFF, kCurrentTimeService >> 8};
  ad.addData(solicit16, sizeof solicit16);
  const uint8_t solicitAncs[] = {17,   0x15, 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
                                 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79};  // 7905F431-B5CE-4E99-A40F-4B1E122D00D0
  ad.addData(solicitAncs, sizeof solicitAncs);
  NimBLEAdvertisementData scan;
  scan.setName(deviceName);
  scan.setAppearance(kAppearanceClock);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(scan);

  xTaskCreate(readerLoop, "ble_time", 6144, nullptr, 1, &readerTask);
  Serial.printf("ble: ready, %d bonded phone(s)\n", NimBLEDevice::getNumBonds());
}

void loop() {
  if (!server) return;

  if (resultReady) {
    resultReady = false;
    if (resultHasOffset) {
      setup_mode::applyTime(resultUtc);
    } else {
      // No offset from the phone: read its local time in the clock's own zone.
      struct tm t;
      const time_t asIfUtc = resultUtc;
      gmtime_r(&asIfUtc, &t);
      t.tm_isdst = -1;
      setup_mode::applyTime(static_cast<long>(mktime(&t)));
    }
    lastSync = millis();
    everSynced = true;
    pairingUntil = 0;
    statusText = "Time set by phone";
    Serial.println("ble: clock set from phone");
  }

  if (pairingUntil && !pairingOpen()) {
    pairingUntil = 0;
    if (!everSynced) statusText = "";
  }

  // Advertise while pairing, and for bonded phones when a sync is due.
  const bool due = paired() && (!time_status::valid() || !everSynced || millis() - lastSync > kResyncMs);
  setAdvertising(pendingConn < 0 && (pairingOpen() || due));
}

void startPairing(uint32_t seconds) {
  pairingUntil = millis() + seconds * 1000;
  statusText = "Pair in iPhone Bluetooth";
  Serial.printf("ble: pairing open for %lu s\n", static_cast<unsigned long>(seconds));
}

bool pairing() { return pairingOpen(); }
bool paired() { return NimBLEDevice::getNumBonds() > 0; }
const char* status() { return statusText; }

}  // namespace ble_time
