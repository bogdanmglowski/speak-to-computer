cmake_minimum_required(VERSION 3.24)

project(speak-to-computer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(X11 REQUIRED)
include(CTest)

add_executable(speak-to-computer
    src/AppSettings.cpp
    src/AppSettings.h
    src/AudioRecorder.cpp
    src/AudioRecorder.h
    src/ClipboardPaster.cpp
    src/ClipboardPaster.h
    src/OverlayWidget.cpp
    src/OverlayWidget.h
    src/TranscriptCleaner.cpp
    src/TranscriptCleaner.h
    src/SpeakToComputerApp.cpp
    src/SpeakToComputerApp.h
    src/WavWriter.cpp
    src/WavWriter.h
    src/WhisperRunner.cpp
    src/WhisperRunner.h
    src/X11Hotkey.cpp
    src/X11Hotkey.h
    src/main.cpp
)

target_compile_definitions(speak-to-computer PRIVATE
    SPEAK_TO_COMPUTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(speak-to-computer PRIVATE
    src
    ${X11_INCLUDE_DIR}
)

target_link_libraries(speak-to-computer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${X11_LIBRARIES}
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(speak-to-computer-tests
        src/AppSettings.cpp
        src/AppSettings.h
        src/AudioRecorder.cpp
        src/AudioRecorder.h
        src/TranscriptCleaner.cpp
        src/TranscriptCleaner.h
        src/WavWriter.cpp
        src/WavWriter.h
        src/WhisperRunner.cpp
        src/WhisperRunner.h
        tests/CoreBehaviorTest.cpp
    )

    target_include_directories(speak-to-computer-tests PRIVATE src)
    target_link_libraries(speak-to-computer-tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME speak-to-computer-core COMMAND speak-to-computer-tests)
endif()
