#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data_recorder/topic_rate_report.hpp"
#include "data_recorder/topic_spec.hpp"

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/generic_subscription.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rosbag2_cpp/bag_events.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace rosbag2_cpp
{
class Writer;
}

namespace rosbag2_transport
{
class Recorder;
}

namespace data_recorder
{

enum class SessionMode
{
  Continuous,
  Snapshot
};

struct RecordingConfig
{
  std::filesystem::path output_directory{"/tmp"};
  std::string prefix;
  bool use_timestamp{true};
  bool compression{true};
  bool record_all{false};
  bool autostart_recording{false};
  SessionMode mode{SessionMode::Continuous};
  int split_size_mb{0};
  int split_duration_s{0};
  int min_free_disk_mb{0};
  int tolerance_topic_rate{5};
  int publish_state_rate{1};
  int snapshot_cache_mb{1024};
  int snapshot_post_trigger_s{10};
  std::string mcap_config_file;
  std::vector<std::string> record_topics;
  std::map<std::string, double> expected_hz;
  std::vector<std::string> transient_local_topics;
};

class DataRecordingNode : public rclcpp::Node
{
public:
  explicit DataRecordingNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~DataRecordingNode() override;

  void on_process_shutdown();

private:
  struct ActionResult
  {
    bool success{false};
    std::string message;
  };

  struct CachedMessage
  {
    std::shared_ptr<rclcpp::SerializedMessage> message;
    std::string type;
  };

  std::string validate_config(const RecordingConfig & config) const;
  std::string load_config(RecordingConfig & config) const;
  RecordingConfig config_with_overrides(
    const std::vector<rclcpp::Parameter> & parameters,
    std::string & error) const;

  rcl_interfaces::msg::SetParametersResult on_set_parameters(
    const std::vector<rclcpp::Parameter> & parameters);

  void start_recording(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response);
  void stop_recording(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response);
  void snapshot(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response);

  ActionResult start_session_locked();
  ActionResult close_bag_and_check_rates_locked();
  ActionResult stop_session_locked();
  ActionResult store_snapshot_locked();
  ActionResult store_snapshot_now_locked();
  ActionResult begin_snapshot_follow_locked(int post_trigger_s);
  void finish_snapshot_follow_locked();
  std::filesystem::path take_snapshot_file_locked();

  void reset_recorder_locked();
  void ensure_transient_subscriptions();
  void cache_transient(
    const std::string & topic,
    const std::string & type,
    const std::shared_ptr<rclcpp::SerializedMessage> & message);
  void repeat_cached_messages(const rclcpp::Time & stamp);
  void on_bag_split(rosbag2_cpp::bag_events::BagSplitInfo & info);
  void pump_transient_repeats();
  void on_status_timer();
  void enforce_continuous_disk_limit();
  void publish_status();
  void configure_status_timer(int publish_state_rate_hz);

  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr start_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr snapshot_service_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr state_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_pub_;
  rclcpp::TimerBase::SharedPtr status_timer_;
  rclcpp::TimerBase::SharedPtr transient_timer_;
  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameter_callback_;

  std::mutex session_mutex_;
  std::shared_ptr<rosbag2_cpp::Writer> writer_;
  std::shared_ptr<rosbag2_transport::Recorder> recorder_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> recorder_executor_;
  std::thread recorder_thread_;
  std::atomic<bool> recorder_spin_stop_{false};

  std::atomic<bool> session_active_{false};
  std::atomic<bool> finalizing_{false};
  SessionMode active_mode_{SessionMode::Continuous};
  std::filesystem::path session_directory_;
  std::filesystem::path output_directory_;
  int tolerance_topic_rate_{5};
  std::map<std::string, double> expected_hz_;
  std::vector<std::string> transient_topics_;
  std::string last_bag_directory_;
  std::string last_snapshot_directory_;
  bool snapshot_follow_active_{false};
  rclcpp::TimerBase::SharedPtr snapshot_follow_timer_;
  bool last_rate_success_{true};
  std::string last_rate_message_;
  std::vector<std::string> last_rate_failures_;
  bool recording_started_valid_{false};
  rclcpp::Time recording_started_time_{0, 0, RCL_ROS_TIME};
  std::string recording_started_text_{"-"};
  std::string last_trigger_text_{"-"};

  std::mutex closed_file_mutex_;
  std::string last_closed_file_;
  std::string last_opened_file_;
  std::atomic<bool> split_pending_{false};

  std::mutex cache_mutex_;
  std::unordered_map<std::string, CachedMessage> transient_cache_;
  std::unordered_set<std::string> subscribed_transient_;
  std::vector<rclcpp::GenericSubscription::SharedPtr> transient_subs_;
};

}  // namespace data_recorder
