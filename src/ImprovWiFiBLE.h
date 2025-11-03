#pragma once
#include "ImprovTypes.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

// Improv over BLE transport for ESP32/ESP32-S3 (NimBLE-Arduino).
// Matches Improv BLE service/characteristics and framing.
class ImprovWiFiBLE : public NimBLECharacteristicCallbacks,
                      public NimBLEServerCallbacks {
public:
  using ConnectFn = std::function<bool(const String &ssid, const String &pass)>;
  using IdentifyFn = std::function<void(void)>;
  using ProvisionedFn = std::function<void(const String &url)>;

  ImprovWiFiBLE() = default;
  ~ImprovWiFiBLE();

  // Call once in setup(). deviceName is the BLE name users will see.
  void begin(ImprovTypes::ChipFamily chip, const String &deviceName,
             const String &fwVersion, const String &friendlyName);

  // Start/stop advertising the Improv BLE service.
  void start();
  void stop();

  // Hooks
  void onConnect(ConnectFn cb) { connect_cb_ = std::move(cb); }
  void onIdentify(IdentifyFn cb) { identify_cb_ = std::move(cb); }
  void onProvisioned(ProvisionedFn cb) { provisioned_cb_ = std::move(cb); }

  // State updates (library/app may call these to mirror actual Wi-Fi state)
  void setAuthorized(bool authorized);
  void setProvisioning();
  void setProvisioned(const String &optionalUrl = "");
  void setError(uint8_t code);

  // Server callbacks
  void onDisconnect(NimBLEServer *s);

  // Characteristic (RPC) writes
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override;

private:
  // Improv BLE UUIDs (service + required chars)
  static constexpr const char *SVC_UUID =
      "00467768-6228-2272-4663-277478268000";
  static constexpr const char *CHAR_STATE_UUID =
      "00467768-6228-2272-4663-277478268001";
  static constexpr const char *CHAR_ERROR_UUID =
      "00467768-6228-2272-4663-277478268002";
  static constexpr const char *CHAR_RPC_CMD_UUID =
      "00467768-6228-2272-4663-277478268003";
  static constexpr const char *CHAR_RPC_RES_UUID =
      "00467768-6228-2272-4663-277478268004";
  static constexpr const char *CHAR_CAPS_UUID =
      "00467768-6228-2272-4663-277478268005";
  static constexpr uint16_t SERVICE_DATA_UUID_16 = 0x4677; // for advertising

  enum : uint8_t {
    STATE_AUTH_REQUIRED = 0x01,
    STATE_AUTHORIZED = 0x02,
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED = 0x04
  };

  enum : uint8_t {
    ERR_NONE = 0x00,
    ERR_BAD_PACKET = 0x01,
    ERR_UNKNOWN_CMD = 0x02,
    ERR_CONNECT = 0x03,
    ERR_NOT_AUTH = 0x04,
    ERR_UNKNOWN = 0xFF
  };

  // helpers
  void updateState(uint8_t s);
  void updateError(uint8_t e);
  void updateCaps(uint8_t caps);
  void advertiseNow();
  static uint8_t checksumLSB(const uint8_t *data, size_t len);

  // RPCs
  void handleRpc(const uint8_t *data, size_t len);
  void rpcSendWifi(const uint8_t *payload, size_t n);
  void rpcIdentify();

  // BLE objects
  NimBLEServer *server_{nullptr};
  NimBLEService *service_{nullptr};
  NimBLECharacteristic *ch_state_{nullptr};
  NimBLECharacteristic *ch_error_{nullptr};
  NimBLECharacteristic *ch_rpc_cmd_{nullptr};
  NimBLECharacteristic *ch_rpc_res_{nullptr};
  NimBLECharacteristic *ch_caps_{nullptr};
  NimBLEAdvertising *adv_{nullptr};

  // identity
  ImprovTypes::ChipFamily chip_{ImprovTypes::ChipFamily::CF_ESP32};
  String device_name_;
  String fw_version_;
  String friendly_name_;

  // state
  uint8_t state_{STATE_AUTHORIZED}; // start ready unless you gate with a button
  uint8_t error_{ERR_NONE};
  uint8_t caps_{0x01}; // bit0: Identify supported

  // user callbacks
  ConnectFn connect_cb_;
  IdentifyFn identify_cb_;
  ProvisionedFn provisioned_cb_;
};