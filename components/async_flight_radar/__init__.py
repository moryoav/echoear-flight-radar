from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ON_ERROR, CONF_ON_RESPONSE, CONF_TIMEOUT


CODEOWNERS = []

CONF_BUFFER_SIZE_RX = "buffer_size_rx"
CONF_BUFFER_SIZE_TX = "buffer_size_tx"
CONF_AEROAPI_KEY = "aeroapi_key"
CONF_MAX_RESPONSE_SIZE = "max_response_size"
CONF_ON_ENRICHMENT_ERROR = "on_enrichment_error"
CONF_ON_ENRICHMENT_RESPONSE = "on_enrichment_response"
CONF_ON_FLIGHT_TIMES_ERROR = "on_flight_times_error"
CONF_ON_FLIGHT_TIMES_RESPONSE = "on_flight_times_response"

async_flight_radar_ns = cg.esphome_ns.namespace("async_flight_radar")
AsyncFlightRadarComponent = async_flight_radar_ns.class_(
    "AsyncFlightRadarComponent", cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AsyncFlightRadarComponent),
            cv.Optional(CONF_TIMEOUT, default="10s"):
                cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MAX_RESPONSE_SIZE, default="32KiB"): cv.validate_bytes,
            cv.Optional(CONF_BUFFER_SIZE_RX, default=2048): cv.uint16_t,
            cv.Optional(CONF_BUFFER_SIZE_TX, default=512): cv.uint16_t,
            cv.Required(CONF_AEROAPI_KEY): cv.string_strict,
            cv.Optional(CONF_ON_RESPONSE): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ERROR): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ENRICHMENT_RESPONSE):
                automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ENRICHMENT_ERROR):
                automation.validate_automation(single=True),
            cv.Optional(CONF_ON_FLIGHT_TIMES_RESPONSE):
                automation.validate_automation(single=True),
            cv.Optional(CONF_ON_FLIGHT_TIMES_ERROR):
                automation.validate_automation(single=True),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.require_framework_version(esp_idf=cv.Version(5, 1, 0)),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_max_response_size(config[CONF_MAX_RESPONSE_SIZE]))
    cg.add(var.set_buffer_size_rx(config[CONF_BUFFER_SIZE_RX]))
    cg.add(var.set_buffer_size_tx(config[CONF_BUFFER_SIZE_TX]))
    cg.add(var.set_aeroapi_key(config[CONF_AEROAPI_KEY]))

    if on_response := config.get(CONF_ON_RESPONSE):
        await automation.build_automation(
            var.get_response_trigger(),
            [
                (cg.const_char_ptr, "body"),
                (cg.size_t, "body_length"),
                (cg.int_, "status_code"),
                (cg.uint32, "duration_ms"),
            ],
            on_response,
        )

    if on_error := config.get(CONF_ON_ERROR):
        await automation.build_automation(
            var.get_error_trigger(),
            [(cg.std_string, "error"), (cg.uint32, "duration_ms")],
            on_error,
        )

    if on_enrichment_response := config.get(CONF_ON_ENRICHMENT_RESPONSE):
        await automation.build_automation(
            var.get_enrichment_response_trigger(),
            [
                (cg.const_char_ptr, "body"),
                (cg.size_t, "body_length"),
                (cg.int_, "status_code"),
                (cg.uint32, "duration_ms"),
                (cg.std_string, "callsign"),
                (cg.std_string, "aircraft_hex"),
            ],
            on_enrichment_response,
        )

    if on_enrichment_error := config.get(CONF_ON_ENRICHMENT_ERROR):
        await automation.build_automation(
            var.get_enrichment_error_trigger(),
            [
                (cg.std_string, "error"),
                (cg.uint32, "duration_ms"),
                (cg.std_string, "callsign"),
                (cg.std_string, "aircraft_hex"),
            ],
            on_enrichment_error,
        )

    if on_flight_times_response := config.get(CONF_ON_FLIGHT_TIMES_RESPONSE):
        await automation.build_automation(
            var.get_flight_times_response_trigger(),
            [
                (cg.const_char_ptr, "body"),
                (cg.size_t, "body_length"),
                (cg.int_, "status_code"),
                (cg.uint32, "duration_ms"),
                (cg.std_string, "callsign"),
                (cg.std_string, "aircraft_hex"),
                (cg.std_string, "registration"),
            ],
            on_flight_times_response,
        )

    if on_flight_times_error := config.get(CONF_ON_FLIGHT_TIMES_ERROR):
        await automation.build_automation(
            var.get_flight_times_error_trigger(),
            [
                (cg.std_string, "error"),
                (cg.uint32, "duration_ms"),
                (cg.std_string, "callsign"),
                (cg.std_string, "aircraft_hex"),
                (cg.std_string, "registration"),
            ],
            on_flight_times_error,
        )

    esp32.include_builtin_idf_component("esp_http_client")
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_TLS_INSECURE", False)
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY", False)
