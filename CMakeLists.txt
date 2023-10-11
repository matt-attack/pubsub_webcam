cmake_minimum_required (VERSION 3.6)

project ("pubsub_video")

find_package(pubsub REQUIRED)
find_package(pubsub_cpp REQUIRED)

# Build examples
if(UNIX)
add_executable(camera "src/camera.cpp")
else()
add_executable(camera "src/camera.cpp" "src/windows_support.cpp")
endif()
target_include_directories(camera PUBLIC include/ ${pubsub_INCLUDE_DIRS})
target_link_libraries(camera ${pubsub_LIBRARIES} ${pubsub_cpp_LIBRARIES})
set_property(TARGET camera PROPERTY CXX_STANDARD 11)
