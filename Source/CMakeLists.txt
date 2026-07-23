cmake_minimum_required(VERSION 3.21)

project(WorkHandler
    VERSION 0.0.1
    LANGUAGES CXX
)

set(APP_TARGET WorkHandler)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui HttpServer Network Sql Widgets LinguistTools)

if(NOT WIN32)
    message(FATAL_ERROR "This CMake configuration currently targets the existing Windows x64 ADS binaries.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "QtDock prebuilt libraries in this project are x64 only.")
endif()

set(ADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/QtDock")
set(ADS_LIB_DIR "${ADS_ROOT}/lib/x64")
set(ADS_INCLUDE_DIR "${ADS_ROOT}/include")

add_library(ads::qtadvanceddocking SHARED IMPORTED GLOBAL)
set_target_properties(ads::qtadvanceddocking PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ADS_INCLUDE_DIR}"
    IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
    IMPORTED_IMPLIB_DEBUG "${ADS_LIB_DIR}/qtadvanceddockingd.lib"
    IMPORTED_LOCATION_DEBUG "${ADS_LIB_DIR}/qtadvanceddockingd.dll"
    IMPORTED_IMPLIB_RELEASE "${ADS_LIB_DIR}/qtadvanceddocking.lib"
    IMPORTED_LOCATION_RELEASE "${ADS_LIB_DIR}/qtadvanceddocking.dll"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${ADS_LIB_DIR}/qtadvanceddocking.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${ADS_LIB_DIR}/qtadvanceddocking.dll"
    IMPORTED_IMPLIB_MINSIZEREL "${ADS_LIB_DIR}/qtadvanceddocking.lib"
    IMPORTED_LOCATION_MINSIZEREL "${ADS_LIB_DIR}/qtadvanceddocking.dll"
)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    public.h
    mainwindow.ui

    myUI/customDialog/logviewerdialog.cpp
    myUI/customDialog/logviewerdialog.h
    myUI/customDialog/aboutdialog.cpp
    myUI/customDialog/aboutdialog.h
    myUI/customDialog/httpservermanagerdialog.cpp
    myUI/customDialog/httpservermanagerdialog.h

    myUI/ui/uiManager.cpp
    myUI/ui/uiManager.h
    myUI/ui/uiAdminoptions.cpp
    myUI/ui/uiAdminoptions.h
    myUI/ui/uiAnimation.cpp
    myUI/ui/uiAnimation.h
    myUI/ui/trayiconrenderer.cpp
    myUI/ui/trayiconrenderer.h
    myUI/ui/thememanager.cpp
    myUI/ui/thememanager.h
    myUI/ui/languagemanager.cpp
    myUI/ui/languagemanager.h

    myUI/feature/softwareconfig.cpp
    myUI/feature/softwareconfig.h
    myUI/feature/shortcutmanager.cpp
    myUI/feature/shortcutmanager.h
    myUI/feature/exitmodedialog.cpp
    myUI/feature/exitmodedialog.h
    myUI/feature/myLogger.cpp
    myUI/feature/myLogger.h

    html.qrc
    image.qrc
    language.qrc
    theme.qrc
)

set(BACKEND_SOURCES
    server/api/apicontext.cpp
    server/api/apicontext.h
    server/api/multipartparser.cpp
    server/api/multipartparser.h
    server/attachments/attachmentcontroller.cpp
    server/attachments/attachmentcontroller.h
    server/attachments/attachmentdao.cpp
    server/attachments/attachmentdao.h
    server/attachments/attachmentservice.cpp
    server/attachments/attachmentservice.h
    server/attachments/imageprocessor.cpp
    server/attachments/imageprocessor.h
    server/blocks/blockcontroller.cpp
    server/blocks/blockcontroller.h
    server/blocks/blockdao.cpp
    server/blocks/blockdao.h
    server/blocks/blockservice.cpp
    server/blocks/blockservice.h
    server/comments/commentcontroller.cpp
    server/comments/commentcontroller.h
    server/comments/commentdao.cpp
    server/comments/commentdao.h
    server/comments/commentservice.cpp
    server/comments/commentservice.h
    server/issues/issuecontroller.cpp
    server/issues/issuecontroller.h
    server/issues/issuedao.cpp
    server/issues/issuedao.h
    server/issues/issueservice.cpp
    server/issues/issueservice.h
    server/maintenance/maintenancedao.cpp
    server/maintenance/maintenancedao.h
    server/maintenance/maintenancemanager.cpp
    server/maintenance/maintenancemanager.h
    server/notifications/notificationcontroller.cpp
    server/notifications/notificationcontroller.h
    server/notifications/notificationdao.cpp
    server/notifications/notificationdao.h
    server/notifications/notificationmanager.cpp
    server/notifications/notificationmanager.h
    server/databasemanager.cpp
    server/databasemanager.h
    server/httpserver.cpp
    server/httpserver.h
    server/passwordhasher.cpp
    server/passwordhasher.h
    server/tokenhelper.cpp
    server/tokenhelper.h
    server/users/useroptionscontroller.cpp
    server/users/useroptionscontroller.h
    server/users/useroptionsservice.cpp
    server/users/useroptionsservice.h
)

add_library(workhandler_backend STATIC ${BACKEND_SOURCES})
set_target_properties(workhandler_backend PROPERTIES AUTOUIC OFF AUTORCC OFF)
target_include_directories(workhandler_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/server"
)
target_compile_options(workhandler_backend PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)
target_link_libraries(workhandler_backend PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::HttpServer
    Qt6::Network
    Qt6::Sql
)

set(PROJECT_TRANSLATIONS
    language/zh_CN.ts
    language/en_UK.ts
)

qt_add_executable(${APP_TARGET} WIN32
    ${PROJECT_SOURCES}
    ${PROJECT_TRANSLATIONS}
)

target_include_directories(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/myUI/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/myUI/feature"
    "${CMAKE_CURRENT_SOURCE_DIR}/myUI/customDialog"
    "${CMAKE_CURRENT_SOURCE_DIR}/server"
    "${ADS_INCLUDE_DIR}"
)

target_compile_definitions(${APP_TARGET} PRIVATE
    QT_DEPRECATED_WARNINGS
    WORKHANDLER_APPLICATION_NAME="${PROJECT_NAME}"
    WORKHANDLER_VERSION="${PROJECT_VERSION}"
)

target_compile_options(${APP_TARGET} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(${APP_TARGET} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::HttpServer
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
    ads::qtadvanceddocking
    workhandler_backend
)

set_target_properties(${APP_TARGET} PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}"
    WIN32_EXECUTABLE TRUE
)

add_custom_command(TARGET ${APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ads::qtadvanceddocking>"
        "$<TARGET_FILE_DIR:${APP_TARGET}>"
    COMMENT "Copy QtDock runtime library next to ${APP_TARGET}"
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(workhandler_httpserver_tests
        tests/httpserverintegrationtest.cpp
    )
    set_target_properties(workhandler_httpserver_tests PROPERTIES
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(workhandler_httpserver_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        workhandler_backend
    )
    add_test(NAME workhandler_httpserver_tests
             COMMAND workhandler_httpserver_tests)

    qt_add_executable(workhandler_maintenance_tests
        tests/maintenancetest.cpp
    )
    set_target_properties(workhandler_maintenance_tests PROPERTIES
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(workhandler_maintenance_tests PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
        workhandler_backend
    )
    add_test(NAME workhandler_maintenance_tests
             COMMAND workhandler_maintenance_tests)

    qt_add_executable(workhandler_tray_tests
        tests/trayiconrenderertest.cpp
        myUI/ui/trayiconrenderer.cpp
        myUI/ui/trayiconrenderer.h
    )
    set_target_properties(workhandler_tray_tests PROPERTIES
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(workhandler_tray_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/myUI/ui"
    )
    target_link_libraries(workhandler_tray_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )
    add_test(NAME workhandler_tray_tests
             COMMAND workhandler_tray_tests)

endif()
