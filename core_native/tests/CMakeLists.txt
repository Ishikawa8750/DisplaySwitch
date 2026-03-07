cmake_minimum_required(VERSION 3.20)

add_executable(test_edid_parser test_edid_parser.cpp)
target_link_libraries(test_edid_parser PRIVATE displayswitch_core)

add_test(NAME edid_parser_test COMMAND test_edid_parser)
