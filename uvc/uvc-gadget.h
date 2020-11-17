/*
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL), available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _UVC_GADGET_H_
#define _UVC_GADGET_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

#define UVC_EVENT_FIRST         (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_CONNECT       (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_DISCONNECT        (V4L2_EVENT_PRIVATE_START + 1)
#define UVC_EVENT_STREAMON      (V4L2_EVENT_PRIVATE_START + 2)
#define UVC_EVENT_STREAMOFF     (V4L2_EVENT_PRIVATE_START + 3)
#define UVC_EVENT_SETUP         (V4L2_EVENT_PRIVATE_START + 4)
#define UVC_EVENT_DATA          (V4L2_EVENT_PRIVATE_START + 5)
#define UVC_EVENT_LAST          (V4L2_EVENT_PRIVATE_START + 5)
#define UVC_EVENT_SUSPEND       (V4L2_EVENT_PRIVATE_START + 6)
#define UVC_EVENT_RESUME        (V4L2_EVENT_PRIVATE_START + 7)
#define UVC_EVENT_LAST          (V4L2_EVENT_PRIVATE_START + 7)

#define MAX_UVC_REQUEST_DATA_LENGTH 60

struct uvc_request_data
{
    __s32 length;
    __u8 data[60];
};

struct uvc_event
{
    union
    {
        enum usb_device_speed speed;
        struct usb_ctrlrequest req;
        struct uvc_request_data data;
    };
};

#define UVCIOC_SEND_RESPONSE        _IOW('U', 1, struct uvc_request_data)

#define UVC_INTF_CONTROL        0
#define UVC_INTF_STREAMING      1

#ifdef __cplusplus
extern "C" {
#endif

#include "uvc_video.h"
#include "uvc_control.h"
#include "mpi_enc.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>

#define V4L2_PIX_FMT_H265     v4l2_fourcc('H', '2', '6', '5') /* H265 with start codes */

/* ---------------------------------------------------------------------------
 * Generic stuff
 */

enum XuCmd
{
    CMD_GET_CAMERA_VERSION = 0x01,
    CMD_SET_CAMERA_IP,
    CMD_START_CAMERA,
    CMD_SHUTDOWN_CAMERA,
    CMD_RESET_CAMERA,
    CMD_SET_MOTOR_RATE = 0x06,
    CMD_SET_MOTOR_BY_STEPS = 0x07,
    CMD_SET_MOTOR_BY_USER = 0x08,
    CMD_STOP_MOTOR_BY_USER = 0x09,
    CMD_SET_EPTZ = 0x0a,
    CMD_SET_H265 = 0x0b,
    CMD_MAX_NUM = CMD_SET_H265,
};

/* IO methods supported */
enum io_method
{
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
    IO_METHOD_DMA_BUFF,
};

#define UVC_IO_METHOD_MMAP 0
#define UVC_IO_METHOD_USERPTR 1
#define UVC_IO_METHOD_DMA_BUFF 2

#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
#define UVC_IO_METHOD   UVC_IO_METHOD_DMA_BUFF // do not modify, modify the RK_MPP_USE_UVC_VIDEO_BUFFER to choice
#else
#define UVC_IO_METHOD   UVC_IO_METHOD_MMAP //UVC_IO_METHOD_DMA_BUFF // here can set 0 or 2 //  1 still not support
#endif

/* Buffer representing one video frame */
struct buffer
{
    struct v4l2_buffer buf;
    void *start;
    size_t length;
};

struct v4l2_buffer_info
{
    struct uvc_buffer *uvc_buf;
    int fd;
};

enum USB_STATE
{
    USB_STATE_FIRST_GET_READY,
    USB_STATE_FIRST_GET_OK,
    USB_STATE_FIRST_SEND_OK,
    USB_STATE_NORMAL_RUN
};

/* Represents a UVC based video output device */
struct uvc_device
{
    int video_id;
    /* uvc device specific */
    int uvc_fd;
    int is_streaming;
    int run_standalone;
    char *uvc_devname;
    int suspend;
    /* uvc control request specific */
    struct uvc_streaming_control probe;
    struct uvc_streaming_control commit;
    int control;
    struct uvc_request_data request_error_code;
    unsigned int brightness_val;
    unsigned short contrast_val;
    int hue_val;
    unsigned int saturation_val;
    unsigned int sharpness_val;
    unsigned int gamma_val;
    unsigned int white_balance_temperature_val;
    unsigned int white_balance_temperature_auto_val;
    unsigned int gain_val;
    unsigned int hue_auto_val;
    unsigned int zoom_val;
    int pan_val;
    int tilt_val;
    short roll_val;
    unsigned char power_line_frequency_val;
    unsigned char ex_sn_data[MAX_UVC_REQUEST_DATA_LENGTH];
    unsigned char ex_ip_data[MAX_UVC_REQUEST_DATA_LENGTH];//
    unsigned char ex_date_data[MAX_UVC_REQUEST_DATA_LENGTH];
    unsigned int eptz_flag;
    unsigned int xu_h265;

    /* uvc buffer specific */
    enum io_method io;
    struct buffer *mem;
    struct buffer *dummy_buf;
    unsigned int nbufs;
    unsigned int fcc;
    unsigned int width;
    unsigned int height;
    unsigned int fps;

    unsigned int bulk;
    uint8_t color;
    unsigned int imgsize;
    void *imgdata;

    /* USB speed specific */
    int mult;
    int burst;
    int maxpkt;
    enum usb_device_speed speed;

    /* uvc specific flags */
    int first_buffer_queued;
    int uvc_shutdown_requested;

    /* uvc buffer queue and dequeue counters */
    unsigned long long int qbuf_count;
    unsigned long long int dqbuf_count;

    /* v4l2 device hook */
    struct v4l2_device *vdev;
    uint8_t cs;
    uint8_t entity_id;
    struct v4l2_buffer ubuf;

    struct v4l2_buffer_info *vbuf_info;
    int abandon_count;
    enum USB_STATE usb_state;
    unsigned int first_usb_get_ready_pts;
    unsigned int first_usb_get_ok_pts;
    unsigned int first_usb_send_ok_pts;
    unsigned int first_cmd_pts;
    unsigned int stream_on_pts;
    int get_buf_count;
};



int uvc_gadget_main(int id);
int uvc_video_reqbufs(struct uvc_device *dev, int nbufs);
int uvc_video_stream(struct uvc_device *dev, int enable);

#ifdef __cplusplus
}
#endif

#endif /* _UVC_GADGET_H_ */

