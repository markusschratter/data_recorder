#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace data_recorder
{

enum class FreqStatus
{
  Ok,
  Low,
  High,
  Na
};

struct TopicSpec
{
  std::string name;
  std::optional<double> expected_hz;
};

inline std::string trim_copy(std::string_view text)
{
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

inline bool is_number(std::string_view text)
{
  if (text.empty()) {
    return false;
  }
  bool dot = false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch == '.') {
      if (dot || index == 0 || index + 1 == text.size()) {
        return false;
      }
      dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
      return false;
    }
  }
  return true;
}

inline std::string normalize_topic_name(std::string name)
{
  name = trim_copy(name);
  if (name.empty()) {
    return name;
  }
  if (name.front() != '/') {
    name.insert(name.begin(), '/');
  }
  return name;
}

// "/topic" is recorded only. "/topic:100" is recorded and checked at 100 Hz.
// The split is the last colon, so a non-numeric suffix stays part of the name.
inline TopicSpec parse_topic_entry(const std::string & entry)
{
  const std::string trimmed = trim_copy(entry);
  TopicSpec spec;
  const auto colon = trimmed.rfind(':');
  if (colon == std::string::npos) {
    spec.name = normalize_topic_name(trimmed);
    return spec;
  }

  const std::string rate_text = trim_copy(trimmed.substr(colon + 1));
  if (!is_number(rate_text)) {
    spec.name = normalize_topic_name(trimmed);
    return spec;
  }

  spec.name = normalize_topic_name(trimmed.substr(0, colon));
  spec.expected_hz = std::stod(rate_text);
  return spec;
}

inline const char * freq_status_label(FreqStatus status)
{
  switch (status) {
    case FreqStatus::Ok:
      return "OK";
    case FreqStatus::Low:
      return "LOW";
    case FreqStatus::High:
      return "HIGH";
    case FreqStatus::Na:
      return "N/A";
  }
  return "N/A";
}

inline FreqStatus frequency_status(double expected_hz, double recorded_hz, double tolerance_pct)
{
  const double tolerance = expected_hz * (tolerance_pct / 100.0);
  if (recorded_hz > expected_hz + tolerance) {
    return FreqStatus::High;
  }
  if (recorded_hz < expected_hz - tolerance) {
    return FreqStatus::Low;
  }
  return FreqStatus::Ok;
}

}  // namespace data_recorder
