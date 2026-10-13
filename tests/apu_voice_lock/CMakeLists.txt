cmake_minimum_required(VERSION 3.20)
project(apu_voice_lock_test C)
# NV1BA0_PIO_VOICE_LOCK and the VOICE_ON/RELEASE around it never wait on the
# APU frame lock and never wake the frame thread's cond.
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(apu_voice_lock_test test_main.c)
target_include_directories(apu_voice_lock_test PRIVATE ../../src ../../src/apu ../../src/nv2a)
# test_main.c defines every xa2_* function, so no host audio device is opened.
target_link_libraries(apu_voice_lock_test PRIVATE xboxrecomp)
enable_testing()
add_test(NAME apu_voice_lock COMMAND apu_voice_lock_test)
