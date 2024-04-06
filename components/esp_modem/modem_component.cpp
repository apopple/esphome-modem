#include "esphome/core/log.h"
#include "esphome/core/util.h"
#include "esphome/core/application.h"

#ifdef USE_ESP32

#include <cinttypes>
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "modem_component.h"

using esphome::esp_log_printf_;
#include "cxx_include/esp_modem_api.hpp"

extern "C" esp_err_t esp_modem_get_signal_quality(esp_modem_dce_t *dce_wrap, int *rssi, int *ber);

namespace esphome {
namespace modem {

static const char *const TAG = "modem";

ModemComponent *global_modem_component;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

#define ESPHL_ERROR_CHECK(err, message) \
  if ((err) != ESP_OK) { \
    ESP_LOGE(TAG, message ": (%d) %s", err, esp_err_to_name(err)); \
    this->mark_failed(); \
    return; \
  }

ModemComponent::ModemComponent() { global_modem_component = this; }

void ModemComponent::setup() {
  int rssi, ber;
  esp_err_t err;
  ESP_LOGCONFIG(TAG, "Setting up Modem...");
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    // Delay here to allow power to stabilise before Modem is initialized.
    delay(300);  // NOLINT
  }

  // init_gpio
  gpio_reset_pin(this->flight_gpio_);

  /* Set the GPIO as a push/pull output */
  gpio_set_direction(this->flight_gpio_, GPIO_MODE_OUTPUT);
  gpio_set_direction(this->power_gpio_, GPIO_MODE_OUTPUT);

  gpio_set_level(this->power_gpio_, 1);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  gpio_set_level(this->power_gpio_, 0);

  /* Enable flight mode */
  gpio_set_level(this->flight_gpio_, 1);

  err = esp_netif_init();
  ESPHL_ERROR_CHECK(err, "modem netif init error");
  err = esp_event_loop_create_default();
  ESPHL_ERROR_CHECK(err, "modem event loop error");

  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
  this->modem_netif_ = esp_netif_new(&cfg);

  ESP_LOGCONFIG(TAG, "Initializing esp_modem for the SIM7600 module...");
  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ModemComponent::esp_ip_event, NULL);
  ESPHL_ERROR_CHECK(err, "modem event handler register error");
  err = esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &ModemComponent::esp_ppp_changed, NULL);
  ESPHL_ERROR_CHECK(err, "modem IP event handler register error");

  this->dce_ = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &this->dte_config_, &this->dce_config_, this->modem_netif_);
  assert(this->dce_);

  err = esp_modem_set_mode(this->dce_, ESP_MODEM_MODE_COMMAND);
  ESPHL_ERROR_CHECK(err, "esp_modem_set_mode(ESP_MODEM_MODE_COMMAND) failed");

  err = esp_modem_get_signal_quality(this->dce_, &rssi, &ber);
  ESPHL_ERROR_CHECK(err, "esp_modem_get_signal_quality failed with");
  ESP_LOGI(TAG, "Signal quality: rssi=%d, ber=%d", rssi, ber);

  err = esp_modem_set_mode(this->dce_, ESP_MODEM_MODE_DATA);
  ESPHL_ERROR_CHECK(err, "esp_modem_set_mode(ESP_MODEM_MODE_DATA) failed");
}

void ModemComponent::loop() {
  const uint32_t now = millis();

  switch (this->state_) {
    case ModemComponentState::STOPPED:
      break;
    case ModemComponentState::CONNECTING:
      break;
    case ModemComponentState::CONNECT_NOTIFY:
      this->on_connect_callback_.call();
      this->state_ = ModemComponentState::CONNECTED;
      break;
    case ModemComponentState::CONNECTED:
      break;
    case ModemComponentState::DISCONNECTED:
      this->on_disconnect_callback_.call();
      this->state_ = ModemComponentState::STOPPED;
      break;
  }
}

void ModemComponent::esp_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event, void *event_data) {
  const char *event_name;

  ESP_LOGI(TAG, "PPP state changed event %ld", event);
}

void ModemComponent::esp_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == IP_EVENT_PPP_GOT_IP) {
      esp_netif_dns_info_t dns_info;
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      esp_netif_t *netif = event->esp_netif;

      ESP_LOGI(TAG, "Modem Connect to PPP Server");
      ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
      ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
      ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
      esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
      ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
      esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);
      ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));

      global_modem_component->state_ = ModemComponentState::CONNECT_NOTIFY;
  } else if (event_id == IP_EVENT_PPP_LOST_IP) {
      ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
      global_modem_component->state_ = ModemComponentState::DISCONNECTED;
  } else if (event_id == IP_EVENT_GOT_IP6) {
      ESP_LOGI(TAG, "GOT IPv6 event!");

      ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
      ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
  }
}

bool ModemComponent::is_connected() { return this->state_ == ModemComponentState::CONNECTED; }

void ModemComponent::set_power_gpio(int power_gpio) { this->power_gpio_ = gpio_num_t(power_gpio); }
void ModemComponent::set_flight_gpio(int flight_gpio) { this->flight_gpio_ = gpio_num_t(flight_gpio); }
void ModemComponent::set_dte_config(uart_port_t uart_num, int uart_txd, int uart_rxd) {
  this->dte_config_ = ESP_MODEM_DTE_DEFAULT_CONFIG();
  this->dte_config_.uart_config.port_num = uart_num;
  this->dte_config_.uart_config.tx_io_num = uart_txd;
  this->dte_config_.uart_config.rx_io_num = uart_rxd;
  this->dte_config_.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
  this->dte_config_.uart_config.rx_buffer_size = 1024;
  this->dte_config_.uart_config.tx_buffer_size = 512;
  this->dte_config_.uart_config.event_queue_size = 30;
  this->dte_config_.task_stack_size = 2048;
  this->dte_config_.task_priority = 5;
  this->dte_config_.dte_buffer_size = 512;
}
void ModemComponent::set_dce_config(const char *apn) {
  this->dce_config_ = ESP_MODEM_DCE_DEFAULT_CONFIG(apn);
}

void ModemComponent::powerdown() { }

}  // namespace modem
}  // namespace esphome

#endif  // USE_ESP32
