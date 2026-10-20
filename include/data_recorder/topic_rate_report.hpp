#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "rosbag2_storage/bag_metadata.hpp"

namespace data_recorder
{

struct RateReport
{
  bool success{true};
  std::string message;
  std::vector<std::string> failures;
};

// Writes topic_rates.csv next to the bag and compares topics that declared an expected Hz.
RateReport write_topic_rate_report(
  const std::filesystem::path & bag_directory,
  const rosbag2_storage::BagMetadata & metadata,
  const std::map<std::string, double> & expected_hz,
  double tolerance_pct);

}  // namespace data_recorder
