cmake_minimum_required(VERSION 3.16)
project(lingmo-studio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

option(BUILD_TESTING "Build the tests" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Concurrent Widgets Qml Quick LinguistTools)
find_package(Mlt7 REQUIRED)

# Editing core: model, operations, MLT plan/XML/builder, export, analysis.
# No QML here, so the tests link it directly.
add_library(studio-core STATIC
    src/core/aitools.cpp
    src/core/effects.cpp
    src/core/exporter.cpp
    src/core/mediaprobe.cpp
    src/core/mltbuilder.cpp
    src/core/mltxml.cpp
    src/core/model.cpp
    src/core/plan.cpp
    src/core/projectio.cpp
    src/core/timelineops.cpp
)
target_include_directories(studio-core PUBLIC src/core)
target_link_libraries(studio-core PUBLIC Qt6::Core Qt6::Gui Mlt7::mlt++ Mlt7::mlt)

# Application objects (editor controller, player, models), shared with tests
add_library(studio-app STATIC
    src/app/editor.cpp
    src/app/timelinemodel.cpp
    src/app/mediabin.cpp
    src/app/player.cpp
    src/app/videoitem.cpp
    src/app/renderqueue.cpp
    src/app/scopes.cpp
    src/app/imageproviders.cpp
    src/app/recorder.cpp
)
target_include_directories(studio-app PUBLIC src/app)
target_link_libraries(studio-app PUBLIC studio-core Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Qml Qt6::Quick)

add_executable(lingmo-studio
    src/app/main.cpp
    resources.qrc
)
target_link_libraries(lingmo-studio PRIVATE
    studio-app
    Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Widgets Qt6::Qml Qt6::Quick
)
set(TRANSLATIONS_DIR ${CMAKE_INSTALL_FULL_DATADIR}/lingmo-studio/translations)
target_compile_definitions(lingmo-studio PRIVATE
    TRANSLATIONS_DIR="${TRANSLATIONS_DIR}"
    LINGMO_STUDIO_VERSION="${PROJECT_VERSION}"
)
# lrelease only: building never rewrites the .ts sources
qt_add_translation(QM_FILES translations/lingmo-studio_pt_BR.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
install(FILES ${QM_FILES} DESTINATION ${TRANSLATIONS_DIR})

install(TARGETS lingmo-studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES lingmo-studio.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/lingmo-studio.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
install(FILES data/lingmo-studio.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(t tst_timeline tst_projectio tst_export tst_aitools)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE studio-core Qt6::Test)
        target_compile_definitions(${t} PRIVATE TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C")
    endforeach()
    add_executable(tst_editor tests/tst_editor.cpp)
    target_link_libraries(tst_editor PRIVATE studio-app Qt6::Test)
    target_compile_definitions(tst_editor PRIVATE TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
    add_test(NAME tst_editor COMMAND tst_editor)
    set_tests_properties(tst_editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C;SDL_AUDIODRIVER=dummy" TIMEOUT 300)
    # developer tool: builds a demo project from media files (screenshots)
    add_executable(demo_project tests/demo_project.cpp)
    target_link_libraries(demo_project PRIVATE studio-core Qt6::Gui)
endif()
