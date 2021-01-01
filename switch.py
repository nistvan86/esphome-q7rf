import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

q7rf_ns = cg.esphome_ns.namespace("q7rf")
Q7RF = q7rf_ns.class_("Q7RF", switch.Switch, cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    switch.SWITCH_SCHEMA.extend({cv.GenerateID(): cv.declare_id(Q7RF)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)
    yield spi.register_spi_device(var, config)
