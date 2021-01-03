import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

CONF_Q7RF_DEVICE_ID = "q7rf_device_id"
CONF_Q7RF_RESEND_INTERVAL = "q7rf_resend_interval"

q7rf_ns = cg.esphome_ns.namespace("q7rf")
Q7RF_SWITCH = q7rf_ns.class_("Q7RFSwitch", switch.Switch, cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    switch.SWITCH_SCHEMA.extend({cv.GenerateID(): cv.declare_id(Q7RF_SWITCH)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
    .extend(
        cv.Schema(
            {
                cv.Required(CONF_Q7RF_DEVICE_ID): cv.hex_uint16_t,
                cv.Optional(CONF_Q7RF_RESEND_INTERVAL): cv.uint16_t,
            }
        )
    )
)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)
    yield spi.register_spi_device(var, config)

    cg.add(var.set_q7rf_device_id(config[CONF_Q7RF_DEVICE_ID]))
    if CONF_Q7RF_RESEND_INTERVAL in config:
        cg.add(var.set_q7rf_resend_interval(config[CONF_Q7RF_RESEND_INTERVAL]))
