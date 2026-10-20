#include "data_recorder/topic_rate_report.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

#include "data_recorder/topic_spec.hpp"

namespace data_recorder
{
namespace
{

std::string csv_number(double value, int precision)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

}  // namespace

RateReport write_topic_rate_report(
  const std::filesystem::path & bag_directory,
  const rosbag2_storage::BagMetadata & metadata,
  const std::map<std::string, double> & expected_hz,
  const double tolerance_pct)
{
  RateReport report;
  const double duration_sec = std::chrono::duration<double>(metadata.duration).count();

  std::map<std::string, size_t> recorded;
  for (const auto & topic : metadata.topics_with_message_count) {
    if (!topic.topic_metadata.name.empty()) {
      recorded[topic.topic_metadata.name] = topic.message_count;
    }
  }

  std::vector<std::string> names;
  names.reserve(recorded.size() + expected_hz.size());
  for (const auto & item : recorded) {
    names.push_back(item.first);
  }
  for (const auto & item : expected_hz) {
    if (recorded.find(item.first) == recorded.end()) {
      names.push_back(item.first);
    }
  }
  std::sort(names.begin(), names.end());

  std::error_code error;
  std::filesystem::create_directories(bag_directory, error);
  const auto csv_path = bag_directory / "topic_rates.csv";
  std::ofstream csv(csv_path);
  if (!csv) {
    report.success = false;
    report.message = "Could not write " + csv_path.string();
    return report;
  }

  csv << "topic,message_count,duration_sec,recorded_hz,expected_hz,tolerance_pct,status\n";
  for (const auto & name : names) {
    const size_t count = recorded.count(name) == 0 ? 0 : recorded.at(name);
    const double recorded_hz = duration_sec > 0.0 ? static_cast<double>(count) / duration_sec : 0.0;
    const auto expected = expected_hz.find(name);
    const bool checked = expected != expected_hz.end();
    FreqStatus status = FreqStatus::Na;
    if (checked) {
      if (duration_sec <= 0.0) {
        status = FreqStatus::Low;
      } else {
        status = frequency_status(expected->second, recorded_hz, tolerance_pct);
      }
    }

    csv << name << ','
        << count << ','
        << csv_number(duration_sec, 6) << ','
        << csv_number(recorded_hz, 4) << ',';
    if (checked) {
      csv << csv_number(expected->second, 4) << ','
          << csv_number(tolerance_pct, 4) << ',';
    } else {
      csv << ",,";
    }
    csv << freq_status_label(status) << '\n';

    if (checked && status != FreqStatus::Ok) {
      std::ostringstream issue;
      issue << name << ',' << freq_status_label(status) << ','
            << csv_number(recorded_hz, 4) << "Hz,"
            << csv_number(expected->second, 4) << "Hz";
      report.failures.push_back(issue.str());
    }
  }

  if (duration_sec <= 0.0 && !expected_hz.empty()) {
    report.success = false;
    report.message = "Invalid or zero recording duration in metadata.";
    return report;
  }

  if (report.failures.empty()) {
    report.success = true;
    report.message = expected_hz.empty() ?
      "Recording finished." :
      "All topic frequencies are within the expected range.";
    return report;
  }

  report.success = false;
  std::ostringstream message;
  for (std::size_t index = 0; index < report.failures.size(); ++index) {
    if (index > 0) {
      message << "; ";
    }
    message << report.failures[index];
  }
  message << ". Topics with no issues are not shown.";
  report.message = message.str();
  return report;
}

}  // namespace data_recorder
