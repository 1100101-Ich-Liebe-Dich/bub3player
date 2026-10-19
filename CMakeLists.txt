cmake_minimum_required(VERSION 3.21)
project(Halcyon VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Qml Concurrent DBus Svg ShaderTools Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.18 gstreamer-controller-1.0>=1.18)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib>=1.11)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3>=3.35)

add_executable(halcyon
  src/main.cpp
  src/core/Sql.h
  src/core/Database.h          src/core/Database.cpp
  src/core/TagReader.h         src/core/TagReader.cpp
  src/core/Scanner.h           src/core/Scanner.cpp
  src/core/FolderWatcher.h     src/core/FolderWatcher.cpp
  src/core/CoverProvider.h     src/core/CoverProvider.cpp
  src/core/IconProvider.h      src/core/IconProvider.cpp
  src/library/Library.h        src/library/Library.cpp
  src/library/PlaylistStore.h  src/library/PlaylistStore.cpp
  src/library/TrackCache.h     src/library/TrackCache.cpp
  src/library/QueryBuilder.h   src/library/QueryBuilder.cpp
  src/library/TrackListModel.h src/library/TrackListModel.cpp
  src/library/GroupModel.h     src/library/GroupModel.cpp
  src/library/FolderModel.h    src/library/FolderModel.cpp
  src/library/ViewRegistry.h   src/library/ViewRegistry.cpp
  src/player/PlayQueue.h       src/player/PlayQueue.cpp
  src/player/Player.h          src/player/Player.cpp
  src/analysis/Fft.h
  src/analysis/AudioAnalyzer.h src/analysis/AudioAnalyzer.cpp
  src/analysis/AnalysisService.h src/analysis/AnalysisService.cpp
  src/recommend/Recommender.h  src/recommend/Recommender.cpp
  src/jellyfin/JellyfinAccess.h src/jellyfin/JellyfinAccess.cpp
  src/jellyfin/JellyfinSync.h  src/jellyfin/JellyfinSync.cpp
  src/jellyfin/Jellyfin.h      src/jellyfin/Jellyfin.cpp
  src/player/Mpris.h           src/player/Mpris.cpp
  src/ui/Memory.h              src/ui/Memory.cpp
  src/ui/Theme.h               src/ui/Theme.cpp
  src/core/Paths.h
)

# QML, themes and fonts are compiled into the binary.
file(GLOB QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml qml/skins/*.qml)
file(GLOB THEME_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS themes/*.json skins/*.json skins/*.jpg)
qt_add_resources(halcyon "ui" PREFIX "/" FILES ${QML_FILES} ${THEME_FILES})
file(GLOB SHADER_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS shaders/*.frag shaders/*.vert)
qt_add_shaders(halcyon "shaders" PREFIX "/" FILES ${SHADER_FILES})
qt_add_resources(halcyon "appicon" PREFIX "/icons" BASE dist/icons FILES dist/icons/de.bauenundbooten.BuB3Player.svg)
file(GLOB FONT_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS assets/fonts/*.ttf)
qt_add_resources(halcyon "fonts" PREFIX "/fonts" BASE assets/fonts FILES ${FONT_FILES})

target_include_directories(halcyon PRIVATE src)
target_compile_definitions(halcyon PRIVATE HALCYON_VERSION="${PROJECT_VERSION}")
target_link_libraries(halcyon PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Concurrent Qt6::DBus Qt6::Svg Qt6::Network
  PkgConfig::GST PkgConfig::TAGLIB PkgConfig::SQLITE)
target_compile_options(halcyon PRIVATE -Wall -Wextra -Wno-unused-parameter)

include(GNUInstallDirs)
install(TARGETS halcyon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dist/de.bauenundbooten.BuB3Player.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES dist/icons/de.bauenundbooten.BuB3Player.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES dist/de.bauenundbooten.BuB3Player.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
