#include <chrono>

#include "data_recorder/data_recorder_rest_node.hpp"

#include "rclcpp/executors/single_threaded_executor.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<data_recorder::DataRecordingRestNode>();
    rclcpp::on_shutdown([node]() {node->on_process_shutdown();});
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (rclcpp::ok()) {
      executor.spin_once(std::chrono::milliseconds(200));
    }
    RCLCPP_INFO(node->get_logger(), "Shutting down");
    executor.remove_node(node);
  } catch (const std::exception & ex) {
    RCLCPP_FATAL(rclcpp::get_logger("data_recorder_rest_api"), "%s", ex.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
