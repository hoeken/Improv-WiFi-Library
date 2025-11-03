#include "ImprovWiFiBLE.h"

void ImprovWiFiBLE::begin(ImprovTypes::ChipFamily chip,
                          const String &deviceName, const String &fwVersion,
                          const String &friendlyName) {
  chip_ = chip;
  device_name_ = deviceName;
  fw_version_ = fwVersion;
  friendly_name_ = friendlyName;

  NimBLEDevice::init(device_name_.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(this);

  service_ = server_->createService(SVC_UUID);
  ch_state_ = service_->createCharacteristic(
      CHAR_STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_error_ = service_->createCharacteristic(
      CHAR_ERROR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_rpc_cmd_ =
      service_->createCharacteristic(CHAR_RPC_CMD_UUID, NIMBLE_PROPERTY::WRITE);
  ch_rpc_res_ = service_->createCharacteristic(
      CHAR_RPC_RES_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_caps_ =
      service_->createCharacteristic(CHAR_CAPS_UUID, NIMBLE_PROPERTY::READ);

  ch_rpc_cmd_->setCallbacks(this);
  updateState(state_);
  updateError(error_);
  updateCaps(caps_);

  service_->start();

  adv_ = NimBLEDevice::getAdvertising();
  adv_->addServiceUUID(SVC_UUID);
  advertiseNow();
  adv_->start();
}

void ImprovWiFiBLE::start() {
  if (!adv_)
    return;
  advertiseNow();
  adv_->start();
}

void ImprovWiFiBLE::stop() {
  if (adv_)
    adv_->stop();
  NimBLEDevice::deinit(true);
}

void ImprovWiFiBLE::setAuthorized(bool authorized) {
  updateState(authorized ? STATE_AUTHORIZED : STATE_AUTH_REQUIRED);
  advertiseNow();
}

void ImprovWiFiBLE::setProvisioning() {
  updateState(STATE_PROVISIONING);
  advertiseNow();
}

void ImprovWiFiBLE::setProvisioned(const String &optionalUrl) {
  updateState(STATE_PROVISIONED);
  advertiseNow();

  // RPC result: send optional URL for clients to open
  std::vector<uint8_t> buf;
  buf.push_back(0x01); // last cmd = Send Wi-Fi
  const std::string s1 = optionalUrl.c_str();
  const size_t payload_len = 1 + s1.size(); // [len][url]
  buf.push_back(static_cast<uint8_t>(payload_len));
  buf.push_back(static_cast<uint8_t>(s1.size()));
  buf.insert(buf.end(), s1.begin(), s1.end());
  buf.push_back(checksumLSB(buf.data(), buf.size()));
  ch_rpc_res_->setValue((uint8_t *)buf.data(), buf.size());
  ch_rpc_res_->notify();

  delay(250); // give clients time to read new state
}

void ImprovWiFiBLE::setError(uint8_t code) {
  updateError(code);
  ch_error_->notify();
  advertiseNow();
}

void ImprovWiFiBLE::onDisconnect(NimBLEServer *) {
  if (adv_) {
    advertiseNow();
    adv_->start();
  }
}

void ImprovWiFiBLE::onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) {
  if (c != ch_rpc_cmd_)
    return;
  const std::string v = c->getValue();
  if (v.size() < 3) {
    setError(ERR_BAD_PACKET);
    return;
  }
  handleRpc(reinterpret_cast<const uint8_t *>(v.data()), v.size());
}

void ImprovWiFiBLE::handleRpc(const uint8_t *data, size_t len) {
  const uint8_t cmd = data[0];
  const uint8_t declared_len = data[1];
  if (declared_len + 3 != len) {
    setError(ERR_BAD_PACKET);
    return;
  }
  const uint8_t cs = data[len - 1];
  if (checksumLSB(data, len - 1) != cs) {
    setError(ERR_BAD_PACKET);
    return;
  }

  switch (cmd) {
  case 0x01:
    rpcSendWifi(&data[2], declared_len);
    break; // Send Wi-Fi
  case 0x02:
    rpcIdentify();
    break; // Identify
  default:
    setError(ERR_UNKNOWN_CMD);
    break;
  }
}

void ImprovWiFiBLE::rpcSendWifi(const uint8_t *p, size_t n) {
  if (n < 2) {
    setError(ERR_BAD_PACKET);
    return;
  }

  const uint8_t ssid_len = p[0];
  if (1 + ssid_len > n) {
    setError(ERR_BAD_PACKET);
    return;
  }
  String ssid;
  ssid.reserve(ssid_len);
  for (uint8_t i = 0; i < ssid_len; i++)
    ssid += (char)p[1 + i];

  size_t pos = 1 + ssid_len;
  if (pos >= n) {
    setError(ERR_BAD_PACKET);
    return;
  }
  const uint8_t pass_len = p[pos];
  if (pos + 1 + pass_len > n) {
    setError(ERR_BAD_PACKET);
    return;
  }
  String pass;
  pass.reserve(pass_len);
  for (uint8_t i = 0; i < pass_len; i++)
    pass += (char)p[pos + 1 + i];

  setProvisioning();

  bool ok = false;
  if (connect_cb_)
    ok = connect_cb_(ssid, pass);

  if (!ok) {
    setError(ERR_CONNECT);
    setAuthorized(true);
    return;
  }

  if (provisioned_cb_)
    provisioned_cb_("");
  setProvisioned("");
}

void ImprovWiFiBLE::rpcIdentify() {
  if (identify_cb_)
    identify_cb_();
}

void ImprovWiFiBLE::updateState(uint8_t s) {
  state_ = s;
  ch_state_->setValue(&state_, 1);
  ch_state_->notify();
}

void ImprovWiFiBLE::updateError(uint8_t e) {
  error_ = e;
  ch_error_->setValue(&error_, 1);
}

void ImprovWiFiBLE::updateCaps(uint8_t caps) {
  caps_ = caps;
  ch_caps_->setValue(&caps_, 1);
}

void ImprovWiFiBLE::advertiseNow() {
  if (!adv_)
    return;
  adv_->stop();

  // Complete Service Data AD structure:
  // [0]  = 0x16  → AD type: Service Data (16-bit UUID)
  // [1]  = 0x77  → UUID 0x4677 (low byte, little-endian)
  // [2]  = 0x46  → UUID 0x4677 (high byte)
  // [3]  = state_ → Improv state (authorized / provisioning / etc.)
  // [4]  = caps_  → device capabilities bitmask
  // [5-8] = reserved / zeros per Improv spec

  const uint8_t serviceData[] = {0x16, 0x77, 0x46, state_, caps_,
                                 0x00, 0x00, 0x00, 0x00};

  NimBLEAdvertisementData ad;
  ad.addData(serviceData, sizeof(serviceData));

  adv_->setAdvertisementData(ad);
}

uint8_t ImprovWiFiBLE::checksumLSB(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum += data[i];
  return (uint8_t)(sum & 0xFF);
}