cmake_minimum_required(VERSION 2.8)

SET(COMPILE_DEFINITIONS -Werror)

#SET(GCC_COVERAGE_COMPILE_FLAGS "-g")

include_directories(/opt/vc/include)
include_directories(/opt/vc/include/interface/vcos/pthreads)
include_directories(/opt/vc/include/interface/vmcs_host)
include_directories(/opt/vc/include/interface/vmcs_host/linux)

include_directories(/home/pi/src/userland)
include_directories(/usr/include/freetype2)

link_directories(/opt/vc/lib)

add_executable(mmal_video_record video_record.c)

target_link_libraries(mmal_video_record mmal_core mmal_util mmal_vc_client vcos bcm_host freetype cairo rt sqlite3 pthread)
