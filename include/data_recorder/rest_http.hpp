#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace data_recorder
{

struct HttpResult
{
  int status{200};
  std::string body;
};

class JsonObject
{
public:
  void add_bool(const std::string & key, bool value);
  void add_string(const std::string & key, const std::string & value);
  void add_int(const std::string & key, std::uint64_t value);
  void add_number(const std::string & key, double value);
  void add_string_array(const std::string & key, const std::vector<std::string> & values);
  std::string str() const;

private:
  std::vector<std::pair<std::string, std::string>> fields_;
};

std::string result_json(bool success, const std::string & message);
std::string path_only(std::string target);
std::string query_string(const std::string & target);
std::string url_decode(std::string text);
std::optional<std::string> query_parameter(const std::string & query, const std::string & key);
double bytes_to_mb(std::uintmax_t bytes);

enum class RestRoute
{
  Start,
  Stop,
  Snapshot,
  Status,
  Nodes,
  Storage,
  Disk,
  SetPrefix,
  MethodNotAllowed,
  NotFound,
};

RestRoute classify_route(const std::string & method, const std::string & path);
HttpResult route_error(RestRoute route);

struct RecorderPath
{
  std::string name;
  std::string action;
};

RecorderPath split_recorder_path(const std::string & path);

struct StatusSnapshot
{
  std::string id;
  std::string node;
  std::string kind;
  bool recording{false};
  bool recording_received{false};
  std::string mode;
  std::string directory;
  std::string information;
  std::string last_snapshot;
  std::string recording_started{"-"};
  std::string recording_stopped{"-"};
  std::string recording_duration{"-"};
  std::string last_trigger{"-"};
  std::string rate_check;
  std::string free_disk_mb;
  std::string min_free_disk_mb;
  std::vector<std::string> rates;
  std::string diagnostics_level;
  std::string diagnostics_message{"No diagnostics received"};
  bool services_ready{false};
};

std::string status_json(const StatusSnapshot & status);
std::string storage_json(
  const std::string & output_directory,
  const std::string & prefix,
  const std::string & mode,
  const std::string & directory);
HttpResult disk_usage_json(const std::string & output_directory);

class RestServer
{
public:
  using Handler = std::function<HttpResult(
    const std::string &, const std::string &, const std::string &)>;

  RestServer(
    const std::string & bind_address,
    unsigned short port,
    Handler handler,
    std::function<void(const std::string &)> on_error);
  ~RestServer();

  RestServer(const RestServer &) = delete;
  RestServer & operator=(const RestServer &) = delete;

  void stop();
  void close_listener();
  unsigned short port() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace data_recorder
