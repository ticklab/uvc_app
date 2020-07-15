cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 11)
add_definitions(-fno-rtti)
add_definitions(-DDBUG)
PROJECT(uvc_app)
include(FindPkgConfig)
pkg_check_modules (JSON-C REQUIRED IMPORTED_TARGET json-c)

include_directories(uvc)

set(LIB_SOURCE
    uvc/uvc-gadget.c
    uvc/uvc_video.cpp
    uvc/yuv.c
    uvc/uvc_control.c
    uvc/uvc_encode.cpp
    uvc/mpi_enc.c
    uvc/uevent.c
    uvc/drm.c
)
add_library(rkuvc SHARED ${LIB_SOURCE})
target_link_libraries(rkuvc pthread drm rockchip_mpp )

set(SOURCE
    main.c
    cJSON/cJSON.c
    ${LIB_SOURCE}
)

set(UVC_APP_DEPENDENT_LIBS
    pthread
    drm
    rockchip_mpp
)

option(DBSERVER_SUPPORT  "enbale dbserver" OFF)
if (${DBSERVER_SUPPORT})
    add_definitions(-DDBSERVER_SUPPORT)
    set(UVC_APP_DEPENDENT_LIBS ${UVC_APP_DEPENDENT_LIBS} IPCProtocol)
endif()

option(ENABLE_MINILOGGER  "enbale minilogger" ON)
if (${ENABLE_MINILOGGER})
    find_package(MiniLogger REQUIRED)
    add_definitions(-DENABLE_MINILOGGER)
    set(UVC_APP_DEPENDENT_LIBS ${UVC_APP_DEPENDENT_LIBS} MiniLogger::MiniLogger)
endif()

option(COMPILES_CAMERA "compile:with rkmedia v4l2 flow " OFF)
option(EPTZ_SUPPORT "uvc support eptz" OFF)
if(COMPILES_CAMERA)


include_directories(process)
add_definitions(-DCAMERA_CONTROL)
set(SOURCE
   ${SOURCE}
   process/camera_control.cpp
   process/camera_pu_control.cpp
   )
SET(UVC_APP_DEPENDENT_LIBS
   ${UVC_APP_DEPENDENT_LIBS}
   easymedia
   )

if(EPTZ_SUPPORT)
add_definitions(-DEPTZ_ENABLE=1)
include_directories(${ROCKX_HEADER_DIR})
set(UVC_APP_DEPENDENT_LIBS
    ${UVC_APP_DEPENDENT_LIBS}
    rockx
    rga
    ${CMAKE_CURRENT_LIST_DIR}/libs/libuvcAlgorithm.so)
set(SOURCE
   ${SOURCE}
   process/eptz_control.cpp
   process/zoom_control.cpp
   )
set(Algorithm_LIBS ${CMAKE_CURRENT_LIST_DIR}/libs/libuvcAlgorithm.so)
install(FILES ${Algorithm_LIBS} DESTINATION lib)

endif()
endif()

ADD_EXECUTABLE(uvc_app ${SOURCE})
target_link_libraries(uvc_app ${UVC_APP_DEPENDENT_LIBS} PkgConfig::JSON-C)

install(TARGETS rkuvc DESTINATION lib)
install(DIRECTORY ./uvc DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(TARGETS uvc_app DESTINATION bin)
install(DIRECTORY . DESTINATION bin
        FILES_MATCHING PATTERN "*.sh"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_WRITE WORLD_EXECUTE)
