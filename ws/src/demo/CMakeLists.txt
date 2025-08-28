cmake_minimum_required(VERSION 3.5)
project(demo)

find_package(ament_cmake REQUIRED)
find_package(rcl REQUIRED)
find_package(rcutils REQUIRED)
find_package(std_msgs REQUIRED)
find_package(example_interfaces REQUIRED)

add_executable(dual_pubsub src/dual_pubsub.cpp)
target_link_libraries(dual_pubsub PUBLIC
  ${std_msgs_TARGETS}
  rcl::rcl
  rcutils::rcutils
)

install(
  TARGETS
    dual_pubsub
  DESTINATION lib/${PROJECT_NAME})

ament_package()
