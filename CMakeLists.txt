cmake_minimum_required(VERSION 3.15)

project(Webrtc VERSION 1.1
		DESCRIPTION "Webrtc project"
		LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(STREAMER_SOURCES
	main.cpp
	rtp_parse.cpp
	helpers.cpp
	helpers.hpp
	ArgParser.cpp
)

add_subdirectory(libdatachannel)
add_subdirectory(libdatachannel/deps/json EXCLUDE_FROM_ALL)

add_executable(streamer_test ${STREAMER_SOURCES})

set_target_properties(streamer_test PROPERTIES
	CXX_STANDARD 20
	OUTPUT_NAME streamer_test)

target_link_libraries(streamer_test LibDataChannel::LibDataChannel nlohmann_json)

configure_file(Data.txt ${PROJECT_BINARY_DIR} COPYONLY)

