add_executable(gv_midi_test_sender main.cpp)
target_link_libraries(gv_midi_test_sender PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
