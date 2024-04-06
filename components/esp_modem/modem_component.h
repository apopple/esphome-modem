#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/components/network/ip_address.h"
#include "cxx_include/esp_modem_types.hpp"
#include "esp_modem_c_api_types.h"
#include "esp_log.h"

#include "driver/gpio.h"

#ifdef USE_ESP32

namespace esphome {
namespace modem {

enum class ModemComponentState {
  STOPPED,
  CONNECTING,
  CONNECT_NOTIFY,
  CONNECTED,
  DISCONNECTED,
};

class ModemComponent : public Component {
 public:
  ModemComponent();
  void setup() override;
  void loop() override;
  void on_shutdown() override { powerdown(); }
  bool is_connected();
  void add_on_connect_callback(std::function<void()> callback) {
    this->on_connect_callback_.add(std::move(callback));
   }
  void add_on_disconnect_callback(std::function<void()> callback) {
    this->on_disconnect_callback_.add(std::move(callback));
   }

  void set_power_gpio(int power_gpio);
  void set_flight_gpio(int flight_gpio);
  void set_dte_config(uart_port_t uart_num, int uart_txd, int uart_rxd);
  void set_dce_config(const char *apn);
  void powerdown();

 protected:
  static void esp_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void esp_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  gpio_num_t power_gpio_;
  gpio_num_t flight_gpio_;
  esp_modem_dte_config_t dte_config_;
  esp_modem_dce_config_t dce_config_;
  ModemComponentState state_{ModemComponentState::STOPPED};
  esp_netif_t *modem_netif_{nullptr};
  esp_modem_dce_t *dce_{nullptr};

  CallbackManager<void()> on_connect_callback_;
  CallbackManager<void()> on_disconnect_callback_;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern ModemComponent *global_modem_component;

class ModemConnectTrigger : public Trigger<> {
 public:
  explicit ModemConnectTrigger(ModemComponent *&modem) {
    modem->add_on_connect_callback([this](void) { this->trigger(); });
  }
};

}  // namespace modem
}  // namespace esphome

#endif  // USE_ESP32
