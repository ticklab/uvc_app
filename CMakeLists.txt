cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 11)
add_definitions(-fno-rtti)
PROJECT(uvc_app)

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
    ${LIB_SOURCE}
)

set(UVC_APP_DEPENDENT_LIBS
    pthread
    drm
    rockchip_mpp
)

option(COMPILES_CAMERA "compile:with rkmedia v4l2 flow " OFF)
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
endif()

ADD_EXECUTABLE(uvc_app ${SOURCE})
target_link_libraries(uvc_app ${UVC_APP_DEPENDENT_LIBS})

install(TARGETS rkuvc DESTINATION lib)
install(DIRECTORY ./uvc DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(TARGETS uvc_app DESTINATION bin)
install(DIRECTORY . DESTINATION bin
        FILES_MATCHING PATTERN "*.sh"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_WRITE WORLD_EXECUTE)
