#pragma once
#include "ImprovTypes.h"
#include <Arduino.h>

// Forward decls to avoid hard deps here
class ImprovWiFi; // serial transport from the existing repo
class ImprovWiFiBLE;

class ImprovWiFiMux {
public:
  // Pass the already-constructed transports you want to use.
  ImprovWiFiMux(ImprovWiFi *serial, ImprovWiFiBLE *ble)
      : serial_(serial), ble_(ble) {}

  // Call after you have set device info on both transports.
  // connectFn should actually attempt Wi-Fi and return true on success.
  void attach(std::function<bool(const String &, const String &)> connectFn,
              std::function<void(void)> identifyFn = nullptr) {
    connect_fn_ = std::move(connectFn);
    identify_fn_ = std::move(identifyFn);

    if (ble_) {
      ble_->onConnect([this](const String &ssid, const String &pass) {
        const bool ok = connect_fn_ ? connect_fn_(ssid, pass) : false;
        if (ok)
          onProvisioned();
        return ok;
      });
      ble_->onIdentify([this]() {
        if (identify_fn_)
          identify_fn_();
      });
      ble_->onProvisioned([this](const String &url) { onProvisioned(url); });
    }
    // Serial path is polled in loop via serial_->handleSerial();
    // We assume user calls setDeviceInfo & uses the repo's existing callbacks
    // for "connected".
  }

  // Call this in loop() to service Serial Improv.
  void handle() {
    if (serial_)
      serial_handle_();
  }

  // Tell both transports we're now provisioned.
  void onProvisioned(const String &url = "") {
    if (ble_)
      ble_->setProvisioned(url);
    // Serial transport typically responds when Wi-Fi comes up; nothing to call
    // here
  }

  // Bind serial handle function (matches the repo's ImprovWiFi API).
  void bindSerialHandle(std::function<void(void)> fn) {
    serial_handle_ = std::move(fn);
  }

private:
  ImprovWiFi *serial_{nullptr};
  ImprovWiFiBLE *ble_{nullptr};

  std::function<bool(const String &, const String &)> connect_fn_;
  std::function<void(void)> identify_fn_;
  std::function<void(void)> serial_handle_ = []() {};
};
