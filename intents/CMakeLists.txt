# Lingmo Intents of Lingmo Studio: a small separate program, lingmo-studio-intents.
# Builds inside the studio tree (add_subdirectory(intents)) or on its own:
#   cmake -S intents -B build-intents
if(NOT CMAKE_PROJECT_NAME)
    cmake_minimum_required(VERSION 3.16)
    project(lingmo-studio-intents LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    include(GNUInstallDirs)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core DBus)
find_package(LingmoIntents 1.0 QUIET)
if(LingmoIntents_FOUND)
    add_executable(lingmo-studio-intents main.cpp)
    target_link_libraries(lingmo-studio-intents PRIVATE Qt6::Core Qt6::DBus LingmoIntents::LingmoIntents)
    install(TARGETS lingmo-studio-intents RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    lingmo_install_intents(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/lingmo-studio.json
                           SERVICE ${CMAKE_CURRENT_SOURCE_DIR}/com.lingmo.Studio.service)
else()
    message(WARNING "LingmoIntents not found: Lingmo Studio is built without app actions (intents)")
endif()
