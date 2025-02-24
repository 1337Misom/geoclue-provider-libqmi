cmake_minimum_required(VERSION 3.10)
project(geoclue-provider-libqmi)

find_package(PkgConfig)

pkg_check_modules(LibQRTR REQUIRED qrtr-glib)
pkg_check_modules(LibQMI REQUIRED qmi-glib)
pkg_check_modules(geoclue REQUIRED geoclue)

include_directories(${geoclue_INCLUDE_DIRS})
link_directories(${geoclue_LIBRARY_DIRS})

include_directories(${LibQMI_INCLUDE_DIRS})
link_directories(${LibQMI_LIBRARY_DIRS})

include_directories(${LibQRTR_INCLUDE_DIRS})
link_directories(${LibQRTR_LIBRARY_DIRS})

add_executable(${PROJECT_NAME} geoclue-provider-libqmi.c)

target_link_libraries(${PROJECT_NAME} PUBLIC ${LibQMI_LIBRARIES} ${LibQRTR_LIBRARIES} ${geoclue_LIBRARIES})
install(TARGETS geoclue-provider-libqmi RUNTIME DESTINATION /usr/libexec)
install(FILES org.freedesktop.Geoclue.Providers.Qmi.service DESTINATION /usr/share/dbus-1/services)
install(FILES geoclue-libqmi.provider DESTINATION /usr/share/geoclue-providers)
