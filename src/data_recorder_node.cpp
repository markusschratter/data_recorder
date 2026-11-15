#include "data_recorder/data_recorder_node.hpp"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <iomanip>
#include <sstream>
#include <utility>

#include <functional>

#include "rclcpp/qos.hpp"
#include "rmw/rmw.h"
#include "rosbag2_cpp/info.hpp"
#include "rosbag2_cpp/writer.hpp"
#include "rosbag2_storage/storage_options.hpp"
#include "rosbag2_transport/record_options.hpp"
#include "rosbag2_transport/recorder.hpp"

namespace data_recorder
{
namespace
{

constexpr const char * kContinuous = "continuous";

rclcpp::QoS transient_local_qos()
{
  // Best effort stays compatible with both reliable and best-effort latched publishers.
  return rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().best_effort();
}

std::string clock_text(const rclcpp::Time & stamp)
{
  const std::time_t time = static_cast<std::time_t>(stamp.nanoseconds() / 1000000000LL);
  std::tm local_time{};
  localtime_r(&time, &local_time);
  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return stream.str();
}

std::string duration_text(const rclcpp::Duration & elapsed)
{
  const auto seconds = elapsed.nanoseconds() / 1000000000LL;
  const auto safe = seconds < 0 ? 0 : seconds;
  const auto hours = safe / 3600;
  const auto minutes = (safe % 3600) / 60;
  const auto secs = safe % 60;
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(2) << hours << ':'
         << std::setw(2) << minutes << ':'
         << std::setw(2) << secs;
  return stream.str();
}

std::string timestamp_string()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_r(&time, &local_time);
  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y_%m_%d-%H_%M_%S");
  return stream.str();
}

bool bag_directory_taken(const std::string & message)
{
  return message.find("already exists") != std::string::npos;
}

std::string unique_bag_name(
  const std::filesystem::path & directory,
  const std::string & prefix,
  const bool use_timestamp)
{
  std::string base = prefix.empty() ? "rosbag2" : prefix;
  if (use_timestamp) {
    base += "_" + timestamp_string();
  }

  std::string candidate = base;
  int suffix = 2;
  while (std::filesystem::exists(directory / candidate)) {
    candidate = base + "_" + std::to_string(suffix++);
  }
  return candidate;
}

const char * mode_label(SessionMode /*mode*/)
{
  return kContinuous;
}

bool parse_mode(const std::string & text, SessionMode & mode)
{
  if (text == kContinuous) {
    mode = SessionMode::Continuous;
    return true;
  }
  return false;
}

struct DiskSpace
{
  bool ok{false};
  std::uintmax_t capacity{0};
  std::uintmax_t available{0};
};

DiskSpace read_disk_space(const std::filesystem::path & directory)
{
  DiskSpace result;
  std::error_code error;
  const auto space = std::filesystem::space(directory, error);
  if (error) {
    return result;
  }
  result.ok = true;
  result.capacity = space.capacity;
  result.available = space.available;
  return result;
}

std::string megabytes_text(const std::uintmax_t bytes)
{
  const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << megabytes;
  return stream.str();
}

bool below_min_free_disk(const DiskSpace & space, const int min_free_disk_mb)
{
  if (!space.ok || min_free_disk_mb <= 0) {
    return false;
  }
  const auto minimum = static_cast<std::uintmax_t>(min_free_disk_mb) * 1024ULL * 1024ULL;
  return space.available < minimum;
}

std::vector<std::string> merged_record_topics(const RecordingConfig & config)
{
  std::vector<std::string> topics = config.record_topics;
  for (const auto & topic : config.transient_local_topics) {
    if (std::find(topics.begin(), topics.end(), topic) == topics.end()) {
      topics.push_back(topic);
    }
  }
  return topics;
}

}  // namespace

DataRecordingNode::DataRecordingNode(const rclcpp::NodeOptions & options)
: rclcpp::Node("data_recorder", options)
{
  declare_parameter<std::string>("output_directory", "/tmp");
  declare_parameter<std::string>("prefix", "");
  declare_parameter<bool>("use_timestamp", true);
  declare_parameter<bool>("autostart_recording", false);
  declare_parameter<bool>("compression", true);
  declare_parameter<bool>("record_all", false);
  declare_parameter<std::vector<std::string>>("topics", std::vector<std::string>{});
  declare_parameter<std::vector<std::string>>(
    "transient_local_topics", std::vector<std::string>{});
  declare_parameter<int>("split_size_mb", 0);
  declare_parameter<int>("split_duration_s", 0);
  declare_parameter<int>("min_free_disk_mb", 0);
  declare_parameter<int>("tolerance_topic_rate", 5);
  declare_parameter<int>("publish_state_rate", 1);
  declare_parameter<std::string>("mode", kContinuous);
  declare_parameter<std::string>("mcap_config_file", "");

  const auto latched = rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable();
  state_pub_ = create_publisher<std_msgs::msg::Bool>("~/recording_state", latched);
  diagnostics_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
    "~/diagnostics", rclcpp::QoS(10));

  start_service_ = create_service<std_srvs::srv::Trigger>(
    "~/start_recording",
    std::bind(
      &DataRecordingNode::start_recording, this,
      std::placeholders::_1, std::placeholders::_2));
  stop_service_ = create_service<std_srvs::srv::Trigger>(
    "~/stop_recording",
    std::bind(
      &DataRecordingNode::stop_recording, this,
      std::placeholders::_1, std::placeholders::_2));

  parameter_callback_ = add_on_set_parameters_callback(
    std::bind(&DataRecordingNode::on_set_parameters, this, std::placeholders::_1));

  configure_status_timer(get_parameter("publish_state_rate").as_int());
  transient_timer_ = create_wall_timer(
    std::chrono::milliseconds(200),
    std::bind(&DataRecordingNode::pump_transient_repeats, this));

  publish_status();

  const bool arm_on_start = get_parameter("autostart_recording").as_bool();
  if (arm_on_start) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    const ActionResult started = start_session_locked();
    if (!started.success) {
      RCLCPP_ERROR(get_logger(), "Autostart failed: %s", started.message.c_str());
    }
  }
}

DataRecordingNode::~DataRecordingNode()
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (session_active_) {
    stop_session_locked();
  }
}

void DataRecordingNode::on_process_shutdown()
{
  RCLCPP_INFO(get_logger(), "Shutting down");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  try {
    while (std::chrono::steady_clock::now() < deadline) {
      if (session_mutex_.try_lock()) {
        std::lock_guard<std::mutex> lock(session_mutex_, std::adopt_lock);
        if (session_active_) {
          close_bag_and_check_rates_locked();
        }
        std::_Exit(0);
      }
      if (!session_active_.load() && !finalizing_.load()) {
        std::_Exit(0);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    RCLCPP_ERROR(get_logger(), "Timed out waiting to finish the recording check");
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(get_logger(), "Shutdown failed: %s", ex.what());
  }
  std::_Exit(0);
}

std::string DataRecordingNode::validate_config(const RecordingConfig & config) const
{
  std::vector<std::string> errors;
  if (config.prefix.find('/') != std::string::npos ||
    config.prefix.find('\\') != std::string::npos)
  {
    errors.emplace_back("prefix must not contain a path separator");
  }
  if (config.split_size_mb < 0) {
    errors.emplace_back("split_size_mb must be >= 0");
  }
  if (config.split_duration_s < 0) {
    errors.emplace_back("split_duration_s must be >= 0");
  }
  if (config.min_free_disk_mb < 0) {
    errors.emplace_back("min_free_disk_mb must be >= 0");
  }
  if (config.tolerance_topic_rate < 0) {
    errors.emplace_back("tolerance_topic_rate must be >= 0");
  }
  if (config.publish_state_rate < 0) {
    errors.emplace_back("publish_state_rate must be >= 0");
  }
  for (const auto & topic : config.record_topics) {
    if (topic.empty() || topic == "/") {
      errors.emplace_back("topics contains an empty topic name");
      break;
    }
  }
  for (const auto & item : config.expected_hz) {
    if (item.second <= 0.0) {
      errors.emplace_back("expected rate for " + item.first + " must be positive");
    }
  }
  for (const auto & topic : config.transient_local_topics) {
    if (topic.empty() || topic == "/") {
      errors.emplace_back("transient_local_topics contains an empty topic name");
      break;
    }
  }

  std::ostringstream stream;
  for (std::size_t index = 0; index < errors.size(); ++index) {
    if (index > 0) {
      stream << "; ";
    }
    stream << errors[index];
  }
  return stream.str();
}

std::string DataRecordingNode::load_config(RecordingConfig & config) const
{
  // Copy out of get_parameter(). as_string()/as_string_array() refer into that
  // temporary, and a range-for does not keep the temporary alive.
  const auto output_directory = get_parameter("output_directory").as_string();
  const auto prefix = get_parameter("prefix").as_string();
  const auto mcap_config_file = get_parameter("mcap_config_file").as_string();
  const auto mode = get_parameter("mode").as_string();
  const auto topic_entries = get_parameter("topics").as_string_array();
  const auto transient_entries = get_parameter("transient_local_topics").as_string_array();

  config.output_directory = output_directory;
  config.prefix = trim_copy(prefix);
  config.use_timestamp = get_parameter("use_timestamp").as_bool();
  config.compression = get_parameter("compression").as_bool();
  config.record_all = get_parameter("record_all").as_bool();
  config.autostart_recording = get_parameter("autostart_recording").as_bool();
  config.split_size_mb = static_cast<int>(get_parameter("split_size_mb").as_int());
  config.split_duration_s = static_cast<int>(get_parameter("split_duration_s").as_int());
  config.min_free_disk_mb = static_cast<int>(get_parameter("min_free_disk_mb").as_int());
  config.tolerance_topic_rate = static_cast<int>(get_parameter("tolerance_topic_rate").as_int());
  config.publish_state_rate = static_cast<int>(get_parameter("publish_state_rate").as_int());
  config.mcap_config_file = mcap_config_file;

  std::string error;
  if (!parse_mode(mode, config.mode)) {
    error = "mode must be continuous";
  }

  config.record_topics.clear();
  config.expected_hz.clear();
  for (const auto & entry : topic_entries) {
    if (trim_copy(entry).empty()) {
      continue;
    }
    const TopicSpec spec = parse_topic_entry(entry);
    if (spec.name.empty() || spec.name == "/") {
      return "topics contains an empty topic name";
    }
    if (std::find(config.record_topics.begin(), config.record_topics.end(), spec.name) ==
      config.record_topics.end())
    {
      config.record_topics.push_back(spec.name);
    }
    if (spec.expected_hz) {
      config.expected_hz[spec.name] = *spec.expected_hz;
    }
  }

  config.transient_local_topics.clear();
  for (const auto & entry : transient_entries) {
    if (trim_copy(entry).empty()) {
      continue;
    }
    const std::string name = parse_topic_entry(entry).name;
    if (std::find(
        config.transient_local_topics.begin(),
        config.transient_local_topics.end(),
        name) == config.transient_local_topics.end())
    {
      config.transient_local_topics.push_back(name);
    }
  }
  if (!error.empty()) {
    return error;
  }
  return validate_config(config);
}

RecordingConfig DataRecordingNode::config_with_overrides(
  const std::vector<rclcpp::Parameter> & parameters,
  std::string & error) const
{
  RecordingConfig config;
  error = load_config(config);

  for (const auto & parameter : parameters) {
    const auto & name = parameter.get_name();
    if (name == "output_directory") {
      config.output_directory = parameter.as_string();
    } else if (name == "prefix") {
      config.prefix = trim_copy(parameter.as_string());
    } else if (name == "use_timestamp") {
      config.use_timestamp = parameter.as_bool();
    } else if (name == "autostart_recording") {
      config.autostart_recording = parameter.as_bool();
    } else if (name == "compression") {
      config.compression = parameter.as_bool();
    } else if (name == "record_all") {
      config.record_all = parameter.as_bool();
    } else if (name == "split_size_mb") {
      config.split_size_mb = static_cast<int>(parameter.as_int());
    } else if (name == "split_duration_s") {
      config.split_duration_s = static_cast<int>(parameter.as_int());
    } else if (name == "min_free_disk_mb") {
      config.min_free_disk_mb = static_cast<int>(parameter.as_int());
    } else if (name == "tolerance_topic_rate") {
      config.tolerance_topic_rate = static_cast<int>(parameter.as_int());
    } else if (name == "publish_state_rate") {
      config.publish_state_rate = static_cast<int>(parameter.as_int());
    } else if (name == "mcap_config_file") {
      config.mcap_config_file = parameter.as_string();
    } else if (name == "mode") {
      if (!parse_mode(parameter.as_string(), config.mode)) {
        error = "mode must be continuous";
        return config;
      }
    } else if (name == "topics") {
      config.record_topics.clear();
      config.expected_hz.clear();
      for (const auto & entry : parameter.as_string_array()) {
        if (trim_copy(entry).empty()) {
          continue;
        }
        const TopicSpec spec = parse_topic_entry(entry);
        if (spec.name.empty() || spec.name == "/") {
          error = "topics contains an empty topic name";
          return config;
        }
        if (std::find(config.record_topics.begin(), config.record_topics.end(), spec.name) ==
          config.record_topics.end())
        {
          config.record_topics.push_back(spec.name);
        }
        if (spec.expected_hz) {
          config.expected_hz[spec.name] = *spec.expected_hz;
        }
      }
    } else if (name == "transient_local_topics") {
      config.transient_local_topics.clear();
      for (const auto & entry : parameter.as_string_array()) {
        if (trim_copy(entry).empty()) {
          continue;
        }
        config.transient_local_topics.push_back(parse_topic_entry(entry).name);
      }
    }
  }

  error = validate_config(config);
  return config;
}

rcl_interfaces::msg::SetParametersResult DataRecordingNode::on_set_parameters(
  const std::vector<rclcpp::Parameter> & parameters)
{
  rcl_interfaces::msg::SetParametersResult result;
  if (session_active_) {
    result.successful = false;
    result.reason = "Recording is active. Configuration locked.";
    return result;
  }

  std::string error;
  const RecordingConfig config = config_with_overrides(parameters, error);
  if (!error.empty()) {
    result.successful = false;
    result.reason = error;
    return result;
  }

  int publish_rate = config.publish_state_rate;
  for (const auto & parameter : parameters) {
    if (parameter.get_name() == "publish_state_rate") {
      publish_rate = static_cast<int>(parameter.as_int());
      configure_status_timer(publish_rate);
    }
  }

  result.successful = true;
  return result;
}

void DataRecordingNode::configure_status_timer(const int publish_state_rate_hz)
{
  status_timer_.reset();
  const double hz = publish_state_rate_hz > 0 ? static_cast<double>(publish_state_rate_hz) : 1.0;
  const auto period = std::chrono::duration<double>(1.0 / hz);
  status_timer_ = create_wall_timer(
    std::chrono::duration_cast<std::chrono::nanoseconds>(period),
    std::bind(&DataRecordingNode::on_status_timer, this));
}

void DataRecordingNode::on_status_timer()
{
  enforce_continuous_disk_limit();
  if (get_parameter("publish_state_rate").as_int() > 0) {
    publish_status();
  }
}

void DataRecordingNode::enforce_continuous_disk_limit()
{
  if (!session_active_.load() || active_mode_ != SessionMode::Continuous) {
    return;
  }
  const int min_free_disk_mb = static_cast<int>(get_parameter("min_free_disk_mb").as_int());
  if (min_free_disk_mb <= 0) {
    return;
  }
  const DiskSpace space = read_disk_space(output_directory_);
  if (!below_min_free_disk(space, min_free_disk_mb)) {
    return;
  }

  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_active_ || active_mode_ != SessionMode::Continuous) {
    return;
  }
  RCLCPP_ERROR(
    get_logger(),
    "Free disk space %s MB is below min_free_disk_mb %d. Stopping continuous recording.",
    megabytes_text(space.available).c_str(), min_free_disk_mb);
  stop_session_locked();
}

void DataRecordingNode::start_recording(
  const std::shared_ptr<std_srvs::srv::Trigger::Request> /*request*/,
  std::shared_ptr<std_srvs::srv::Trigger::Response> response)
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  const ActionResult started = start_session_locked();
  response->success = started.success;
  response->message = started.message;
  publish_status();
}

void DataRecordingNode::stop_recording(
  const std::shared_ptr<std_srvs::srv::Trigger::Request> /*request*/,
  std::shared_ptr<std_srvs::srv::Trigger::Response> response)
{
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_active_) {
    response->success = false;
    response->message = "Not recording";
    return;
  }
  const ActionResult stopped = stop_session_locked();
  response->success = stopped.success;
  response->message = stopped.message;
  publish_status();
}

DataRecordingNode::ActionResult DataRecordingNode::start_session_locked()
{
  if (session_active_) {
    return {false, "Already recording"};
  }

  RecordingConfig config;
  const std::string config_error = load_config(config);
  if (!config_error.empty()) {
    return {false, config_error};
  }

  std::error_code filesystem_error;
  if (!std::filesystem::is_directory(config.output_directory, filesystem_error)) {
    return {false, "Recording directory not available: " + config.output_directory.string()};
  }
  if (config.mode == SessionMode::Continuous && config.min_free_disk_mb > 0) {
    const DiskSpace space = read_disk_space(config.output_directory);
    if (!space.ok) {
      return {false, "Could not read free disk space"};
    }
    if (below_min_free_disk(space, config.min_free_disk_mb)) {
      return {
        false,
        "Free disk space " + megabytes_text(space.available) +
        " MB is below min_free_disk_mb " + std::to_string(config.min_free_disk_mb)};
    }
  }

  const auto topics = merged_record_topics(config);
  if (!config.record_all && topics.empty()) {
    return {false, "No topics to record"};
  }

  const std::filesystem::path parent = config.output_directory;
  output_directory_ = config.output_directory;

  rosbag2_storage::StorageOptions storage;
  storage.storage_id = "mcap";
  if (config.split_size_mb > 0) {
    storage.max_bagfile_size =
      static_cast<uint64_t>(config.split_size_mb) * 1024ULL * 1024ULL;
  }
  if (config.split_duration_s > 0) {
    storage.max_bagfile_duration = static_cast<uint64_t>(config.split_duration_s);
  }
  if (config.compression) {
    if (!config.mcap_config_file.empty() &&
      std::filesystem::is_regular_file(config.mcap_config_file))
    {
      storage.storage_config_uri = config.mcap_config_file;
    } else {
      RCLCPP_WARN(
        get_logger(),
        "compression is true, but mcap_config_file is missing. Recording without MCAP compression.");
    }
  }

  rosbag2_transport::RecordOptions record;
  record.all_topics = config.record_all;
  record.rmw_serialization_format = rmw_get_serialization_format();
  if (!config.record_all) {
    record.topics = topics;
  }
  const auto transient_qos = transient_local_qos();
  for (const auto & topic : config.transient_local_topics) {
    record.topic_qos_profile_overrides.emplace(topic, transient_qos);
  }

  rclcpp::NodeOptions recorder_options;
  recorder_options.use_global_arguments(false);
  recorder_options.start_parameter_services(false);
  std::vector<std::string> arguments{"--ros-args"};
  bool have_arguments = false;
  const std::string ns = std::string(get_namespace());
  if (!ns.empty() && ns != "/") {
    arguments.push_back("-r");
    arguments.push_back("__ns:=" + ns);
    have_arguments = true;
  }
  if (get_parameter("use_sim_time").as_bool()) {
    arguments.push_back("-p");
    arguments.push_back("use_sim_time:=true");
    have_arguments = true;
  }
  if (have_arguments) {
    recorder_options.arguments(arguments);
  }

  active_mode_ = config.mode;
  tolerance_topic_rate_ = config.tolerance_topic_rate;
  expected_hz_ = config.expected_hz;
  transient_topics_ = config.transient_local_topics;

  std::string start_error;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const std::string bag_name = unique_bag_name(parent, config.prefix, config.use_timestamp);
    session_directory_ = parent / bag_name;
    storage.uri = session_directory_.string();
    writer_ = std::make_shared<rosbag2_cpp::Writer>();
    try {
      const std::string recorder_node_name = "_rosbag2_" + std::string(get_name());
      recorder_ = std::make_shared<rosbag2_transport::Recorder>(
        writer_, storage, record, recorder_node_name, recorder_options);
      recorder_executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
      recorder_executor_->add_node(recorder_);
      recorder_spin_stop_ = false;
      recorder_thread_ = std::thread([this]() {
        while (rclcpp::ok() && !recorder_spin_stop_.load()) {
          recorder_executor_->spin_once(std::chrono::milliseconds(200));
        }
      });
      recorder_->record();
      start_error.clear();
      break;
    } catch (const std::exception & ex) {
      start_error = ex.what();
      const auto failed_directory = session_directory_;
      reset_recorder_locked();
      if (bag_directory_taken(start_error)) {
        continue;
      }
      std::error_code remove_error;
      std::filesystem::remove_all(failed_directory, remove_error);
      return {false, std::string("Failed to start recording: ") + start_error};
    }
  }
  if (!start_error.empty()) {
    return {false, std::string("Failed to start recording: ") + start_error};
  }

  last_rate_success_ = true;
  last_rate_message_.clear();
  last_rate_failures_.clear();
  {
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    transient_cache_.clear();
  }
  subscribed_transient_.clear();
  transient_subs_.clear();
  session_active_ = true;
  recording_started_time_ = now();
  recording_started_valid_ = true;
  recording_started_text_ = clock_text(recording_started_time_);
  ensure_transient_subscriptions();

  last_bag_directory_ = session_directory_.string();
  const std::string message = "Recording to " + session_directory_.string();
  RCLCPP_INFO(get_logger(), "%s", message.c_str());
  return {true, message};
}

DataRecordingNode::ActionResult DataRecordingNode::stop_session_locked()
{
  const ActionResult result = close_bag_and_check_rates_locked();
  reset_recorder_locked();
  return result;
}

DataRecordingNode::ActionResult DataRecordingNode::close_bag_and_check_rates_locked()
{
  finalizing_.store(true);
  struct ClearFinalizing
  {
    std::atomic<bool> & flag;
    ~ClearFinalizing() {flag.store(false);}
  } clear_finalizing{finalizing_};

  const auto bag_directory = session_directory_;
  const auto expected = expected_hz_;
  const int tolerance = tolerance_topic_rate_;

  const bool was_active = session_active_;
  session_active_ = false;
  split_pending_ = false;
  if (was_active) {
    recording_started_valid_ = false;
  }

  std::string stop_error;
  if (recorder_) {
    try {
      recorder_->stop();
    } catch (const std::exception & ex) {
      stop_error = ex.what();
    }
  }

  if (!stop_error.empty()) {
    RCLCPP_ERROR(get_logger(), "Stopping the recorder failed: %s", stop_error.c_str());
  }

  ActionResult result;
  try {
    rosbag2_cpp::Info info;
    const auto metadata = info.read_metadata(bag_directory.string(), "mcap");
    const RateReport report = write_topic_rate_report(
      bag_directory, metadata, expected, static_cast<double>(tolerance));
    last_rate_success_ = report.success;
    last_rate_message_ = report.message;
    last_rate_failures_ = report.failures;
    const auto csv_path = bag_directory / "topic_rates.csv";
    if (report.message.rfind("Could not write ", 0) != 0) {
      RCLCPP_INFO(get_logger(), "Saved topic rate CSV to: %s", csv_path.string().c_str());
    }
    result.success = report.success;
    result.message = "Bag: " + bag_directory.string() + ". " + report.message;
    if (report.success) {
      RCLCPP_INFO(get_logger(), "Recording check passed: %s", report.message.c_str());
    } else {
      RCLCPP_ERROR(get_logger(), "Recording check failed: %s", report.message.c_str());
    }
  } catch (const std::exception & ex) {
    last_rate_success_ = false;
    last_rate_message_ = ex.what();
    last_rate_failures_.clear();
    result.success = false;
    result.message = "Bag: " + bag_directory.string() + ". " + ex.what();
    RCLCPP_ERROR(get_logger(), "Recording check failed: %s", ex.what());
  }

  last_bag_directory_ = bag_directory.string();
  return result;
}

void DataRecordingNode::reset_recorder_locked()
{
  recorder_spin_stop_ = true;
  if (recorder_executor_) {
    recorder_executor_->cancel();
  }
  if (recorder_thread_.joinable()) {
    recorder_thread_.join();
  }
  recorder_spin_stop_ = false;
  if (recorder_executor_ && recorder_) {
    recorder_executor_->remove_node(recorder_);
  }
  recorder_.reset();
  writer_.reset();
  recorder_executor_.reset();
  transient_subs_.clear();
  subscribed_transient_.clear();
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    transient_cache_.clear();
  }
}

void DataRecordingNode::ensure_transient_subscriptions()
{
  if (!session_active_) {
    return;
  }
  const auto graph = get_topic_names_and_types();
  const auto qos = transient_local_qos();
  for (const auto & topic : transient_topics_) {
    if (subscribed_transient_.count(topic) != 0) {
      continue;
    }
    const auto found = graph.find(topic);
    if (found == graph.end() || found->second.empty()) {
      continue;
    }
    const std::string type = found->second.front();
    auto subscription = create_generic_subscription(
      topic, type, qos,
      [this, topic, type](std::shared_ptr<rclcpp::SerializedMessage> message) {
        cache_transient(topic, type, std::move(message));
      });
    transient_subs_.push_back(subscription);
    subscribed_transient_.insert(topic);
    RCLCPP_INFO(
      get_logger(), "Caching transient-local topic %s [%s]", topic.c_str(), type.c_str());
  }
}

void DataRecordingNode::cache_transient(
  const std::string & topic,
  const std::string & type,
  const std::shared_ptr<rclcpp::SerializedMessage> & message)
{
  if (!message) {
    return;
  }
  auto copy = std::make_shared<rclcpp::SerializedMessage>(*message);
  std::lock_guard<std::mutex> lock(cache_mutex_);
  transient_cache_[topic] = CachedMessage{std::move(copy), type};
}

void DataRecordingNode::repeat_cached_messages(const rclcpp::Time & stamp)
{
  std::vector<std::pair<std::string, CachedMessage>> pending;
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    pending.reserve(transient_cache_.size());
    for (const auto & item : transient_cache_) {
      if (!item.second.message) {
        continue;
      }
      pending.emplace_back(
        item.first,
        CachedMessage{
          std::make_shared<rclcpp::SerializedMessage>(*item.second.message),
          item.second.type
        });
    }
  }
  if (!writer_) {
    return;
  }
  for (auto & item : pending) {
    try {
      writer_->write(item.second.message, item.first, item.second.type, stamp);
    } catch (const std::exception & ex) {
      RCLCPP_WARN(
        get_logger(), "Could not repeat %s: %s", item.first.c_str(), ex.what());
    }
  }
}

void DataRecordingNode::pump_transient_repeats()
{
  ensure_transient_subscriptions();
  if (!split_pending_.exchange(false)) {
    return;
  }
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_active_ || active_mode_ != SessionMode::Continuous || !writer_) {
    return;
  }
  repeat_cached_messages(now());
}

void DataRecordingNode::publish_status()
{
  std_msgs::msg::Bool state;
  state.data = session_active_.load();
  state_pub_->publish(state);

  diagnostic_msgs::msg::DiagnosticArray diagnostics;
  diagnostics.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = get_name();
  status.hardware_id = "data_recorder";

  const auto output_directory_parameter = get_parameter("output_directory").as_string();
  const int min_free_disk_mb = static_cast<int>(get_parameter("min_free_disk_mb").as_int());
  const std::filesystem::path directory = session_active_ ?
    output_directory_ :
    std::filesystem::path(output_directory_parameter);
  std::error_code error;
  const bool directory_ok = std::filesystem::is_directory(directory, error);
  const DiskSpace disk = directory_ok ? read_disk_space(directory) : DiskSpace{};

  auto add_value = [&status](const std::string & key, const std::string & value) {
      diagnostic_msgs::msg::KeyValue item;
      item.key = key;
      item.value = value;
      status.values.push_back(item);
    };

  if (!directory_ok) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = "Recording directory not available";
    add_value("Information", status.message);
  } else {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message = "OK";
    if (!session_active_ && !last_rate_success_) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      status.message = "Topic rate check failed";
    }
    SessionMode reported_mode = active_mode_;
    if (!session_active_) {
      SessionMode configured_mode = SessionMode::Continuous;
      if (parse_mode(get_parameter("mode").as_string(), configured_mode)) {
        reported_mode = configured_mode;
      }
    }
    add_value("Recording", session_active_ ? "true" : "false");
    add_value("Mode", mode_label(reported_mode));
    if (session_active_) {
      add_value("Directory", session_directory_.string());
      add_value("Information", "Recording directory: " + session_directory_.string());
    } else {
      add_value("Directory", last_bag_directory_.empty() ? "-" : last_bag_directory_);
      add_value("Information", "Not recording");
    }
    if (session_active_ && recording_started_valid_) {
      add_value("RecordingStarted", recording_started_text_);
      add_value("RecordingDuration", duration_text(now() - recording_started_time_));
    }
    add_value("FreeDiskMB", disk.ok ? megabytes_text(disk.available) : "-");
    add_value("MinFreeDiskMB", std::to_string(min_free_disk_mb));
    add_value("SplitSizeMB", std::to_string(static_cast<int>(get_parameter("split_size_mb").as_int())));
    add_value(
      "SplitDurationS",
      std::to_string(static_cast<int>(get_parameter("split_duration_s").as_int())));
    add_value("RateCheck", last_rate_message_.empty() ? "-" : (last_rate_success_ ? "OK" : "ERROR"));
    if (below_min_free_disk(disk, min_free_disk_mb)) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      status.message = "Free disk space below minimum";
    }
    const std::size_t failure_limit = std::min<std::size_t>(last_rate_failures_.size(), 20);
    for (std::size_t index = 0; index < failure_limit; ++index) {
      add_value("Rate", last_rate_failures_[index]);
    }
  }

  if (get_parameter("record_all").as_bool()) {
    add_value("Topic", "All topics");
  }
  const auto topic_entries = get_parameter("topics").as_string_array();
  for (const auto & entry : topic_entries) {
    const TopicSpec spec = parse_topic_entry(entry);
    if (spec.name.empty() || spec.name == "/") {
      continue;
    }
    std::ostringstream topic_text;
    topic_text << spec.name;
    if (spec.expected_hz) {
      topic_text << "  " << std::fixed << std::setprecision(0) << *spec.expected_hz << " Hz";
    }
    add_value("Topic", topic_text.str());
  }

  diagnostics.status.push_back(status);
  diagnostics_pub_->publish(diagnostics);
}

}  // namespace data_recorder
