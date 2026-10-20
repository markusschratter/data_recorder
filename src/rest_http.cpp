#include "data_recorder/rest_http.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace data_recorder
{
namespace
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

std::string json_escape(const std::string & text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (const unsigned char character : text) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (character < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped += hex[character >> 4];
          escaped += hex[character & 0x0f];
        } else {
          escaped += static_cast<char>(character);
        }
        break;
    }
  }
  return escaped;
}

}  // namespace

void JsonObject::add_bool(const std::string & key, const bool value)
{
  fields_.emplace_back(key, value ? "true" : "false");
}

void JsonObject::add_string(const std::string & key, const std::string & value)
{
  fields_.emplace_back(key, "\"" + json_escape(value) + "\"");
}

void JsonObject::add_int(const std::string & key, const std::uint64_t value)
{
  fields_.emplace_back(key, std::to_string(value));
}

void JsonObject::add_number(const std::string & key, const double value)
{
  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream.precision(1);
  stream << value;
  fields_.emplace_back(key, stream.str());
}

void JsonObject::add_string_array(const std::string & key, const std::vector<std::string> & values)
{
  std::string array = "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      array += ',';
    }
    array += "\"" + json_escape(values[index]) + "\"";
  }
  array += ']';
  fields_.emplace_back(key, array);
}

std::string JsonObject::str() const
{
  std::string json = "{";
  for (std::size_t index = 0; index < fields_.size(); ++index) {
    if (index > 0) {
      json += ',';
    }
    json += "\"" + json_escape(fields_[index].first) + "\":" + fields_[index].second;
  }
  json += '}';
  return json;
}

std::string result_json(const bool success, const std::string & message)
{
  JsonObject json;
  json.add_bool("success", success);
  json.add_string("message", message);
  return json.str();
}

std::string path_only(std::string target)
{
  const auto query = target.find('?');
  if (query != std::string::npos) {
    target.erase(query);
  }
  if (target.size() > 1 && target.back() == '/') {
    target.pop_back();
  }
  return target;
}

std::string query_string(const std::string & target)
{
  const auto query = target.find('?');
  if (query == std::string::npos) {
    return "";
  }
  return target.substr(query + 1);
}

std::string url_decode(std::string text)
{
  std::string decoded;
  decoded.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '+' ) {
      decoded.push_back(' ');
    } else if (text[index] == '%' && index + 2 < text.size()) {
      const auto value = std::stoi(text.substr(index + 1, 2), nullptr, 16);
      decoded.push_back(static_cast<char>(value));
      index += 2;
    } else {
      decoded.push_back(text[index]);
    }
  }
  return decoded;
}

std::optional<std::string> query_parameter(const std::string & query, const std::string & key)
{
  std::size_t begin = 0;
  while (begin < query.size()) {
    const auto end = query.find('&', begin);
    const auto field = query.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    const auto separator = field.find('=');
    const auto name = url_decode(separator == std::string::npos ? field : field.substr(0, separator));
    if (name == key) {
      return url_decode(separator == std::string::npos ? "" : field.substr(separator + 1));
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return std::nullopt;
}

double bytes_to_mb(const std::uintmax_t bytes)
{
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

RecorderPath split_recorder_path(const std::string & path)
{
  if (path == "/recording/status" || path == "/recording/nodes" || path == "/disk") {
    return {"", path};
  }
  const std::string prefix = "/recording/";
  if (path.rfind(prefix, 0) == 0) {
    const std::string rest = path.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) {
      return {rest, "/recording/status"};
    }
    const std::string id = rest.substr(0, slash);
    const std::string tail = rest.substr(slash + 1);
    if (tail == "disk") {
      return {id, "/disk"};
    }
    return {id, "/recording/" + tail};
  }
  if (path.size() < 2 || path.front() != '/') {
    return {"", path};
  }
  const auto slash = path.find('/', 1);
  if (slash == std::string::npos) {
    return {"", path};
  }
  return {path.substr(1, slash - 1), path.substr(slash)};
}

RestRoute classify_route(const std::string & method, const std::string & path)
{
  const bool command = method == "GET" || method == "POST";
  if (command && path == "/recording/start") {
    return RestRoute::Start;
  }
  if (command && path == "/recording/stop") {
    return RestRoute::Stop;
  }
  if (command && (path == "/recording/snapshot" || path == "/recording/trigger")) {
    return RestRoute::Snapshot;
  }
  if (method == "GET" && path == "/recording/status") {
    return RestRoute::Status;
  }
  if (method == "GET" && path == "/recording/nodes") {
    return RestRoute::Nodes;
  }
  if (method == "GET" && path == "/recording/storage") {
    return RestRoute::Storage;
  }
  if (method == "GET" && path == "/disk") {
    return RestRoute::Disk;
  }
  const bool prefix_path =
    path == "/recording/prefix" || path.rfind("/recording/prefix/", 0) == 0;
  if (command && prefix_path) {
    return RestRoute::SetPrefix;
  }

  const bool known =
    path == "/recording/start" || path == "/recording/stop" || path == "/recording/snapshot" ||
    path == "/recording/trigger" ||
    path == "/recording/status" || path == "/recording/nodes" || path == "/recording/storage" || path == "/disk" ||
    prefix_path;
  return known ? RestRoute::MethodNotAllowed : RestRoute::NotFound;
}

HttpResult route_error(const RestRoute route)
{
  if (route == RestRoute::MethodNotAllowed) {
    return {405, result_json(false, "Method not allowed")};
  }
  return {404, result_json(false, "Not found")};
}

std::string status_json(const StatusSnapshot & status)
{
  JsonObject json;
  json.add_string("id", status.id);
  json.add_string("node", status.node);
  json.add_string("kind", status.kind);
  json.add_bool("recording", status.recording);
  json.add_bool("recording_received", status.recording_received);
  json.add_string("mode", status.mode);
  json.add_string("directory", status.directory);
  json.add_string("information", status.information);
  json.add_string("last_snapshot", status.last_snapshot);
  json.add_string("recording_started", status.recording_started);
  json.add_string("recording_stopped", status.recording_stopped);
  json.add_string("recording_duration", status.recording_duration);
  json.add_string("last_trigger", status.last_trigger);
  json.add_string("rate_check", status.rate_check);
  json.add_string("free_disk_mb", status.free_disk_mb);
  json.add_string("min_free_disk_mb", status.min_free_disk_mb);
  json.add_string_array("rates", status.rates);
  json.add_string("diagnostics_level", status.diagnostics_level);
  json.add_string("diagnostics_message", status.diagnostics_message);
  json.add_bool("services_ready", status.services_ready);
  return json.str();
}

std::string storage_json(
  const std::string & output_directory,
  const std::string & prefix,
  const std::string & mode,
  const std::string & directory)
{
  JsonObject json;
  json.add_bool("success", true);
  json.add_string("output_directory", output_directory);
  json.add_string("prefix", prefix);
  json.add_string("mode", mode);
  json.add_string("directory", directory);
  return json.str();
}

HttpResult disk_usage_json(const std::string & output_directory)
{
  JsonObject json;
  json.add_string("path", output_directory);
  std::error_code error;
  if (!std::filesystem::exists(output_directory, error) || error) {
    json.add_bool("success", false);
    json.add_bool("exists", false);
    json.add_string("message", "Recording directory not available");
    return {200, json.str()};
  }

  const auto space = std::filesystem::space(output_directory, error);
  if (error) {
    json.add_bool("success", false);
    json.add_bool("exists", true);
    json.add_string("message", error.message());
    return {200, json.str()};
  }

  json.add_bool("success", true);
  json.add_bool("exists", true);
  json.add_int("capacity_bytes", static_cast<std::uint64_t>(space.capacity));
  json.add_int("free_bytes", static_cast<std::uint64_t>(space.free));
  json.add_int("available_bytes", static_cast<std::uint64_t>(space.available));
  json.add_number("capacity_mb", bytes_to_mb(space.capacity));
  json.add_number("free_mb", bytes_to_mb(space.free));
  json.add_number("available_mb", bytes_to_mb(space.available));
  return {200, json.str()};
}

struct RestServer::Impl
{
  using Handler = RestServer::Handler;

  Impl(
    const std::string & bind_address,
    const unsigned short port,
    Handler handler,
    std::function<void(const std::string &)> on_error)
  : handler_(std::move(handler)),
    on_error_(std::move(on_error))
  {
    acceptor_ = std::make_shared<tcp::acceptor>(context_);
    const tcp::endpoint endpoint{net::ip::make_address(bind_address), port};
    acceptor_->open(endpoint.protocol());
    acceptor_->set_option(net::socket_base::reuse_address(true));
    acceptor_->bind(endpoint);
    acceptor_->listen(net::socket_base::max_listen_connections);
    bound_port_ = acceptor_->local_endpoint().port();
    thread_ = std::thread([this]() {run();});
  }

  ~Impl()
  {
    stop();
  }

  void close_listener()
  {
    running_ = false;
    std::shared_ptr<tcp::acceptor> acceptor;
    {
      std::lock_guard<std::mutex> lock(acceptor_mutex_);
      acceptor = acceptor_;
    }
    if (acceptor) {
      boost::system::error_code error;
      acceptor->cancel(error);
      acceptor->close(error);
    }
  }

  void stop()
  {
    close_listener();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void run()
  {
    while (running_) {
      tcp::socket socket{context_};
      boost::system::error_code error;
      acceptor_->accept(socket, error);
      if (!running_ || error == net::error::operation_aborted) {
        break;
      }
      if (error) {
        on_error_(error.message());
        continue;
      }
      serve(std::move(socket));
    }
  }

  void serve(tcp::socket socket)
  {
    beast::tcp_stream stream{std::move(socket)};
    stream.expires_after(std::chrono::seconds(5));
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(64 * 1024);
    boost::system::error_code error;
    http::read(stream, buffer, parser, error);
    if (error) {
      return;
    }
    stream.expires_never();
    auto request = parser.release();
    HttpResult result;
    try {
      const std::string target = std::string(request.target());
      result = handler_(
        std::string(request.method_string()), path_only(target), query_string(target));
    } catch (const std::exception & ex) {
      result.status = 500;
      result.body = result_json(false, ex.what());
    }

    http::response<http::string_body> response{
      static_cast<http::status>(result.status), request.version()};
    response.set(http::field::server, "data_recorder_rest_api");
    response.set(http::field::content_type, "application/json");
    response.set(http::field::access_control_allow_origin, "*");
    response.keep_alive(false);
    response.body() = result.body;
    response.prepare_payload();
    stream.expires_after(std::chrono::seconds(5));
    http::write(stream, response, error);
    stream.socket().shutdown(tcp::socket::shutdown_send, error);
  }

  Handler handler_;
  std::function<void(const std::string &)> on_error_;
  net::io_context context_{1};
  std::mutex acceptor_mutex_;
  std::shared_ptr<tcp::acceptor> acceptor_;
  unsigned short bound_port_{0};
  std::atomic<bool> running_{true};
  std::thread thread_;
};

RestServer::RestServer(
  const std::string & bind_address,
  const unsigned short port,
  Handler handler,
  std::function<void(const std::string &)> on_error)
: impl_(std::make_unique<Impl>(bind_address, port, std::move(handler), std::move(on_error)))
{
}

RestServer::~RestServer() = default;

void RestServer::stop()
{
  impl_->stop();
}

void RestServer::close_listener()
{
  if (!impl_) {
    return;
  }
  impl_->close_listener();
}

unsigned short RestServer::port() const
{
  return impl_->bound_port_;
}

}  // namespace data_recorder
