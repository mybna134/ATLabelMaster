cmake_minimum_required(VERSION 3.16)

project(LabelMaster VERSION 1.2.2 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/labelmaster/src)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/labelmaster/src/ui
)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    ${SRC_PATH}/*.cpp
    ${SRC_PATH}/*.hpp
    ${SRC_PATH}/*.ui
)

find_package(OpenCV REQUIRED)
find_package(OpenVINO REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Svg)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} ${SRC_FILES})

set(LABELMASTER_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "ATLabelMaster application version")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    LABELMASTER_VERSION="${LABELMASTER_APP_VERSION}"
    LABELMASTER_ASSETS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/labelmaster"
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${SRC_PATH}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    ${OpenCV_LIBS}
    openvino::runtime
    Threads::Threads
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(APP_SHARE_DIR "${CMAKE_INSTALL_DATADIR}/labelmaster")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    install(DIRECTORY resources/ DESTINATION ${APP_SHARE_DIR}/resources)
endif()

install(DIRECTORY assets/themes/ DESTINATION ${APP_SHARE_DIR}/themes)
install(DIRECTORY assets/icons/ DESTINATION ${APP_SHARE_DIR}/icons)
install(DIRECTORY assets/label/ DESTINATION ${APP_SHARE_DIR}/label)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/models")
    install(DIRECTORY assets/models/ DESTINATION ${APP_SHARE_DIR}/models)
endif()

option(LABELMASTER_FLATPAK "Install Flatpak desktop integration metadata" OFF)
if(LABELMASTER_FLATPAK)
    set(LABELMASTER_FLATPAK_ID "io.github.mybna134.ATLabelMaster")

    install(
        FILES "packaging/flatpak/${LABELMASTER_FLATPAK_ID}.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(
        FILES "packaging/flatpak/${LABELMASTER_FLATPAK_ID}.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
    install(
        FILES "packaging/flatpak/${LABELMASTER_FLATPAK_ID}.metainfo.xml"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
endif()
