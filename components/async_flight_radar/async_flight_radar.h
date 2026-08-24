#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace esphome {
namespace async_flight_radar {

enum class EnrichmentStatus : uint8_t {
  EMPTY = 0,
  PENDING,
  READY,
  UNAVAILABLE,
  RETRY,
};

struct EnrichmentRecord {
  bool occupied{false};
  bool route_positions_valid{false};
  EnrichmentStatus status{EnrichmentStatus::EMPTY};
  uint32_t status_changed_ms{0};
  uint32_t last_seen_ms{0};
  char callsign[16]{};
  char aircraft_hex[10]{};
  char airline[33]{};
  char origin_city[25]{};
  char origin_code[6]{};
  float origin_lat{0.0f};
  float origin_lon{0.0f};
  char destination_city[25]{};
  char destination_code[6]{};
  float destination_lat{0.0f};
  float destination_lon{0.0f};
};

class AsyncFlightRadarComponent final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  void set_max_response_size(size_t max_response_size) { this->max_response_size_ = max_response_size; }
  void set_buffer_size_rx(uint16_t buffer_size_rx) { this->buffer_size_rx_ = buffer_size_rx; }
  void set_buffer_size_tx(uint16_t buffer_size_tx) { this->buffer_size_tx_ = buffer_size_tx; }
  void set_aeroapi_key(const std::string &aeroapi_key) { this->aeroapi_key_ = aeroapi_key; }

  bool start(const std::string &url);
  bool start_enrichment(const std::string &url, const std::string &callsign,
                        const std::string &aircraft_hex);
  bool start_flight_times(const std::string &url, const std::string &callsign,
                          const std::string &aircraft_hex, const std::string &registration);
  bool start_flight_operator(const std::string &url, const std::string &callsign,
                             const std::string &aircraft_hex, const std::string &operator_code);
  bool is_running() const { return this->request_running_; }
  bool is_enrichment_running() const { return this->enrichment_request_running_; }
  bool is_flight_times_running() const { return this->flight_times_request_running_; }
  bool is_flight_operator_running() const { return this->flight_operator_request_running_; }
  float get_max_step_duration_ms() const { return this->max_main_step_duration_us_ / 1000.0f; }
  uint32_t get_failure_count() const { return this->failure_count_; }
  uint32_t get_enrichment_failure_count() const { return this->enrichment_failure_count_; }
  uint32_t get_flight_times_failure_count() const { return this->flight_times_failure_count_; }
  uint32_t get_enrichment_cache_ready_count() const;
  uint32_t get_worker_stack_high_water_bytes() const { return this->worker_stack_high_water_bytes_.load(); }

  bool needs_enrichment(const std::string &callsign);
  const EnrichmentRecord *find_enrichment(const std::string &callsign);
  void store_enrichment_ready(const std::string &callsign, const std::string &aircraft_hex,
                              const std::string &airline, const std::string &origin_city,
                              const std::string &origin_code, float origin_lat, float origin_lon,
                              const std::string &destination_city, const std::string &destination_code,
                              float destination_lat, float destination_lon, bool route_positions_valid);
  void store_enrichment_unavailable(const std::string &callsign, const std::string &aircraft_hex);
  void store_enrichment_error(const std::string &callsign, const std::string &aircraft_hex);
  void merge_enrichment_route(const std::string &callsign, const std::string &aircraft_hex,
                              const std::string &origin_city, const std::string &origin_code,
                              const std::string &destination_city, const std::string &destination_code);
  void merge_enrichment_airline(const std::string &callsign, const std::string &aircraft_hex,
                                const std::string &airline);

  Trigger<const char *, size_t, int, uint32_t> *get_response_trigger() { return &this->response_trigger_; }
  Trigger<std::string, uint32_t> *get_error_trigger() { return &this->error_trigger_; }
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string> *get_enrichment_response_trigger() {
    return &this->enrichment_response_trigger_;
  }
  Trigger<std::string, uint32_t, std::string, std::string> *get_enrichment_error_trigger() {
    return &this->enrichment_error_trigger_;
  }
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string, std::string> *
  get_flight_times_response_trigger() {
    return &this->flight_times_response_trigger_;
  }
  Trigger<std::string, uint32_t, std::string, std::string, std::string> *
  get_flight_times_error_trigger() {
    return &this->flight_times_error_trigger_;
  }
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string, std::string> *
  get_flight_operator_response_trigger() {
    return &this->flight_operator_response_trigger_;
  }
  Trigger<std::string, uint32_t, std::string, std::string, std::string> *
  get_flight_operator_error_trigger() {
    return &this->flight_operator_error_trigger_;
  }

 protected:
  static constexpr size_t MAX_URL_SIZE = 256;
  static constexpr size_t REQUEST_KEY_SIZE = 16;
  static constexpr size_t AIRCRAFT_HEX_SIZE = 10;
  static constexpr size_t REGISTRATION_SIZE = 16;
  static constexpr size_t OPERATOR_CODE_SIZE = 8;
  static constexpr size_t ERROR_SIZE = 96;
  static constexpr uint32_t WORKER_STACK_SIZE = 8192;
  static constexpr size_t ENRICHMENT_CACHE_SIZE = 48;
  static constexpr uint32_t ENRICHMENT_READY_TTL_MS = 12UL * 60UL * 60UL * 1000UL;
  static constexpr uint32_t ENRICHMENT_UNAVAILABLE_TTL_MS = 30UL * 60UL * 1000UL;
  static constexpr uint32_t ENRICHMENT_RETRY_MS = 30UL * 1000UL;

  enum class RequestType : uint8_t {
    STOP = 0,
    RADAR,
    ENRICHMENT,
    FLIGHT_TIMES,
    FLIGHT_OPERATOR,
  };

  struct RequestMessage {
    RequestType type;
    uint32_t sequence;
    char url[MAX_URL_SIZE];
    char request_key[REQUEST_KEY_SIZE];
    char aircraft_hex[AIRCRAFT_HEX_SIZE];
    char registration[REGISTRATION_SIZE];
    char operator_code[OPERATOR_CODE_SIZE];
  };

  struct ResultMessage {
    RequestType type;
    uint32_t sequence;
    uint32_t duration_ms;
    size_t response_length;
    int status_code;
    bool success;
    char error[ERROR_SIZE];
    char request_key[REQUEST_KEY_SIZE];
    char aircraft_hex[AIRCRAFT_HEX_SIZE];
    char registration[REGISTRATION_SIZE];
    char operator_code[OPERATOR_CODE_SIZE];
  };

  static void worker_task_entry_(void *parameter);
  static esp_err_t http_event_handler_(esp_http_client_event_t *event);
  void worker_task_();
  ResultMessage perform_request_(const RequestMessage &request);
  void record_main_step_duration_(uint32_t started_us);
  static std::string normalize_callsign_(const std::string &callsign);
  static void copy_text_(char *destination, size_t destination_size, const std::string &source);
  EnrichmentRecord *find_enrichment_record_(const std::string &normalized_callsign);
  EnrichmentRecord *allocate_enrichment_record_(const std::string &normalized_callsign,
                                                 const std::string &aircraft_hex);
  void expire_enrichment_record_(EnrichmentRecord *record, uint32_t now);
  void mark_enrichment_pending_(const std::string &callsign, const std::string &aircraft_hex);

  QueueHandle_t request_queue_{nullptr};
  QueueHandle_t result_queue_{nullptr};
  QueueHandle_t response_ack_queue_{nullptr};
  TaskHandle_t worker_task_handle_{nullptr};
  char *response_buffer_{nullptr};
  size_t worker_response_length_{0};
  size_t max_response_size_{32768};
  uint32_t timeout_ms_{10000};
  uint32_t request_sequence_{0};
  uint32_t failure_count_{0};
  uint32_t enrichment_failure_count_{0};
  uint32_t flight_times_failure_count_{0};
  uint32_t max_main_step_duration_us_{0};
  uint16_t buffer_size_rx_{2048};
  uint16_t buffer_size_tx_{512};
  bool request_running_{false};
  bool enrichment_request_running_{false};
  bool flight_times_request_running_{false};
  bool flight_operator_request_running_{false};
  bool worker_response_overflow_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<uint32_t> worker_stack_high_water_bytes_{0};
  std::array<EnrichmentRecord, ENRICHMENT_CACHE_SIZE> enrichment_cache_{};
  std::string aeroapi_key_;

  Trigger<const char *, size_t, int, uint32_t> response_trigger_;
  Trigger<std::string, uint32_t> error_trigger_;
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string> enrichment_response_trigger_;
  Trigger<std::string, uint32_t, std::string, std::string> enrichment_error_trigger_;
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string, std::string>
      flight_times_response_trigger_;
  Trigger<std::string, uint32_t, std::string, std::string, std::string> flight_times_error_trigger_;
  Trigger<const char *, size_t, int, uint32_t, std::string, std::string, std::string>
      flight_operator_response_trigger_;
  Trigger<std::string, uint32_t, std::string, std::string, std::string> flight_operator_error_trigger_;
};

}  // namespace async_flight_radar
}  // namespace esphome
