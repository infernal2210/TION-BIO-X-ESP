import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.const import CONF_ID

DOMAIN = "tion_dp122_gate"

CONF_READY = "ready"
CONF_AVAILABLE = "available"
CONF_ENTITIES = "entities"
CONF_TIMEOUT = "timeout"

tion_dp122_gate_ns = cg.esphome_ns.namespace("tion_dp122_gate")
TionDP122Gate = tion_dp122_gate_ns.class_("TionDP122Gate", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TionDP122Gate),
        cv.Required(CONF_READY): cv.returning_lambda,
        cv.Required(CONF_AVAILABLE): cv.returning_lambda,
        cv.Required(CONF_ENTITIES): cv.ensure_list(cv.use_id(cg.EntityBase)),
        cv.Optional(CONF_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_ready(config[CONF_READY]))
    cg.add(var.set_available(config[CONF_AVAILABLE]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT].total_milliseconds))

    for entity_id in config[CONF_ENTITIES]:
        entity = await cg.get_variable(entity_id)
        cg.add(var.add_entity(entity))
