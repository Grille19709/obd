// ble_data.h - everything that arrives over Bluetooth LE, all in background tasks:
//   - OBD values from a BLE ELM327 adapter (ELMduino on top of a GATT serial link)
//   - Victron SmartSolar "Instant Readout" (encrypted advertisements)
//   - LiFePO4 battery with JBD BMS (GATT client, polled every 2 s)
//   - optional: all values as notifications for the phone page
// The UI only reads val[] / valid(); it never blocks on the radio.

#ifndef BLE_DATA_H
#define BLE_DATA_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <mbedtls/aes.h>
#include "ELMduino.h"
#include "config.h"

// The first OBD_COUNT values come from the car; their order is part of the phone packet.
enum Val { V_RPM, V_SPEED, V_COOLANT, V_CARBAT, V_BOOST, V_RAIL, V_MAF, V_LOAD,
           V_PV_W, V_PV_A, V_PV_V, V_PV_KWH,
           V_BAT_SOC, V_BAT_A, V_BAT_AH, V_BAT_TEMP, V_BAT_V, V_COUNT };
const int OBD_COUNT = V_PV_W;
const float PHONE_SCALE[OBD_COUNT] = {1, 1, 1, 100, 100, 1, 10, 1};

// values 0..5 are shared with the phone page
enum ObdStatus : uint8_t { ST_START, ST_CONNECTING, ST_NO_BT, ST_ELM_SILENT, ST_CONNECTED, ST_LOST, ST_SEARCHING };

float         val[V_COUNT];
unsigned long readAt[V_COUNT];          // 0 = never read
volatile uint8_t solarState = 0xFF;     // Victron "device state", 0xFF = unknown
volatile uint8_t solarError = 0;
volatile uint8_t batFets    = 0;        // BMS MOSFETs: bit 0 charge, bit 1 discharge
volatile ObdStatus obdStatus = ST_START;
volatile bool obdConnected = false;
volatile bool bmsConnected = false;

bool valid(int v) {
  return readAt[v] && millis() - readAt[v] < (v < OBD_COUNT ? OBD_STALE_MS : SOLAR_STALE_MS);
}

const char* obdStatusText() {
  switch (obdStatus) {
    case ST_SEARCHING:  return "no adapter found";
    case ST_CONNECTING: return "connecting";
    case ST_NO_BT:      return "connect failed";
    case ST_ELM_SILENT: return "adapter silent";
    case ST_CONNECTED:  return "connected";
    case ST_LOST:       return "ignition off?";
    default:            return "starting";
  }
}

const char* solarStateText(uint8_t z) {
  switch (z) {
    case 0:    return "Off";
    case 2:    return "Fault";
    case 3:    return "Bulk";
    case 4:    return "Absorption";
    case 5:    return "Float";
    case 6:    return "Storage";
    case 7:    return "Equalize";
    case 245:  return "Starting";
    case 252:  return "External";
    case 0xFF: return "--";
    default:   return "?";
  }
}

// ---------------- Scan: Victron data, OBD adapter discovery, device list ----------------
struct SeenDevice { char addr[18]; char name[17]; int rssi; unsigned long at; };
const int SEEN_MAX = 6;
SeenDevice seen[SEEN_MAX];              // named BLE devices in range (for the "More" screen)

char    obdAddr[18] = "";               // filled by the scan once the adapter is seen
uint8_t obdAddrType = 0;
volatile bool obdFound = false;

uint8_t victronKey[16];
bool    victronOn = false;

bool parseVictronKey() {
  if (strlen(VICTRON_KEY) != 32) return false;
  for (int i = 0; i < 16; i++) {
    char hex[3] = {VICTRON_KEY[2 * i], VICTRON_KEY[2 * i + 1], 0};
    victronKey[i] = strtoul(hex, nullptr, 16);
  }
  return true;
}

class ScanHandler : public BLEAdvertisedDeviceCallbacks {
  bool firstSeen = false, wrongKey = false;

  void remember(const char* addr, const char* name, int rssi) {
    int slot = 0;
    for (int i = 0; i < SEEN_MAX; i++) {
      if (!strcmp(seen[i].addr, addr)) { slot = i; break; }
      if (seen[i].at < seen[slot].at) slot = i;      // otherwise replace the oldest entry
    }
    if (strcmp(seen[slot].addr, addr)) {
      Serial.printf("[SCAN] %s  \"%s\"  %d dBm\n", addr, name, rssi);
      strlcpy(seen[slot].addr, addr, sizeof(seen[slot].addr));
    }
    strlcpy(seen[slot].name, name, sizeof(seen[slot].name));
    seen[slot].rssi = rssi;
    seen[slot].at = millis();
  }

  void checkObd(BLEAdvertisedDevice& dev, const String& addr) {
    if (obdFound) return;
    String name = dev.haveName() ? dev.getName() : String("");
    name.toLowerCase();
    bool hit = strlen(OBD_MAC) && addr.equalsIgnoreCase(OBD_MAC);
    if (!hit && name.length() && !name.startsWith("esp32"))
      for (auto n : OBD_NAMES) if (name.indexOf(n) >= 0) { hit = true; break; }
    if (!hit) return;
    strlcpy(obdAddr, addr.c_str(), sizeof(obdAddr));
    obdAddrType = dev.getAddressType();
    obdFound = true;
    Serial.printf("[OBD] adapter found: %s \"%s\"\n", obdAddr, dev.getName().c_str());
  }

  void victron(BLEAdvertisedDevice& dev) {
    String m = dev.getManufacturerData();
    const uint8_t* d = (const uint8_t*)m.c_str();
    size_t n = m.length();
    if (!firstSeen) {   // print once raw, helps when debugging
      firstSeen = true;
      Serial.print("[SOLAR] first packet:");
      for (size_t i = 0; i < n; i++) Serial.printf(" %02x", d[i]);
      Serial.println();
    }
    // Victron (0x02E1), product advertisement 0x10, record 0x01 = solar charger
    if (n < 22 || d[0] != 0xE1 || d[1] != 0x02 || d[2] != 0x10 || d[6] != 0x01) return;
    if (d[9] != victronKey[0]) {
      if (!wrongKey) Serial.println("[SOLAR] key does not match");
      wrongKey = true;
      return;
    }
    // AES-128-CTR, counter starts with the 16 bit packet number
    uint8_t counter[16] = {d[7], d[8]}, block[16], plain[16];
    size_t offset = 0, len = min<size_t>(n - 10, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, victronKey, 128);
    mbedtls_aes_crypt_ctr(&aes, len, &offset, counter, block, d + 10, plain);
    mbedtls_aes_free(&aes);

    int16_t  volt  = plain[2] | plain[3] << 8;   // 0.01 V
    int16_t  amp   = plain[4] | plain[5] << 8;   // 0.1 A
    uint16_t yield = plain[6] | plain[7] << 8;   // 0.01 kWh
    uint16_t power = plain[8] | plain[9] << 8;   // W
    unsigned long now = millis();
    solarState = plain[0];
    solarError = plain[1];
    if (volt  != 0x7FFF) { val[V_PV_V]   = volt / 100.0;  readAt[V_PV_V]   = now; }
    if (amp   != 0x7FFF) { val[V_PV_A]   = amp / 10.0;    readAt[V_PV_A]   = now; }
    if (yield != 0xFFFF) { val[V_PV_KWH] = yield / 100.0; readAt[V_PV_KWH] = now; }
    if (power != 0xFFFF) { val[V_PV_W]   = power;         readAt[V_PV_W]   = now; }
  }

  void onResult(BLEAdvertisedDevice dev) override {
    String addr = dev.getAddress().toString();
    if (victronOn && dev.haveManufacturerData() && addr.equalsIgnoreCase(VICTRON_MAC)) { victron(dev); return; }
    bool isObdMac = strlen(OBD_MAC) && addr.equalsIgnoreCase(OBD_MAC);
    if (dev.haveName() || isObdMac)
      remember(addr.c_str(), dev.haveName() ? dev.getName().c_str() : "(no name)", dev.getRSSI());
    checkObd(dev, addr);
  }
};

void startScan() { BLEDevice::getScan()->start(0, nullptr, false); }   // runs until stopped

// Connecting works more reliably while the scan pauses; only one connect at a time.
SemaphoreHandle_t radioLock;

bool bleConnect(BLEClient* client, const char* mac, uint8_t addrType) {
  xSemaphoreTake(radioLock, portMAX_DELAY);
  BLEDevice::getScan()->stop();
  bool ok = client->connect(BLEAddress(mac), addrType, 10000);
  startScan();
  xSemaphoreGive(radioLock);
  return ok;
}

// ---------------- LiFePO4 battery (JBD BMS) ----------------
// JBD: service FF00, answers as notifications on FF01, commands to FF02
// "basic info": DD A5 03 00 FF FD 77 -> answer DD 03 <status> <length> <data> <checksum 2> 77
uint8_t bmsCommand[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
uint8_t bmsBuffer[64];
size_t  bmsLength = 0;

uint16_t be16(const uint8_t* p) { return p[0] << 8 | p[1]; }

void bmsParse(const uint8_t* d, size_t n) {   // d = payload of the answer to 0x03
  if (n < 23) return;
  unsigned long now = millis();
  val[V_BAT_V]   = be16(d) / 100.0;
  val[V_BAT_A]   = (int16_t)be16(d + 2) / 100.0;
  val[V_BAT_AH]  = be16(d + 4) / 100.0;
  val[V_BAT_SOC] = d[19];
  batFets = d[20];
  readAt[V_BAT_V] = readAt[V_BAT_A] = readAt[V_BAT_AH] = readAt[V_BAT_SOC] = now;
  if (d[22] > 0 && n >= 25) {   // first temperature probe, 0.1 K
    val[V_BAT_TEMP] = (be16(d + 23) - 2731) / 10.0;
    readAt[V_BAT_TEMP] = now;
  }
}

// answers arrive in 20 byte pieces: collect until complete
void bmsNotify(BLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  if (bmsLength == 0 && (n == 0 || d[0] != 0xDD)) return;   // missed the start
  if (bmsLength + n > sizeof(bmsBuffer)) { bmsLength = 0; return; }
  memcpy(bmsBuffer + bmsLength, d, n);
  bmsLength += n;
  if (bmsLength < 4 || bmsLength < (size_t)4 + bmsBuffer[3] + 3) return;
  if (bmsBuffer[1] == 0x03 && bmsBuffer[2] == 0x00) bmsParse(bmsBuffer + 4, bmsBuffer[3]);
  bmsLength = 0;
}

void bmsTask(void*) {
  BLEClient* client = BLEDevice::createClient();
  BLERemoteCharacteristic* command = nullptr;
  for (;;) {
    if (!client->isConnected()) {
      command = nullptr;
      bmsConnected = false;
      if (!bleConnect(client, AKKU_MAC, 0 /* public address */)) { Serial.println("[BMS] not reachable"); delay(20000); continue; }
      BLERemoteService* service = client->getService(BLEUUID((uint16_t)0xFF00));
      BLERemoteCharacteristic* answer = service ? service->getCharacteristic(BLEUUID((uint16_t)0xFF01)) : nullptr;
      command = service ? service->getCharacteristic(BLEUUID((uint16_t)0xFF02)) : nullptr;
      if (!answer || !command) {
        Serial.println("[BMS] connected, but no JBD service FF00/FF01/FF02");
        client->disconnect();
        delay(60000);
        continue;
      }
      answer->registerForNotify(bmsNotify);
      bmsConnected = true;
      Serial.println("[BMS] connected");
    }
    bmsLength = 0;
    command->writeValue(bmsCommand, sizeof(bmsCommand), false);
    delay(2000);
  }
}

// ---------------- OBD: ELM327 over a BLE serial link ----------------
// BLE adapters expose a "serial port" as two GATT characteristics: one notifies the
// answers, one takes the commands. This makes them look like a Stream for ELMduino.
class BleStream : public Stream {
  static const uint16_t SIZE = 1024;           // power of two
  uint8_t rx[SIZE];
  volatile uint16_t head = 0, tail = 0;
  uint8_t out[20];                             // fits the default BLE packet
  size_t outLen = 0;

 public:
  BLERemoteCharacteristic* tx = nullptr;
  bool withResponse = false;
  using Print::write;

  int available() override { return (uint16_t)(head - tail) & (SIZE - 1); }
  int peek() override { return head == tail ? -1 : rx[tail]; }
  int read() override {
    if (head == tail) return -1;
    uint8_t c = rx[tail];
    tail = (tail + 1) & (SIZE - 1);
    return c;
  }
  void flush() override { send(); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  // collect until the command is complete: adapters want it in one packet
  size_t write(const uint8_t* d, size_t n) override {
    for (size_t i = 0; i < n; i++) {
      out[outLen++] = d[i];
      if (d[i] == '\r' || outLen == sizeof(out)) send();
    }
    return n;
  }
  void send() {
    if (tx && outLen) tx->writeValue(out, outLen, withResponse);
    outLen = 0;
  }
  void feed(const uint8_t* d, size_t n) {       // called from the BLE task
    for (size_t i = 0; i < n; i++) {
      uint16_t next = (head + 1) & (SIZE - 1);
      if (next == tail) return;                 // full: drop the rest
      rx[head] = d[i];
      head = next;
    }
  }
  void reset() { head = 0; tail = 0; outLen = 0; }
};

BleStream obdLink;
ELM327 myELM327;

void obdNotify(BLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { obdLink.feed(d, n); }

// Find the serial characteristics: known services first (FFF0, FFE0, 18F0), then any
// service that has one notifying and one writable characteristic.
bool obdFindChannels(BLEClient* client, BLERemoteCharacteristic*& answer, BLERemoteCharacteristic*& command) {
  auto pick = [&](BLERemoteService* s) {
    answer = command = nullptr;
    BLERemoteCharacteristic* writable = nullptr;
    for (auto& c : *s->getCharacteristics()) {
      BLERemoteCharacteristic* ch = c.second;
      bool n = ch->canNotify() || ch->canIndicate();
      bool w = ch->canWrite() || ch->canWriteNoResponse();
      if (n && !answer) answer = ch;
      if (w && !n && !command) command = ch;     // prefer a pure command characteristic
      if (w && !writable) writable = ch;
    }
    if (!command) command = writable;
    return answer && command;
  };

  auto* services = client->getServices();
  Serial.println("[OBD] services:");
  for (auto& s : *services) Serial.printf("  %s\n", s.first.c_str());

  for (uint16_t known : {0xFFF0, 0xFFE0, 0x18F0}) {
    BLEUUID uuid(known);
    for (auto& s : *services)
      if (s.second->getUUID().equals(uuid) && pick(s.second)) return true;
  }
  for (auto& s : *services) {
    BLEUUID u = s.second->getUUID();
    if (u.equals(BLEUUID((uint16_t)0x1800)) || u.equals(BLEUUID((uint16_t)0x1801))) continue;
    if (pick(s.second)) return true;
  }
  return false;
}

// waits for the answer of a non-blocking ELMduino query. The wait is longer than ELMduino's
// own timeout: giving up earlier would leave its state machine waiting, and the next query
// would then read this answer (wrong values in the wrong field).
const uint32_t ELM_TIMEOUT_MS = 2000;
template <typename F>
bool ask(F query, double& result, uint32_t timeout = ELM_TIMEOUT_MS + 1000) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    double r = query();
    if (myELM327.nb_rx_state == ELM_SUCCESS) { result = r; return true; }
    if (myELM327.nb_rx_state != ELM_GETTING_MSG) return false;
    delay(5);
  }
  return false;
}

int carBatPidFails = 0;   // ECU answered other PIDs but not PID 42

bool obdRead(Val v, float& value) {
  double r = 0;
  bool ok = false;
  switch (v) {
    case V_RPM:     ok = ask([] { return (double)myELM327.rpm(); }, r); break;
    case V_SPEED:   ok = ask([] { return (double)myELM327.kph(); }, r); break;
    case V_COOLANT: ok = ask([] { return (double)myELM327.engineCoolantTemp(); }, r); break;
    case V_CARBAT:  // PID 42 = voltage seen by the engine ECU. "AT RV" is measured by the adapter
                    // itself and answers even with the ignition off, so only used as a fallback.
                    if (carBatPidFails < 3) {
                      ok = ask([] { return (double)myELM327.ctrlModVoltage(); }, r);
                      if (ok) carBatPidFails = 0;
                      else if (valid(V_RPM)) carBatPidFails++;   // ECU talks, but not PID 42
                    } else {
                      ok = ask([] { return (double)myELM327.batteryVoltage(); }, r);
                    }
                    break;
    case V_BOOST:   ok = ask([] { return (double)myELM327.manifoldPressure(); }, r);
                    r = (r - AIR_PRESSURE_KPA) / 100.0;   // absolute kPa -> boost in bar
                    break;
    case V_RAIL:    ok = ask([] { return (double)myELM327.fuelRailGuagePressure(); }, r);
                    r /= 100.0;                           // kPa -> bar
                    break;
    case V_MAF:     ok = ask([] { return (double)myELM327.mafRate(); }, r); break;
    case V_LOAD:    ok = ask([] { return (double)myELM327.engineLoad(); }, r); break;
    default:        break;
  }
  value = r;
  return ok;
}

void obdTask(void*) {
  BLEClient* client = BLEDevice::createClient();
  int query = 0, errors = 0;
  for (;;) {
    if (!obdConnected || !client->isConnected()) {
      if (obdConnected) {                       // link dropped (adapter unplugged, switched off)
        Serial.println("[OBD] link lost");
        obdConnected = false;
        obdStatus = ST_LOST;
        obdFound = false;                       // reconnect once it advertises again
        for (int i = 0; i < OBD_COUNT; i++) readAt[i] = 0;
      }
      if (!obdFound) {                          // scan has not seen an adapter (yet)
        if (millis() > 15000 && obdStatus == ST_START) obdStatus = ST_SEARCHING;
        delay(200);
        continue;
      }
      Serial.printf("[OBD] connecting to %s...\n", obdAddr);
      obdStatus = ST_CONNECTING;
      obdLink.tx = nullptr;
      obdLink.reset();
      if (client->isConnected()) client->disconnect();   // leftover half-open link
      if (!bleConnect(client, obdAddr, obdAddrType)) {
        Serial.println("[OBD] connect failed");
        obdStatus = ST_NO_BT;
        obdFound = false;                       // try again as soon as the scan sees it
        delay(2000);
        continue;
      }
      BLERemoteCharacteristic *answer = nullptr, *command = nullptr;
      if (!obdFindChannels(client, answer, command)) {
        Serial.println("[OBD] no serial service on this device");
        obdStatus = ST_ELM_SILENT;
        client->disconnect();
        delay(OBD_RETRY_MS);
        continue;
      }
      Serial.printf("[OBD] answers on %s, commands to %s\n",
                    answer->getUUID().toString().c_str(), command->getUUID().toString().c_str());
      answer->registerForNotify(obdNotify, answer->canNotify());
      obdLink.withResponse = !command->canWriteNoResponse();
      obdLink.tx = command;
      free(myELM327.payload);                   // begin() allocates it again
      myELM327 = ELM327();                      // fresh state machine, nothing left from the old link
      if (!myELM327.begin(obdLink, false, ELM_TIMEOUT_MS)) {
        Serial.println("[OBD] ELM327 does not answer");
        obdStatus = ST_ELM_SILENT;
        client->disconnect();
        delay(OBD_RETRY_MS);
        continue;
      }
      Serial.println("[OBD] connected");
      obdConnected = true;
      obdStatus = ST_CONNECTED;
      errors = 0;
      query = 0;
    }

    // one value per round, in turn
    Val v = (Val)query;
    float value;
    if (obdRead(v, value)) {
      val[v] = value;
      readAt[v] = millis();
      errors = 0;
    } else {
      readAt[v] = 0;
      if (++errors >= 8) {                      // adapter no longer answers (e.g. ignition off)
        Serial.println("[OBD] no answers, disconnecting");
        client->disconnect();
        obdConnected = false;
        obdStatus = ST_LOST;
        for (int i = 0; i < OBD_COUNT; i++) readAt[i] = 0;
        delay(OBD_RETRY_MS);                    // ignition off: no need to hammer the car
        obdFound = false;                       // then reconnect when it is seen again
      }
    }
    query = (query + 1) % OBD_COUNT;
  }
}

// ---------------- Phone page (BLE notifications) ----------------
// Packets (little endian), first byte = type:
//   type 0, 18 bytes: 8 x int16 OBD values (enum Val, times PHONE_SCALE; -32768 = invalid), uint8 status
//   type 1, 11 bytes: uint8 charge state (0xFF = no data), uint8 error, int16 battery 0.01 V,
//                     int16 charge current 0.1 A, uint16 yield today 0.01 kWh, uint16 PV power W
//   type 2, 11 bytes: uint8 SOC % (0xFF = no data), uint16 voltage 0.01 V, int16 current 0.01 A,
//                     uint16 remaining 0.1 Ah, int16 temperature 0.1 C, uint8 MOSFETs
#if PHONE_BLE
BLECharacteristic* phoneValues;

class PhoneCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer* s) override { s->startAdvertising(); }   // visible again
};

void putInt16(uint8_t* p, int16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }

void phoneSend() {
  uint8_t obd[2 + 2 * OBD_COUNT] = {0};   // type 0
  for (int i = 0; i < OBD_COUNT; i++) {
    int16_t v = INT16_MIN;
    if (valid(i)) v = (int16_t)constrain(lroundf(val[i] * PHONE_SCALE[i]), -32767, 32767);
    putInt16(obd + 1 + 2 * i, v);
  }
  obd[1 + 2 * OBD_COUNT] = obdStatus == ST_SEARCHING ? ST_NO_BT : obdStatus;
  phoneValues->setValue(obd, sizeof(obd));
  phoneValues->notify();

  uint8_t solar[11] = {1};   // type 1
  solar[1] = valid(V_PV_W) ? solarState : 0xFF;
  solar[2] = solarError;
  putInt16(solar + 3, lroundf(val[V_PV_V] * 100));
  putInt16(solar + 5, lroundf(val[V_PV_A] * 10));
  putInt16(solar + 7, (uint16_t)lroundf(val[V_PV_KWH] * 100));
  putInt16(solar + 9, (uint16_t)lroundf(val[V_PV_W]));
  phoneValues->setValue(solar, sizeof(solar));
  phoneValues->notify();

  uint8_t bat[11] = {2};   // type 2
  bat[1] = valid(V_BAT_SOC) ? (uint8_t)val[V_BAT_SOC] : 0xFF;
  putInt16(bat + 2, (uint16_t)lroundf(val[V_BAT_V] * 100));
  putInt16(bat + 4, lroundf(val[V_BAT_A] * 100));
  putInt16(bat + 6, (uint16_t)lroundf(val[V_BAT_AH] * 10));
  putInt16(bat + 8, valid(V_BAT_TEMP) ? lroundf(val[V_BAT_TEMP] * 10) : INT16_MIN);
  bat[10] = batFets;
  phoneValues->setValue(bat, sizeof(bat));
  phoneValues->notify();
}

void phoneInit() {
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new PhoneCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);
  phoneValues = service->createCharacteristic(WERTE_UUID,
                  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  phoneValues->addDescriptor(new BLE2902());   // NimBLE adds this descriptor itself
#endif
  service->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  xTaskCreatePinnedToCore([](void*) {
    for (;;) { phoneSend(); delay(1000); }
  }, "phone", 4096, nullptr, 1, nullptr, 0);
}
#endif

// ---------------- Start ----------------
void bleDataInit() {
  radioLock = xSemaphoreCreateMutex();
  BLEDevice::init(BLE_NAME);
#if PHONE_BLE
  phoneInit();
#endif
  victronOn = parseVictronKey();
  if (!victronOn) Serial.println("[SOLAR] no key configured");

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanHandler(), true);   // report every packet
  scan->setActiveScan(true);    // names are often only in the scan response
  scan->setInterval(100);       // short window: the radio is shared with the connections
  scan->setWindow(30);
  startScan();

  if (strlen(AKKU_MAC)) xTaskCreatePinnedToCore(bmsTask, "bms", 6144, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(obdTask, "obd", 8192, nullptr, 1, nullptr, 0);
}

#endif
