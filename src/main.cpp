#include <chrono>

#include "data_recorder/data_recorder_node.hpp"

#include "rclcpp/executors/single_threaded_executor.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  {
    auto node = std::make_shared<data_recorder::DataRecordingNode>();
    // Runs before rcl_shutdown(), while the recorder can still close the bag.
    node->get_node_base_interface()->get_context()->add_pre_shutdown_callback(
      [node]() {node->on_process_shutdown();});
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (rclcpp::ok()) {
      executor.spin_once(std::chrono::milliseconds(200));
    }
    RCLCPP_INFO(node->get_logger(), "Shutting down");
    executor.remove_node(node);
  }
  rclcpp::shutdown();
  return 0;
}
