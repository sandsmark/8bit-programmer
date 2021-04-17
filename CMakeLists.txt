cmake_minimum_required(VERSION 3.5)

project(8bit-programmer LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check https://doc.qt.io/qt/deployment-android.html for more information.
# They need to be set before the find_package( ...) calls below.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets SerialPort REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets SerialPort REQUIRED)
find_package(Threads)

set(PROJECT_SOURCES
        main.cpp
        Editor.cpp
        Editor.h
        Modem.h
        Modem.cpp
        AudioBuffer.cpp
        AudioBuffer.h

        MiniAudio_impl.cpp
)

if(MSVC)
    add_definitions(/D_USE_MATH_DEFINES)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(8bit-programmer
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(8bit-programmer SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(8bit-programmer
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(8bit-programmer
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::SerialPort
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
