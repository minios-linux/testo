cmake_minimum_required(VERSION 3.0)

file(GLOB SRCS *.cpp)
list(REMOVE_ITEM SRCS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

add_library(nn STATIC ${SRCS})
if (TARGET onnxruntime::onnxruntime)
	target_link_libraries(nn stb onnxruntime::onnxruntime)
else()
	target_link_libraries(nn stb onnxruntime)
endif()
if(UNIX)
	target_link_libraries(nn pthread)
endif()
if(WIN32)
	target_link_libraries(nn winapi)
endif()

add_executable(nn_demo main.cpp)
target_link_libraries(nn_demo nn)

file(GLOB NN_UNIT_TEST_SRCS "unit_tests/*.cpp")
add_executable(nn_unit_tests ${NN_UNIT_TEST_SRCS})
target_link_libraries(nn_unit_tests nn)
