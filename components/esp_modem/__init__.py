from esphome import pins
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_ON_CONNECT,
    CONF_ON_DISCONNECT,
    CONF_TRIGGER_ID,
)
from esphome.core import CORE, coroutine_with_priority
from esphome.components.network import IPAddress

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network"]

modem_ns = cg.esphome_ns.namespace("modem")
CONF_MODEM_POWER_GPIO = "power_gpio"
CONF_MODEM_FLIGHT_GPIO = "flight_gpio"
CONF_MODEM_UART_NUM = "uart_num"
CONF_MODEM_UART_TXD = "uart_txd"
CONF_MODEM_UART_RXD = "uart_rxd"
CONF_MODEM_APN = "apn"

ModemComponent = modem_ns.class_("ModemComponent", cg.Component)
ModemConnectTrigger = modem_ns.class_("ModemConnectTrigger", automation.Trigger.template())
ModemDisconnectTrigger = modem_ns.class_("ModemDisconnectTrigger", automation.Trigger.template())

def _validate(config):
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ModemComponent),
            cv.Required(CONF_MODEM_POWER_GPIO): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_MODEM_FLIGHT_GPIO): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_MODEM_UART_NUM): cv.int_range(min=0, max=2),
            cv.Required(CONF_MODEM_UART_TXD): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_MODEM_UART_RXD): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_MODEM_APN): cv.string,
            cv.Optional(CONF_ON_CONNECT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ModemConnectTrigger),
                }
            ),
            cv.Optional(CONF_ON_DISCONNECT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ModemDisconnectTrigger),
                }
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
)

@coroutine_with_priority(60.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_power_gpio(config[CONF_MODEM_POWER_GPIO]))
    cg.add(var.set_flight_gpio(config[CONF_MODEM_FLIGHT_GPIO]))
    uart_num = cg.RawExpression(f'UART_NUM_{config[CONF_MODEM_UART_NUM]}')
    cg.add(var.set_dte_config(uart_num, config[CONF_MODEM_UART_TXD], config[CONF_MODEM_UART_RXD]))
    cg.add(var.set_dce_config(config[CONF_MODEM_APN]))
    cg.add_define("USE_MODEM")

    for conf in config.get(CONF_ON_CONNECT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
