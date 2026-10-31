cmake_minimum_required(VERSION 3.24)

project(speak-to-computer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(X11 REQUIRED)
include(CTest)

add_executable(speak-to-computer
    assets/icons.qrc
    src/AppSettings.cpp
    src/AppSettings.h
    src/AudioRecorder.cpp
    src/AudioRecorder.h
    src/ClipboardPaster.cpp
    src/ClipboardPaster.h
    src/FileHandoff.cpp
    src/FileHandoff.h
    src/OverlayWidget.cpp
    src/OverlayWidget.h
    src/PreferencesDialog.cpp
    src/PreferencesDialog.h
    src/BuzzAsrRunner.cpp
    src/BuzzAsrRunner.h
    src/ParakeetRunner.cpp
    src/ParakeetRunner.h
    src/RealtimeSidecar.cpp
    src/RealtimeSidecar.h
    src/VadEndpointDetector.cpp
    src/VadEndpointDetector.h
    src/WebRtcVad.cpp
    src/WebRtcVad.h
    src/TranscriptCleaner.cpp
    src/TranscriptCleaner.h
    src/SpeakToComputerApp.cpp
    src/SpeakToComputerApp.h
    src/WavWriter.cpp
    src/WavWriter.h
    src/WhisperRunner.cpp
    src/WhisperRunner.h
    src/WakeWordListener.cpp
    src/WakeWordListener.h
    src/X11Hotkey.cpp
    src/X11Hotkey.h
    src/main.cpp
    src/activation_sound.wav
    src/end_sound.wav
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
    Qt6::Network
    ${X11_LIBRARIES}
)

add_custom_command(TARGET speak-to-computer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/src/activation_sound.wav
    $<TARGET_FILE_DIR:speak-to-computer>
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/src/end_sound.wav
    $<TARGET_FILE_DIR:speak-to-computer>
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:speak-to-computer>/sounds/soft
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/soft $<TARGET_FILE_DIR:speak-to-computer>/sounds/soft
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/openwakeword_sidecar.py
    $<TARGET_FILE_DIR:speak-to-computer>
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-openwakeword-runtime.sh
    $<TARGET_FILE_DIR:speak-to-computer>
    COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:speak-to-computer>/openwakeword
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/openwakeword/requirements.txt
    $<TARGET_FILE_DIR:speak-to-computer>/openwakeword
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/openwakeword/smoke_test.py
    $<TARGET_FILE_DIR:speak-to-computer>/openwakeword
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(speak-to-computer-tests
        assets/icons.qrc
        src/AppSettings.cpp
        src/AppSettings.h
        src/AudioRecorder.cpp
        src/AudioRecorder.h
        src/FileHandoff.cpp
        src/FileHandoff.h
        src/ClipboardPaster.cpp
        src/ClipboardPaster.h
        src/BuzzAsrRunner.cpp
        src/BuzzAsrRunner.h
        src/ParakeetRunner.cpp
        src/ParakeetRunner.h
        src/OverlayWidget.cpp
        src/OverlayWidget.h
        src/PreferencesDialog.cpp
        src/PreferencesDialog.h
        src/TranscriptCleaner.cpp
        src/TranscriptCleaner.h
        src/VadEndpointDetector.cpp
        src/VadEndpointDetector.h
        src/WebRtcVad.cpp
        src/WebRtcVad.h
        src/WavWriter.cpp
        src/WavWriter.h
        src/WhisperRunner.cpp
        src/WhisperRunner.h
        tests/CoreBehaviorTest.cpp
    )

    target_include_directories(speak-to-computer-tests PRIVATE src)
    target_link_libraries(speak-to-computer-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)

    add_test(NAME speak-to-computer-core COMMAND speak-to-computer-tests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME buzzasr-sidecar COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/BuzzAsrSidecarTest.py)
    add_test(NAME parakeet-sidecar COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ParakeetSidecarTest.py)
    find_program(XVFB_RUN xvfb-run)
    find_program(XDOTOOL xdotool)
    if(XVFB_RUN AND XDOTOOL)
        add_executable(speak-to-computer-x11-tests src/X11Hotkey.cpp src/X11Hotkey.h tests/X11HotkeyTest.cpp)
        target_include_directories(speak-to-computer-x11-tests PRIVATE src ${X11_INCLUDE_DIR})
        target_link_libraries(speak-to-computer-x11-tests PRIVATE Qt6::Core Qt6::Test ${X11_LIBRARIES})
        add_test(NAME x11-hotkey COMMAND ${XVFB_RUN} -a $<TARGET_FILE:speak-to-computer-x11-tests>)
    endif()
endif()
