#include "async_flight_radar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

namespace esphome {
namespace async_flight_radar {

static const char *const TAG = "async_flight_radar";
static constexpr uint32_t SLOW_MAIN_STEP_WARNING_US = 50000;

void AsyncFlightRadarComponent::setup() {
  RAMAllocator<char> allocator(RAMAllocator<char>::ALLOC_EXTERNAL);
  this->response_buffer_ = allocator.allocate(this->max_response_size_ + 1);
  if (this->response_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate %u-byte response buffer in PSRAM",
             static_cast<unsigned>(this->max_response_size_ + 1));
    this->mark_failed();
    return;
  }
  this->response_buffer_[0] = '\0';

  this->request_queue_ = xQueueCreate(3, sizeof(RequestMessage));
  this->result_queue_ = xQueueCreate(1, sizeof(ResultMessage));
  this->response_ack_queue_ = xQueueCreate(1, sizeof(uint8_t));
  if (this->request_queue_ == nullptr || this->result_queue_ == nullptr || this->response_ack_queue_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate worker queues");
    this->mark_failed();
    return;
  }

  const BaseType_t task_result =
      xTaskCreatePinnedToCore(AsyncFlightRadarComponent::worker_task_entry_, "flight_radar_http", WORKER_STACK_SIZE,
                              this, 1, &this->worker_task_handle_, 0);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Could not create HTTP worker task");
    this->worker_task_handle_ = nullptr;
    this->mark_failed();
  }
}

void AsyncFlightRadarComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Async Flight Radar HTTP worker:");
  ESP_LOGCONFIG(TAG, "  Request timeout: %u ms", static_cast<unsigned>(this->timeout_ms_));
  ESP_LOGCONFIG(TAG, "  Maximum response: %u bytes", static_cast<unsigned>(this->max_response_size_));
  ESP_LOGCONFIG(TAG, "  RX buffer: %u bytes", this->buffer_size_rx_);
  ESP_LOGCONFIG(TAG, "  TX buffer: %u bytes", this->buffer_size_tx_);
  ESP_LOGCONFIG(TAG, "  Worker stack: %u bytes on core 0", static_cast<unsigned>(WORKER_STACK_SIZE));
  ESP_LOGCONFIG(TAG, "  Enrichment cache: %u fixed records", static_cast<unsigned>(ENRICHMENT_CACHE_SIZE));
  ESP_LOGCONFIG(TAG, "  AeroAPI authentication: %s", this->aeroapi_key_.empty() ? "missing" : "configured");
  ESP_LOGCONFIG(TAG, "  Response buffer: %s", this->response_buffer_ != nullptr ? "allocated in PSRAM" : "unavailable");
}

bool AsyncFlightRadarComponent::start(const std::string &url) {
  if (this->is_failed() || this->shutting_down_.load() || this->request_running_ ||
      this->request_queue_ == nullptr) {
    return false;
  }
  if (url.rfind("https://", 0) != 0 || url.size() >= MAX_URL_SIZE) {
    ESP_LOGE(TAG, "Request rejected: invalid or oversized HTTPS URL");
    return false;
  }

  RequestMessage request{};
  request.type = RequestType::RADAR;
  request.sequence = ++this->request_sequence_;
  std::memcpy(request.url, url.c_str(), url.size() + 1);
  if (xQueueSendToFront(this->request_queue_, &request, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Request %u rejected: worker queue is full", static_cast<unsigned>(request.sequence));
    return false;
  }

  this->request_running_ = true;
  ESP_LOGD(TAG, "Request %u queued for HTTP worker", static_cast<unsigned>(request.sequence));
  return true;
}

bool AsyncFlightRadarComponent::start_enrichment(const std::string &url, const std::string &callsign,
                                                  const std::string &aircraft_hex) {
  if (this->is_failed() || this->shutting_down_.load() || this->enrichment_request_running_ ||
      this->request_queue_ == nullptr) {
    return false;
  }
  if (url.rfind("https://", 0) != 0 || url.size() >= MAX_URL_SIZE) {
    ESP_LOGE(TAG, "Enrichment request rejected: invalid or oversized HTTPS URL");
    return false;
  }

  const std::string normalized_callsign = normalize_callsign_(callsign);
  if (normalized_callsign.empty() || normalized_callsign.size() >= REQUEST_KEY_SIZE) {
    ESP_LOGE(TAG, "Enrichment request rejected: invalid callsign");
    return false;
  }
  if (!this->needs_enrichment(normalized_callsign)) return false;

  RequestMessage request{};
  request.type = RequestType::ENRICHMENT;
  request.sequence = ++this->request_sequence_;
  std::memcpy(request.url, url.c_str(), url.size() + 1);
  copy_text_(request.request_key, sizeof(request.request_key), normalized_callsign);
  copy_text_(request.aircraft_hex, sizeof(request.aircraft_hex), aircraft_hex);
  if (xQueueSendToBack(this->request_queue_, &request, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Enrichment %u rejected: worker queue is full", static_cast<unsigned>(request.sequence));
    return false;
  }

  this->enrichment_request_running_ = true;
  this->mark_enrichment_pending_(normalized_callsign, aircraft_hex);
  ESP_LOGD(TAG, "Enrichment %u queued for %s", static_cast<unsigned>(request.sequence), request.request_key);
  return true;
}

bool AsyncFlightRadarComponent::start_flight_times(const std::string &url, const std::string &callsign,
                                                    const std::string &aircraft_hex,
                                                    const std::string &registration) {
  if (this->is_failed() || this->shutting_down_.load() || this->flight_times_request_running_ ||
      this->request_queue_ == nullptr || this->aeroapi_key_.empty()) {
    return false;
  }
  if (url.rfind("https://", 0) != 0 || url.size() >= MAX_URL_SIZE) {
    ESP_LOGE(TAG, "Flight-times request rejected: invalid or oversized HTTPS URL");
    return false;
  }

  const std::string normalized_callsign = normalize_callsign_(callsign);
  if (normalized_callsign.empty() || normalized_callsign.size() >= REQUEST_KEY_SIZE) {
    ESP_LOGE(TAG, "Flight-times request rejected: invalid callsign");
    return false;
  }

  RequestMessage request{};
  request.type = RequestType::FLIGHT_TIMES;
  request.sequence = ++this->request_sequence_;
  std::memcpy(request.url, url.c_str(), url.size() + 1);
  copy_text_(request.request_key, sizeof(request.request_key), normalized_callsign);
  copy_text_(request.aircraft_hex, sizeof(request.aircraft_hex), aircraft_hex);
  copy_text_(request.registration, sizeof(request.registration), registration);
  if (xQueueSendToFront(this->request_queue_, &request, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Flight-times %u rejected: worker queue is full",
             static_cast<unsigned>(request.sequence));
    return false;
  }

  this->flight_times_request_running_ = true;
  ESP_LOGD(TAG, "Flight-times %u queued for %s", static_cast<unsigned>(request.sequence),
           request.request_key);
  return true;
}

bool AsyncFlightRadarComponent::start_flight_operator(const std::string &url, const std::string &callsign,
                                                       const std::string &aircraft_hex,
                                                       const std::string &operator_code) {
  if (this->is_failed() || this->shutting_down_.load() || this->flight_operator_request_running_ ||
      this->request_queue_ == nullptr || this->aeroapi_key_.empty()) {
    return false;
  }
  if (url.rfind("https://", 0) != 0 || url.size() >= MAX_URL_SIZE) {
    ESP_LOGE(TAG, "Flight-operator request rejected: invalid or oversized HTTPS URL");
    return false;
  }

  const std::string normalized_callsign = normalize_callsign_(callsign);
  const std::string normalized_operator = normalize_callsign_(operator_code);
  if (normalized_callsign.empty() || normalized_callsign.size() >= REQUEST_KEY_SIZE ||
      normalized_operator.empty() || normalized_operator.size() >= OPERATOR_CODE_SIZE) {
    ESP_LOGE(TAG, "Flight-operator request rejected: invalid callsign or operator");
    return false;
  }

  RequestMessage request{};
  request.type = RequestType::FLIGHT_OPERATOR;
  request.sequence = ++this->request_sequence_;
  std::memcpy(request.url, url.c_str(), url.size() + 1);
  copy_text_(request.request_key, sizeof(request.request_key), normalized_callsign);
  copy_text_(request.aircraft_hex, sizeof(request.aircraft_hex), aircraft_hex);
  copy_text_(request.operator_code, sizeof(request.operator_code), normalized_operator);
  if (xQueueSendToFront(this->request_queue_, &request, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Flight-operator %u rejected: worker queue is full",
             static_cast<unsigned>(request.sequence));
    return false;
  }

  this->flight_operator_request_running_ = true;
  ESP_LOGD(TAG, "Flight-operator %u queued for %s (%s)", static_cast<unsigned>(request.sequence),
           request.request_key, request.operator_code);
  return true;
}

void AsyncFlightRadarComponent::loop() {
  if (this->result_queue_ == nullptr) return;

  ResultMessage result{};
  if (xQueueReceive(this->result_queue_, &result, 0) != pdTRUE) return;

  const uint32_t step_started_us = micros();
  if (result.type == RequestType::RADAR) {
    this->request_running_ = false;
    if (result.success) {
      ESP_LOGD(TAG, "Radar %u delivered: HTTP %d, %u bytes, %u ms",
               static_cast<unsigned>(result.sequence), result.status_code,
               static_cast<unsigned>(result.response_length), static_cast<unsigned>(result.duration_ms));
      this->response_trigger_.trigger(this->response_buffer_, result.response_length, result.status_code,
                                      result.duration_ms);
    } else {
      this->failure_count_++;
      ESP_LOGW(TAG, "Radar %u failed after %u ms: %s", static_cast<unsigned>(result.sequence),
               static_cast<unsigned>(result.duration_ms), result.error);
      this->error_trigger_.trigger(std::string(result.error), result.duration_ms);
    }
  } else if (result.type == RequestType::ENRICHMENT) {
    this->enrichment_request_running_ = false;
    if (result.success) {
      ESP_LOGD(TAG, "Enrichment %u delivered for %s: HTTP %d, %u bytes, %u ms",
               static_cast<unsigned>(result.sequence), result.request_key, result.status_code,
               static_cast<unsigned>(result.response_length), static_cast<unsigned>(result.duration_ms));
      this->enrichment_response_trigger_.trigger(
          this->response_buffer_, result.response_length, result.status_code, result.duration_ms,
          std::string(result.request_key), std::string(result.aircraft_hex));
    } else {
      this->enrichment_failure_count_++;
      ESP_LOGW(TAG, "Enrichment %u failed for %s after %u ms: %s",
               static_cast<unsigned>(result.sequence), result.request_key,
               static_cast<unsigned>(result.duration_ms), result.error);
      this->enrichment_error_trigger_.trigger(std::string(result.error), result.duration_ms,
                                              std::string(result.request_key),
                                              std::string(result.aircraft_hex));
    }
  } else if (result.type == RequestType::FLIGHT_TIMES) {
    this->flight_times_request_running_ = false;
    if (result.success) {
      ESP_LOGD(TAG, "Flight-times %u delivered for %s: HTTP %d, %u bytes, %u ms",
               static_cast<unsigned>(result.sequence), result.request_key, result.status_code,
               static_cast<unsigned>(result.response_length), static_cast<unsigned>(result.duration_ms));
      this->flight_times_response_trigger_.trigger(
          this->response_buffer_, result.response_length, result.status_code, result.duration_ms,
          std::string(result.request_key), std::string(result.aircraft_hex),
          std::string(result.registration));
    } else {
      this->flight_times_failure_count_++;
      ESP_LOGW(TAG, "Flight-times %u failed for %s after %u ms: %s",
               static_cast<unsigned>(result.sequence), result.request_key,
               static_cast<unsigned>(result.duration_ms), result.error);
      this->flight_times_error_trigger_.trigger(
          std::string(result.error), result.duration_ms, std::string(result.request_key),
          std::string(result.aircraft_hex), std::string(result.registration));
    }
  } else if (result.type == RequestType::FLIGHT_OPERATOR) {
    this->flight_operator_request_running_ = false;
    if (result.success) {
      ESP_LOGD(TAG, "Flight-operator %u delivered for %s (%s): HTTP %d, %u bytes, %u ms",
               static_cast<unsigned>(result.sequence), result.request_key, result.operator_code,
               result.status_code, static_cast<unsigned>(result.response_length),
               static_cast<unsigned>(result.duration_ms));
      this->flight_operator_response_trigger_.trigger(
          this->response_buffer_, result.response_length, result.status_code, result.duration_ms,
          std::string(result.request_key), std::string(result.aircraft_hex),
          std::string(result.operator_code));
    } else {
      this->flight_times_failure_count_++;
      ESP_LOGW(TAG, "Flight-operator %u failed for %s (%s) after %u ms: %s",
               static_cast<unsigned>(result.sequence), result.request_key, result.operator_code,
               static_cast<unsigned>(result.duration_ms), result.error);
      this->flight_operator_error_trigger_.trigger(
          std::string(result.error), result.duration_ms, std::string(result.request_key),
          std::string(result.aircraft_hex), std::string(result.operator_code));
    }
  }

  this->record_main_step_duration_(step_started_us);
  const uint8_t acknowledgement = 1;
  xQueueSend(this->response_ack_queue_, &acknowledgement, 0);
}

void AsyncFlightRadarComponent::worker_task_entry_(void *parameter) {
  static_cast<AsyncFlightRadarComponent *>(parameter)->worker_task_();
}

void AsyncFlightRadarComponent::worker_task_() {
  while (!this->shutting_down_.load()) {
    RequestMessage request{};
    if (xQueueReceive(this->request_queue_, &request, portMAX_DELAY) != pdTRUE) continue;
    if (this->shutting_down_.load() || request.type == RequestType::STOP || request.sequence == 0) break;

    ResultMessage result = this->perform_request_(request);
    this->worker_stack_high_water_bytes_.store(uxTaskGetStackHighWaterMark(nullptr));
    if (xQueueSend(this->result_queue_, &result, portMAX_DELAY) != pdTRUE) continue;

    uint8_t acknowledgement = 0;
    xQueueReceive(this->response_ack_queue_, &acknowledgement, portMAX_DELAY);
  }

  this->worker_task_handle_ = nullptr;
  vTaskDelete(nullptr);
}

AsyncFlightRadarComponent::ResultMessage AsyncFlightRadarComponent::perform_request_(
    const RequestMessage &request) {
  ResultMessage result{};
  result.type = request.type;
  result.sequence = request.sequence;
  result.status_code = -1;
  copy_text_(result.request_key, sizeof(result.request_key), request.request_key);
  copy_text_(result.aircraft_hex, sizeof(result.aircraft_hex), request.aircraft_hex);
  copy_text_(result.registration, sizeof(result.registration), request.registration);
  copy_text_(result.operator_code, sizeof(result.operator_code), request.operator_code);
  const uint32_t started_ms = millis();
  const size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  this->worker_response_length_ = 0;
  this->worker_response_overflow_ = false;
  this->response_buffer_[0] = '\0';

  esp_http_client_config_t config{};
  config.url = request.url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = this->timeout_ms_;
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  config.user_agent = "ESPHome EchoEar Flight Radar";
  config.buffer_size = this->buffer_size_rx_;
  config.buffer_size_tx = this->buffer_size_tx_;
  config.keep_alive_enable = false;
  config.event_handler = AsyncFlightRadarComponent::http_event_handler_;
  config.user_data = this;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    std::snprintf(result.error, sizeof(result.error), "client initialization failed");
  } else {
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Connection", "close");
    if (request.type == RequestType::FLIGHT_TIMES || request.type == RequestType::FLIGHT_OPERATOR) {
      esp_http_client_set_header(client, "x-apikey", this->aeroapi_key_.c_str());
    }
    const esp_err_t request_result = esp_http_client_perform(client);
    result.status_code = esp_http_client_get_status_code(client);

    if (this->worker_response_overflow_) {
      std::snprintf(result.error, sizeof(result.error), "response too large");
    } else if (request_result != ESP_OK) {
      const int socket_errno = esp_http_client_get_errno(client);
      int tls_code = 0;
      int tls_flags = 0;
      const esp_err_t tls_error =
          esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
      std::snprintf(result.error, sizeof(result.error), "%s errno=%d tls=%s/%d flags=0x%X",
                    esp_err_to_name(request_result), socket_errno, esp_err_to_name(tls_error),
                    tls_code, static_cast<unsigned>(tls_flags));
    } else {
      result.success = true;
      result.response_length = this->worker_response_length_;
    }
    esp_http_client_cleanup(client);
  }

  result.duration_ms = millis() - started_ms;
  const size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const char *request_name = request.type == RequestType::RADAR
                                 ? "Radar"
                                 : (request.type == RequestType::ENRICHMENT
                                        ? "Enrichment"
                                        : (request.type == RequestType::FLIGHT_TIMES
                                               ? "Flight-times"
                                               : "Flight-operator"));
  ESP_LOGD(TAG, "%s %u worker complete in %u ms; internal heap %u -> %u bytes",
           request_name,
           static_cast<unsigned>(request.sequence), static_cast<unsigned>(result.duration_ms),
           static_cast<unsigned>(heap_before), static_cast<unsigned>(heap_after));
  return result;
}

esp_err_t AsyncFlightRadarComponent::http_event_handler_(esp_http_client_event_t *event) {
  auto *component = static_cast<AsyncFlightRadarComponent *>(event->user_data);
  if (component == nullptr || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;

  const size_t incoming = static_cast<size_t>(event->data_len);
  const size_t remaining = component->max_response_size_ - component->worker_response_length_;
  const size_t copy_size = std::min(incoming, remaining);
  if (copy_size > 0) {
    std::memcpy(component->response_buffer_ + component->worker_response_length_, event->data, copy_size);
    component->worker_response_length_ += copy_size;
    component->response_buffer_[component->worker_response_length_] = '\0';
  }
  if (copy_size != incoming) component->worker_response_overflow_ = true;
  return ESP_OK;
}

std::string AsyncFlightRadarComponent::normalize_callsign_(const std::string &callsign) {
  std::string normalized;
  normalized.reserve(std::min(callsign.size(), REQUEST_KEY_SIZE - 1));
  for (const char value : callsign) {
    char character = value;
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) {
      normalized.push_back(character);
      if (normalized.size() == REQUEST_KEY_SIZE - 1) break;
    }
  }
  return normalized;
}

void AsyncFlightRadarComponent::copy_text_(char *destination, size_t destination_size,
                                            const std::string &source) {
  if (destination_size == 0) return;
  const size_t copy_length = std::min(source.size(), destination_size - 1);
  std::memcpy(destination, source.data(), copy_length);
  destination[copy_length] = '\0';
}

EnrichmentRecord *AsyncFlightRadarComponent::find_enrichment_record_(
    const std::string &normalized_callsign) {
  for (auto &record : this->enrichment_cache_) {
    if (record.occupied && normalized_callsign == record.callsign) return &record;
  }
  return nullptr;
}

EnrichmentRecord *AsyncFlightRadarComponent::allocate_enrichment_record_(
    const std::string &normalized_callsign, const std::string &aircraft_hex) {
  EnrichmentRecord *record = this->find_enrichment_record_(normalized_callsign);
  if (record != nullptr) {
    record->last_seen_ms = millis();
    if (!aircraft_hex.empty()) copy_text_(record->aircraft_hex, sizeof(record->aircraft_hex), aircraft_hex);
    return record;
  }

  EnrichmentRecord *candidate = nullptr;
  uint32_t oldest_age = 0;
  const uint32_t now = millis();
  for (auto &entry : this->enrichment_cache_) {
    if (!entry.occupied) {
      candidate = &entry;
      break;
    }
    if (entry.status == EnrichmentStatus::PENDING) continue;
    const uint32_t age = now - entry.last_seen_ms;
    if (candidate == nullptr || age > oldest_age) {
      candidate = &entry;
      oldest_age = age;
    }
  }
  if (candidate == nullptr) return nullptr;

  *candidate = EnrichmentRecord{};
  candidate->occupied = true;
  candidate->last_seen_ms = now;
  copy_text_(candidate->callsign, sizeof(candidate->callsign), normalized_callsign);
  copy_text_(candidate->aircraft_hex, sizeof(candidate->aircraft_hex), aircraft_hex);
  return candidate;
}

void AsyncFlightRadarComponent::expire_enrichment_record_(EnrichmentRecord *record, uint32_t now) {
  if (record == nullptr || !record->occupied) return;
  const uint32_t age = now - record->status_changed_ms;
  bool expired = false;
  if (record->status == EnrichmentStatus::READY) {
    expired = age >= ENRICHMENT_READY_TTL_MS;
  } else if (record->status == EnrichmentStatus::UNAVAILABLE) {
    expired = age >= ENRICHMENT_UNAVAILABLE_TTL_MS;
  } else if (record->status == EnrichmentStatus::RETRY) {
    expired = age >= ENRICHMENT_RETRY_MS;
  }
  if (expired) record->status = EnrichmentStatus::EMPTY;
}

bool AsyncFlightRadarComponent::needs_enrichment(const std::string &callsign) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  if (normalized_callsign.empty()) return false;
  EnrichmentRecord *record = this->find_enrichment_record_(normalized_callsign);
  if (record == nullptr) return true;

  const uint32_t now = millis();
  record->last_seen_ms = now;
  this->expire_enrichment_record_(record, now);
  return record->status == EnrichmentStatus::EMPTY;
}

const EnrichmentRecord *AsyncFlightRadarComponent::find_enrichment(const std::string &callsign) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  if (normalized_callsign.empty()) return nullptr;
  EnrichmentRecord *record = this->find_enrichment_record_(normalized_callsign);
  if (record == nullptr) return nullptr;
  const uint32_t now = millis();
  record->last_seen_ms = now;
  this->expire_enrichment_record_(record, now);
  return record->status == EnrichmentStatus::EMPTY ? nullptr : record;
}

void AsyncFlightRadarComponent::mark_enrichment_pending_(const std::string &callsign,
                                                          const std::string &aircraft_hex) {
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalize_callsign_(callsign), aircraft_hex);
  if (record == nullptr) return;
  record->status = EnrichmentStatus::PENDING;
  record->status_changed_ms = millis();
}

void AsyncFlightRadarComponent::store_enrichment_ready(
    const std::string &callsign, const std::string &aircraft_hex, const std::string &airline,
    const std::string &origin_city, const std::string &origin_code, float origin_lat, float origin_lon,
    const std::string &destination_city, const std::string &destination_code, float destination_lat,
    float destination_lon, bool route_positions_valid) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalized_callsign, aircraft_hex);
  if (record == nullptr) return;
  if (!airline.empty()) copy_text_(record->airline, sizeof(record->airline), airline);
  copy_text_(record->origin_city, sizeof(record->origin_city), origin_city);
  copy_text_(record->origin_code, sizeof(record->origin_code), origin_code);
  record->origin_lat = origin_lat;
  record->origin_lon = origin_lon;
  copy_text_(record->destination_city, sizeof(record->destination_city), destination_city);
  copy_text_(record->destination_code, sizeof(record->destination_code), destination_code);
  record->destination_lat = destination_lat;
  record->destination_lon = destination_lon;
  record->route_positions_valid = route_positions_valid;
  record->status = EnrichmentStatus::READY;
  record->status_changed_ms = millis();
}

void AsyncFlightRadarComponent::store_enrichment_unavailable(const std::string &callsign,
                                                               const std::string &aircraft_hex) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalized_callsign, aircraft_hex);
  if (record == nullptr) return;
  if (record->status == EnrichmentStatus::READY) return;
  record->airline[0] = '\0';
  record->origin_city[0] = '\0';
  record->origin_code[0] = '\0';
  record->destination_city[0] = '\0';
  record->destination_code[0] = '\0';
  record->route_positions_valid = false;
  record->status = EnrichmentStatus::UNAVAILABLE;
  record->status_changed_ms = millis();
}

void AsyncFlightRadarComponent::store_enrichment_error(const std::string &callsign,
                                                         const std::string &aircraft_hex) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalized_callsign, aircraft_hex);
  if (record == nullptr) return;
  if (record->status == EnrichmentStatus::READY) return;
  record->route_positions_valid = false;
  record->status = EnrichmentStatus::RETRY;
  record->status_changed_ms = millis();
}

void AsyncFlightRadarComponent::merge_enrichment_route(
    const std::string &callsign, const std::string &aircraft_hex, const std::string &origin_city,
    const std::string &origin_code, const std::string &destination_city,
    const std::string &destination_code) {
  const std::string normalized_callsign = normalize_callsign_(callsign);
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalized_callsign, aircraft_hex);
  if (record == nullptr) return;

  if (record->origin_city[0] == '\0' && !origin_city.empty()) {
    copy_text_(record->origin_city, sizeof(record->origin_city), origin_city);
  }
  if (record->origin_code[0] == '\0' && !origin_code.empty()) {
    copy_text_(record->origin_code, sizeof(record->origin_code), origin_code);
  }
  if (record->destination_city[0] == '\0' && !destination_city.empty()) {
    copy_text_(record->destination_city, sizeof(record->destination_city), destination_city);
  }
  if (record->destination_code[0] == '\0' && !destination_code.empty()) {
    copy_text_(record->destination_code, sizeof(record->destination_code), destination_code);
  }
  if (record->origin_city[0] != '\0' && record->destination_city[0] != '\0') {
    record->status = EnrichmentStatus::READY;
    record->status_changed_ms = millis();
  }
}

void AsyncFlightRadarComponent::merge_enrichment_airline(const std::string &callsign,
                                                           const std::string &aircraft_hex,
                                                           const std::string &airline) {
  if (airline.empty()) return;
  const std::string normalized_callsign = normalize_callsign_(callsign);
  EnrichmentRecord *record = this->allocate_enrichment_record_(normalized_callsign, aircraft_hex);
  if (record == nullptr || record->airline[0] != '\0') return;
  copy_text_(record->airline, sizeof(record->airline), airline);
}

uint32_t AsyncFlightRadarComponent::get_enrichment_cache_ready_count() const {
  uint32_t ready = 0;
  for (const auto &record : this->enrichment_cache_) {
    if (record.occupied && record.status == EnrichmentStatus::READY) ready++;
  }
  return ready;
}

void AsyncFlightRadarComponent::record_main_step_duration_(uint32_t started_us) {
  const uint32_t duration_us = micros() - started_us;
  this->max_main_step_duration_us_ = std::max(this->max_main_step_duration_us_, duration_us);
  if (duration_us >= SLOW_MAIN_STEP_WARNING_US) {
    ESP_LOGW(TAG, "Slow result delivery step: %.1f ms", duration_us / 1000.0f);
  }
}

void AsyncFlightRadarComponent::on_shutdown() {
  this->shutting_down_.store(true);
  if (this->request_queue_ != nullptr) {
    RequestMessage stop_request{};
    stop_request.type = RequestType::STOP;
    xQueueSendToFront(this->request_queue_, &stop_request, 0);
  }
  // The component and its fixed buffers live until reboot. Leaving them intact
  // avoids a shutdown-time race if ESP-IDF is still returning from a request.
}

}  // namespace async_flight_radar
}  // namespace esphome
