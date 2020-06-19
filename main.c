#include <stdio.h>
#include <unistd.h>
#include "uvc_control.h"
#include "uvc_video.h"

#ifdef CAMERA_CONTROL
#include "camera_control.h"
#endif

#include "mpi_enc.h"
#include "uevent.h"
#include "drm.h"

#define ALIGN(size, align) ((size + align - 1) & (~(align - 1)))

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    unsigned int handle;
    char *buffer;
    int handle_fd;
    size_t size;
    int i = 0;
    int width, height;
    int y, uv;
    int extra_cnt = 0;
    uint32_t flags = 0;
#if (RK_MPP_ENC_TEST_NATIVE == 0)
#ifdef CAMERA_CONTROL
    if (argc != 3)
    {
        printf("uvc_app loop from v4l2.\n");
        camera_control_init();
        uvc_control_start_setcallback(camera_control_start);
        uvc_control_stop_setcallback(camera_control_deinit);
        uevent_monitor_run(UVC_CONTROL_CAMERA);
        //system("uvc_config.sh");
        uvc_control_run(UVC_CONTROL_CAMERA);
        while (1)
        {
            uvc_control_loop();
            usleep(100000);
        }
        uvc_video_id_exit_all();
        camera_control_deinit();
        printf("uvc_app exit.\n");
        StopAllThread();
        return 0;
    }
#else
   if (argc != 3) {
     printf("please select true control mode!!\n");
     return 0;
   }
#endif
#endif

    if (argc < 3) {
        printf("Usage: uvc_app width height [test_file.nv12]\n");
        printf("e.g. uvc_app 640 480 [test_file.nv12]\n");
        return -1;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    FILE *test_file = NULL;
    if (width == 0 || height == 0) {
        printf("Usage: uvc_app width height [test_file.nv12]\n");
        printf("e.g. uvc_app 640 480 [test_file.nv12]\n");
        return -1;
    }

    fd = drm_open();
    if (fd < 0)
        return -1;

    size = width * height * 3 / 2;
    ret = drm_alloc(fd, size, 16, &handle, 0);
    if (ret)
        return -1;
    printf("size:%d", size);
    ret = drm_handle_to_fd(fd, handle, &handle_fd, 0);
    if (ret)
        return -1;

    buffer = (char *)drm_map_buffer(fd, handle, size);
    if (!buffer)
    {
        printf("drm map buffer fail.\n");
        return -1;
    }

    if (argc == 4) {
        test_file = fopen(argv[3], "r+b");
        if (!test_file) {
            printf("open %s fail.\n", argv[3]);
            return -1;
        }
    } else {
        y = width * height / 4;
        memset(buffer, 128, y);
        memset(buffer + y, 64, y);
        memset(buffer + y * 2, 128, y);
        memset(buffer + y * 3, 192, y);
        uv = width * height / 8;
        memset(buffer + y * 4, 0, uv);
        memset(buffer + y * 4 + uv, 64, uv);
        memset(buffer + y * 4 + uv * 2, 128, uv);
        memset(buffer + y * 4 + uv * 3, 192, uv);
    }

    flags = UVC_CONTROL_LOOP_ONCE;
#if RK_MPP_ENC_TEST_NATIVE
    uvc_encode_init(&uvc_enc, width, height, TEST_ENC_TPYE);
#else
    uvc_control_run(flags);
 #endif
    while (1)
    {
        if (test_file) {
            if(feof(test_file)) {
                rewind(test_file);
            }
            size = fread(buffer, 1, width * height * 3 / 2, test_file);
        }

        extra_cnt++;
        uvc_read_camera_buffer(buffer, handle_fd, size, &extra_cnt, sizeof(extra_cnt));
        usleep(30000);
    }
    if (test_file)
         fclose(test_file);

    uvc_control_join(flags);

    drm_unmap_buffer(buffer, size);
    drm_free(fd, handle);
    drm_close(fd);
    return 0;
}
