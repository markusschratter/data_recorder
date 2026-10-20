cmake_minimum_required(VERSION 3.14)
project(data_recorder)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(Boost REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rmw REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(Threads REQUIRED)

function(data_recorder_link_boost target)
  if(TARGET Boost::headers)
    target_link_libraries(${target} Boost::headers Threads::Threads)
  else()
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${target} Threads::Threads)
  endif()
endfunction()

add_executable(data_recorder
  src/main.cpp
  src/data_recorder_node.cpp
  src/topic_rate_report.cpp
)
target_include_directories(data_recorder PRIVATE include)
ament_target_dependencies(data_recorder
  diagnostic_msgs
  rclcpp
  rmw
  rosbag2_cpp
  rosbag2_storage
  rosbag2_transport
  std_msgs
  std_srvs
)
target_link_libraries(data_recorder Threads::Threads)

add_executable(data_recorder_rest
  src/rest_main.cpp
  src/data_recorder_rest_node.cpp
  src/rest_http.cpp
)
target_include_directories(data_recorder_rest PRIVATE include)
ament_target_dependencies(data_recorder_rest
  diagnostic_msgs
  rclcpp
  std_msgs
  std_srvs
)
data_recorder_link_boost(data_recorder_rest)

install(TARGETS data_recorder data_recorder_rest
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
