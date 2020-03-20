#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <camera_engine_rkisp/interface/rkisp_api.h>

#include "uvc_video.h"
#include "uvc_control.h"
#include "mpi_enc.h"


static char dev_name[255];
static int width = 640;
static int height = 480;
static int format = V4L2_PIX_FMT_NV12;
static int fd = -1;

static int timeout = 0;
static int loop = 0;

static int silent = 0;

#define DBG(...) do { if(silent) printf(__VA_ARGS__); } while(0)
#define ERR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)


void parse_args(int argc, char **argv)
{
    int c;
    int digit_optind = 0;

    while (1) {
        int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"width",    required_argument, 0, 'w' },
            {"height",   required_argument, 0, 'h' },
            {"device",   required_argument, 0, 'd' },
            {"debug",   required_argument, 0, 'f' },
            {"timeout",   required_argument, 0, 't' },
            {"loop",   required_argument, 0, 'l' },
            {"help",     no_argument,       0, 'p' },
            {0,          0,                 0,  0  }
        };

        c = getopt_long(argc, argv, "w:h:m:f:i:d:o:c:t:l:e:g:ps",
            long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'w':
            width = atoi(optarg);
            break;
        case 'h':
            height = atoi(optarg);
            break;
        case 'd':
            strcpy(dev_name, optarg);
            break;
	case 'f':
            silent = atoi(optarg);
            break;
	case 't':
            timeout = atoi(optarg);
            break;
        case 'l' :
            loop = atoi(optarg);
            break;
        case '?':
        case 'p':
            ERR("Usage: %s to capture rkisp1 frames\n"
                "   --width,  default 640, optional, width of image\n"
                "   --height, default 480, optional, height of image\n"
                "   --device,              required, path of video device\n"
                "   --timeout,             set timeout(s) for stop test.\n"
                "   --debug, default 0, optional, open debug log\n",
                argv[0]);
            exit(-1);

        default:
            ERR("?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (strlen(dev_name) == 0) {
        ERR("arguments --output and --device are required\n");
        exit(-1);
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

	const struct rkisp_api_ctx *ctx;
    const struct rkisp_api_buf *buf;
    uint32_t flags = 0;
    int extra_cnt = 0;

    ctx = rkisp_open_device(dev_name, 1);

    if (ctx == NULL) {
        ERR("ctx == NULL\n");
        return -1;
    }

    if (rkisp_set_fmt(ctx, width, height, format)) {
        ERR("rkisp_set_fmt fai,width=%d,height=%d ,format=%d\n",width,height,format);
        return -1;
    }

    if (rkisp_start_capture(ctx)) {
        ERR("rkisp_start_capture fail\n");
        return -1;
    }

    int iframe = 1;
	switch (width) {
        case 640:
                iframe = 0;
                break;
        case 1280:
                iframe = 1;
                break;
        case 1920:
                iframe = 2;
                break;
        case 2560:
                iframe = 3;
                break;
        case 2592:
                iframe = 4;
                break;
        default:
                DBG("width no support for camera list.");
	}
    if(loop)
      flags = UVC_CONTROL_CHECK_STRAIGHT;//UVC_CONTROL_LOOP_ONCE;
    else
      flags = UVC_CONTROL_LOOP_ONCE;
    uvc_control_run(flags);

    struct timeval startTime;
    gettimeofday(&startTime, NULL);

    do {
        buf = rkisp_get_frame(ctx, 0);
        DBG("size: %d, dmabuf fd: %d\n", buf->size, buf->fd);
        extra_cnt++;
        uvc_read_camera_buffer(buf->buf, buf->fd, buf->size, &extra_cnt, sizeof(extra_cnt));

        rkisp_put_frame(ctx, buf);
        if(timeout > 0){
           struct timeval curTime;
           gettimeofday(&curTime, NULL);
           long seconds;
           seconds  = curTime.tv_sec  - startTime.tv_sec;
           if (timeout < seconds){
               ERR("timeout to exit uvc camera now ...\n");
               break;
           }
         }
    } while (1);
ERR("uvc_control_join before\n");
    uvc_control_join(flags);

    rkisp_stop_capture(ctx);
    rkisp_close_device(ctx);
ERR("exit ....\n");
    return 0;
}
