add_library(evox_test_support INTERFACE)
target_include_directories(evox_test_support INTERFACE ${CMAKE_SOURCE_DIR}/tools ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(evox_tests
    TestMain.cpp
    test_conditioning.cpp
    test_pitch.cpp
    test_engine.cpp
    test_correction.cpp
    test_oversampling.cpp)

target_link_libraries(evox_tests PRIVATE evox_dsp evox_test_support)
find_package(Threads REQUIRED)
target_link_libraries(evox_tests PRIVATE Threads::Threads)

add_test(NAME evox_tests COMMAND evox_tests)
