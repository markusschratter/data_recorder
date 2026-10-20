#include "data_recorder/data_recorder_rest_node.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

#include "rclcpp/parameter_client.hpp"

namespace data_recorder
{
namespace
{

std::string diagnostic_value(
  const diagnostic_msgs::msg::DiagnosticStatus & status,
  const std::string & key)
{
  for (const auto & item : status.values) {
    if (item.key == key) {
      return item.value;
    }
  }
  return "";
}

std::vector<std::string> diagnostic_values(
  const diagnostic_msgs::msg::DiagnosticStatus & status,
  const std::string & key)
{
  std::vector<std::string> values;
  for (const auto & item : status.values) {
    if (item.key == key) {
      values.push_back(item.value);
    }
  }
  return values;
}

const char * diagnostic_level(const std::uint8_t level)
{
  switch (level) {
    case diagnostic_msgs::msg::DiagnosticStatus::OK:
      return "OK";
    case diagnostic_msgs::msg::DiagnosticStatus::WARN:
      return "WARN";
    case diagnostic_msgs::msg::DiagnosticStatus::ERROR:
      return "ERROR";
    case diagnostic_msgs::msg::DiagnosticStatus::STALE:
      return "STALE";
    default:
      return "UNKNOWN";
  }
}

const char * kind_label(const RecorderKind kind)
{
  return kind == RecorderKind::Snapshot ? "snapshot" : "continuous";
}

std::pair<std::string, std::string> parse_recorder_entry(const std::string & entry)
{
  const auto colon = entry.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= entry.size()) {
    throw std::runtime_error(
      "Recorder entry must be url_name:/namespace/node, got '" + entry + "'");
  }
  const std::string id = entry.substr(0, colon);
  const std::string node_name = entry.substr(colon + 1);
  if (id.find('/') != std::string::npos) {
    throw std::runtime_error("Recorder URL name must not contain '/': " + id);
  }
  if (node_name.front() != '/') {
    throw std::runtime_error("Recorder node name must be absolute: " + node_name);
  }
  return {id, node_name};
}

}  // namespace

DataRecordingRestNode::DataRecordingRestNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("data_recorder_rest_api", options)
{
  declare_parameter<std::string>("bind_address", "127.0.0.1");
  declare_parameter<int>("port", 8080);
  declare_parameter<double>("service_timeout_s", 10.0);
  declare_parameter<std::vector<std::string>>(
    "continuous", std::vector<std::string>{"data_recorder:/system/data_recorder"});
  declare_parameter<std::vector<std::string>>(
    "snapshot", std::vector<std::string>{"snapshot:/system/data_recorder_snapshot"});

  const auto bind_address = get_parameter("bind_address").as_string();
  const auto port = get_parameter("port").as_int();
  const auto timeout_s = get_parameter("service_timeout_s").as_double();
  if (bind_address.empty()) {
    throw std::runtime_error("bind_address is empty");
  }
  if (port < 1 || port > 65535) {
    throw std::runtime_error("port must be between 1 and 65535");
  }
  if (timeout_s <= 0.0) {
    throw std::runtime_error("service_timeout_s must be positive");
  }
  service_timeout_ = std::chrono::milliseconds(static_cast<int>(timeout_s * 1000.0));

  add_recorders(get_parameter("continuous").as_string_array(), RecorderKind::Continuous);
  add_recorders(get_parameter("snapshot").as_string_array(), RecorderKind::Snapshot);
  if (recorders_.empty()) {
    throw std::runtime_error("continuous and snapshot recorder lists are both empty");
  }

  server_ = std::make_unique<RestServer>(
    bind_address, static_cast<unsigned short>(port),
    [this](const std::string & method, const std::string & path, const std::string & query) {
      return handle_request(method, path, query);
    },
    [this](const std::string & message) {
      RCLCPP_ERROR(get_logger(), "REST server: %s", message.c_str());
    });

  RCLCPP_INFO(
    get_logger(), "REST API listening on http://%s:%ld for %zu recorder(s)",
    bind_address.c_str(), static_cast<long>(port), recorders_.size());
}

DataRecordingRestNode::~DataRecordingRestNode()
{
  if (server_) {
    server_->stop();
  }
}

void DataRecordingRestNode::on_process_shutdown()
{
  RCLCPP_INFO(get_logger(), "Shutting down");
  if (server_) {
    server_->close_listener();
  }
  std::_Exit(0);
}

void DataRecordingRestNode::add_recorders(
  const std::vector<std::string> & entries, const RecorderKind kind)
{
  const auto latched = rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable();
  for (const auto & entry : entries) {
    const auto parsed = parse_recorder_entry(entry);
    for (const auto & existing : recorders_) {
      if (existing->id == parsed.first) {
        throw std::runtime_error("Duplicate recorder URL name: " + parsed.first);
      }
    }

    auto recorder = std::make_unique<RecorderTarget>();
    recorder->id = parsed.first;
    recorder->node_name = parsed.second;
    recorder->kind = kind;
    const auto prefix = recorder->node_name + "/";
    recorder->start_client = create_client<std_srvs::srv::Trigger>(prefix + "start_recording");
    recorder->stop_client = create_client<std_srvs::srv::Trigger>(prefix + "stop_recording");
    recorder->snapshot_client = create_client<std_srvs::srv::Trigger>(prefix + "snapshot");
    recorder->parameters_client =
      std::make_shared<rclcpp::AsyncParametersClient>(this, recorder->node_name);

    RecorderTarget * raw = recorder.get();
    recorder->state_sub = create_subscription<std_msgs::msg::Bool>(
      prefix + "recording_state", latched,
      [this, raw](const std_msgs::msg::Bool::SharedPtr message) {
        on_recording_state(*raw, message);
      });
    recorder->diagnostics_sub = create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
      prefix + "diagnostics", rclcpp::QoS(10),
      [this, raw](const diagnostic_msgs::msg::DiagnosticArray::SharedPtr message) {
        on_diagnostics(*raw, message);
      });

    RCLCPP_INFO(
      get_logger(), "%s recorder '%s' -> %s",
      kind_label(kind), recorder->id.c_str(), recorder->node_name.c_str());
    recorders_.push_back(std::move(recorder));
  }
}

RecorderTarget * DataRecordingRestNode::find_recorder(const std::string & id)
{
  for (const auto & recorder : recorders_) {
    if (recorder->id == id) {
      return recorder.get();
    }
  }
  return nullptr;
}

HttpResult DataRecordingRestNode::handle_request(
  const std::string & method,
  const std::string & path,
  const std::string & query)
{
  const RecorderPath parsed = split_recorder_path(path);
  const RestRoute route = classify_route(method, parsed.action);
  if (route == RestRoute::Status && parsed.name.empty()) {
    return all_status_response();
  }
  if (route == RestRoute::Nodes && parsed.name.empty()) {
    return nodes_response();
  }
  if (route == RestRoute::MethodNotAllowed || route == RestRoute::NotFound) {
    return route_error(route);
  }
  if (parsed.name.empty()) {
    return {404, result_json(false, "Recorder name is required")};
  }

  RecorderTarget * recorder = find_recorder(parsed.name);
  if (recorder == nullptr) {
    return {404, result_json(false, "Unknown recorder '" + parsed.name + "'")};
  }

  const bool snapshot_action = route == RestRoute::Snapshot;
  const bool continuous_action = route == RestRoute::Start || route == RestRoute::Stop;
  if (snapshot_action && recorder->kind != RecorderKind::Snapshot) {
    return {404, result_json(false, "snapshot applies to snapshot recorders")};
  }
  if (continuous_action && recorder->kind != RecorderKind::Continuous) {
    return {404, result_json(false, "start and stop apply to continuous recorders")};
  }

  switch (route) {
    case RestRoute::Start:
      return call_trigger(recorder->start_client);
    case RestRoute::Stop:
      return call_trigger(recorder->stop_client);
    case RestRoute::Snapshot:
      return call_trigger(recorder->snapshot_client);
    case RestRoute::Status:
      return status_response(*recorder);
    case RestRoute::Nodes:
      return nodes_response();
    case RestRoute::Storage:
      return storage_response(*recorder);
    case RestRoute::Disk:
      return disk_response(*recorder);
    case RestRoute::SetPrefix:
      return set_prefix(*recorder, parsed.action, query);
    case RestRoute::MethodNotAllowed:
    case RestRoute::NotFound:
      return route_error(route);
  }
  return route_error(RestRoute::NotFound);
}

HttpResult DataRecordingRestNode::set_prefix(
  RecorderTarget & recorder,
  const std::string & path,
  const std::string & query)
{
  std::string prefix;
  const std::string marker = "/recording/prefix/";
  if (path.rfind(marker, 0) == 0) {
    prefix = url_decode(path.substr(marker.size()));
  } else {
    const auto found = query_parameter(query, "prefix");
    if (!found) {
      return {400, result_json(false, "prefix is required")};
    }
    prefix = *found;
  }

  if (!recorder.parameters_client->service_is_ready()) {
    return {503, result_json(false, "Recorder parameters are not available")};
  }
  auto future = recorder.parameters_client->set_parameters({rclcpp::Parameter("prefix", prefix)});
  if (future.wait_for(service_timeout_) != std::future_status::ready) {
    return {504, result_json(false, "Recorder parameters timed out")};
  }
  try {
    const auto results = future.get();
    if (results.empty()) {
      return {500, result_json(false, "prefix was not set")};
    }
    if (!results.front().successful) {
      return {200, result_json(false, results.front().reason)};
    }
  } catch (const std::exception & ex) {
    return {500, result_json(false, ex.what())};
  }
  return {200, result_json(true, "prefix set to " + prefix)};
}

HttpResult DataRecordingRestNode::call_trigger(
  const rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr & client) const
{
  if (!client->service_is_ready()) {
    return {503, result_json(false, "Recorder service is not available")};
  }
  auto request = std::make_shared<std_srvs::srv::Trigger::Request>();
  auto future = client->async_send_request(request);
  if (future.wait_for(service_timeout_) != std::future_status::ready) {
    return {504, result_json(false, "Recorder service timed out")};
  }
  const auto response = future.get();
  return {200, result_json(response->success, response->message)};
}

StatusSnapshot DataRecordingRestNode::snapshot_of(const RecorderTarget & recorder) const
{
  StatusSnapshot snapshot;
  snapshot.id = recorder.id;
  snapshot.node = recorder.node_name;
  snapshot.kind = kind_label(recorder.kind);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot.recording = recorder.recording;
    snapshot.recording_received = recorder.recording_received;
    if (recorder.diagnostics_received) {
      const auto recording_text = diagnostic_value(recorder.diagnostics, "Recording");
      if (recording_text == "true" || recording_text == "false") {
        snapshot.recording = recording_text == "true";
      }
      snapshot.recording_received = true;
      snapshot.mode = diagnostic_value(recorder.diagnostics, "Mode");
      snapshot.directory = diagnostic_value(recorder.diagnostics, "Directory");
      snapshot.information = diagnostic_value(recorder.diagnostics, "Information");
      snapshot.last_snapshot = diagnostic_value(recorder.diagnostics, "LastSnapshot");
      snapshot.recording_started = diagnostic_value(recorder.diagnostics, "RecordingStarted");
      snapshot.recording_stopped = diagnostic_value(recorder.diagnostics, "RecordingStopped");
      snapshot.recording_duration = diagnostic_value(recorder.diagnostics, "RecordingDuration");
      snapshot.last_trigger = diagnostic_value(recorder.diagnostics, "LastTrigger");
      snapshot.rate_check = diagnostic_value(recorder.diagnostics, "RateCheck");
      snapshot.free_disk_mb = diagnostic_value(recorder.diagnostics, "FreeDiskMB");
      snapshot.min_free_disk_mb = diagnostic_value(recorder.diagnostics, "MinFreeDiskMB");
      snapshot.rates = diagnostic_values(recorder.diagnostics, "Rate");
      snapshot.diagnostics_level = diagnostic_level(recorder.diagnostics.level);
      snapshot.diagnostics_message = recorder.diagnostics.message;
    }
  }
  const auto & ready_client = recorder.kind == RecorderKind::Snapshot ?
    recorder.snapshot_client : recorder.start_client;
  snapshot.services_ready = ready_client && ready_client->service_is_ready();
  return snapshot;
}

HttpResult DataRecordingRestNode::status_response(const RecorderTarget & recorder) const
{
  return {200, status_json(snapshot_of(recorder))};
}

HttpResult DataRecordingRestNode::all_status_response() const
{
  std::string json = "{\"recorders\":[";
  for (std::size_t index = 0; index < recorders_.size(); ++index) {
    if (index > 0) {
      json += ',';
    }
    json += status_json(snapshot_of(*recorders_[index]));
  }
  json += "]}";
  return {200, json};
}

HttpResult DataRecordingRestNode::nodes_response() const
{
  std::string json = "{\"nodes\":[";
  bool first = true;
  for (const auto & recorder : recorders_) {
    const StatusSnapshot status = snapshot_of(*recorder);
    if (!status.services_ready) {
      continue;
    }
    if (!first) {
      json += ',';
    }
    first = false;
    JsonObject item;
    item.add_string("id", recorder->id);
    item.add_string("node", recorder->node_name);
    item.add_string("kind", status.kind);
    item.add_string("status", "/recording/" + recorder->id + "/status");
    json += item.str();
  }
  json += "]}";
  return {200, json};
}

HttpResult DataRecordingRestNode::storage_response(const RecorderTarget & recorder) const
{
  if (!recorder.parameters_client->service_is_ready()) {
    return {503, result_json(false, "Recorder parameters are not available")};
  }
  auto future = recorder.parameters_client->get_parameters({"output_directory", "prefix", "mode"});
  if (future.wait_for(service_timeout_) != std::future_status::ready) {
    return {504, result_json(false, "Recorder parameters timed out")};
  }

  std::string output_directory;
  std::string prefix;
  std::string mode;
  try {
    for (const auto & parameter : future.get()) {
      if (parameter.get_name() == "output_directory") {
        output_directory = parameter.as_string();
      } else if (parameter.get_name() == "prefix") {
        prefix = parameter.as_string();
      } else if (parameter.get_name() == "mode") {
        mode = parameter.as_string();
      }
    }
  } catch (const std::exception & ex) {
    return {500, result_json(false, ex.what())};
  }

  std::string directory;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (recorder.diagnostics_received) {
      directory = diagnostic_value(recorder.diagnostics, "Directory");
    }
  }
  return {200, storage_json(output_directory, prefix, mode, directory)};
}

HttpResult DataRecordingRestNode::disk_response(const RecorderTarget & recorder) const
{
  if (!recorder.parameters_client->service_is_ready()) {
    return {503, result_json(false, "Recorder parameters are not available")};
  }
  auto future = recorder.parameters_client->get_parameters({"output_directory"});
  if (future.wait_for(service_timeout_) != std::future_status::ready) {
    return {504, result_json(false, "Recorder parameters timed out")};
  }

  std::string output_directory;
  try {
    const auto parameters = future.get();
    if (parameters.empty()) {
      return {500, result_json(false, "output_directory was not returned")};
    }
    output_directory = parameters.front().as_string();
  } catch (const std::exception & ex) {
    return {500, result_json(false, ex.what())};
  }

  return disk_usage_json(output_directory);
}

void DataRecordingRestNode::on_recording_state(
  RecorderTarget & recorder, const std_msgs::msg::Bool::SharedPtr message)
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  recorder.recording = message->data;
  recorder.recording_received = true;
}

void DataRecordingRestNode::on_diagnostics(
  RecorderTarget & recorder, const diagnostic_msgs::msg::DiagnosticArray::SharedPtr message)
{
  for (const auto & status : message->status) {
    if (status.hardware_id == "data_recorder" || status.name == recorder.id) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      recorder.diagnostics = status;
      recorder.diagnostics_received = true;
      return;
    }
  }
}

}  // namespace data_recorder
