#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "data_recorder/rest_http.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace data_recorder
{

enum class RecorderKind
{
  Continuous,
  Snapshot
};

struct RecorderTarget
{
  std::string id;
  std::string node_name;
  RecorderKind kind{RecorderKind::Continuous};

  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr start_client;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr stop_client;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr snapshot_client;
  rclcpp::AsyncParametersClient::SharedPtr parameters_client;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr state_sub;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_sub;

  bool recording{false};
  bool recording_received{false};
  bool diagnostics_received{false};
  diagnostic_msgs::msg::DiagnosticStatus diagnostics;
};

class DataRecordingRestNode : public rclcpp::Node
{
public:
  explicit DataRecordingRestNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~DataRecordingRestNode() override;

  void on_process_shutdown();

private:
  HttpResult handle_request(
    const std::string & method, const std::string & path, const std::string & query);
  HttpResult set_prefix(
    RecorderTarget & recorder, const std::string & path, const std::string & query);
  HttpResult call_trigger(
    const rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr & client) const;
  HttpResult status_response(const RecorderTarget & recorder) const;
  HttpResult all_status_response() const;
  HttpResult nodes_response() const;
  HttpResult storage_response(const RecorderTarget & recorder) const;
  HttpResult disk_response(const RecorderTarget & recorder) const;
  StatusSnapshot snapshot_of(const RecorderTarget & recorder) const;

  void on_recording_state(RecorderTarget & recorder, const std_msgs::msg::Bool::SharedPtr message);
  void on_diagnostics(
    RecorderTarget & recorder, const diagnostic_msgs::msg::DiagnosticArray::SharedPtr message);

  RecorderTarget * find_recorder(const std::string & id);
  void add_recorders(const std::vector<std::string> & entries, RecorderKind kind);

  std::chrono::milliseconds service_timeout_{10000};
  std::vector<std::unique_ptr<RecorderTarget>> recorders_;
  mutable std::mutex state_mutex_;
  std::unique_ptr<RestServer> server_;
};

}  // namespace data_recorder
