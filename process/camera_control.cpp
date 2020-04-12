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

#include "camera_control.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdbool.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <iostream>

#include <easymedia/buffer.h>
#include <easymedia/key_string.h>
#include <easymedia/media_config.h>
#include <easymedia/utils.h>

#include <easymedia/flow.h>

struct Camera_Stream
{
    int width;
    int height;
    int fps;
    pthread_mutex_t record_mutex;
    pthread_cond_t record_cond;
    std::shared_ptr<easymedia::Flow> input;
    std::shared_ptr<easymedia::Flow> uvc_flow;

    pthread_t record_id;
    int deviceid;
    volatile int pthread_run;
};

static bool do_uvc(easymedia::Flow *f,
                   easymedia::MediaBufferVector &input_vector);
class UVCJoinFlow : public easymedia::Flow
{
public:
    UVCJoinFlow(uint32_t id);
    virtual ~UVCJoinFlow()
    {
        StopAllThread();
    }

private:
    uint32_t id;
    friend bool do_uvc(easymedia::Flow *f,
                       easymedia::MediaBufferVector &input_vector);
};

UVCJoinFlow::UVCJoinFlow(uint32_t id)
    : id(20)
{
    easymedia::SlotMap sm;
    sm.thread_model = easymedia::Model::ASYNCCOMMON;
    sm.mode_when_full = easymedia::InputMode::DROPFRONT;
    sm.input_slots.push_back(0);
    sm.input_maxcachenum.push_back(2);
    sm.fetch_block.push_back(true);
    if (true)
    {
        sm.input_slots.push_back(1);
        sm.input_maxcachenum.push_back(1);
        sm.fetch_block.push_back(false);
    }
    sm.process = do_uvc;
    if (!InstallSlotMap(sm, "uvc_extract", -1))
    {
        fprintf(stderr, "Fail to InstallSlotMap, %s\n", "uvc_join");
        SetError(-EINVAL);
        return;
    }
}

static struct Camera_Stream *stream_list = NULL;
static pthread_rwlock_t notelock = PTHREAD_RWLOCK_INITIALIZER;
static std::list<pthread_t> record_id_list;

bool do_uvc(easymedia::Flow *f, easymedia::MediaBufferVector &input_vector)
{
    UVCJoinFlow *flow = (UVCJoinFlow *)f;
    auto img_buf = input_vector[0];
    if (!img_buf || img_buf->GetType() != Type::Image)
        return false;

    auto img = std::static_pointer_cast<easymedia::ImageBuffer>(img_buf);

    uvc_read_camera_buffer(img_buf->GetPtr(), img_buf->GetFD(), img_buf->GetValidSize(),
                           NULL, 0);
    return true;
}

static void camera_control_wait(struct Camera_Stream *stream)
{
    pthread_mutex_lock(&stream->record_mutex);
    if (stream->pthread_run)
        pthread_cond_wait(&stream->record_cond, &stream->record_mutex);
    pthread_mutex_unlock(&stream->record_mutex);
}

void video_record_signal(struct Camera_Stream *stream)
{
    pthread_mutex_lock(&stream->record_mutex);
    stream->pthread_run = 0;
    pthread_cond_signal(&stream->record_cond);
    pthread_mutex_unlock(&stream->record_mutex);
}

static struct Camera_Stream *getfastvideo(void)
{
    return stream_list;
}

static void camera_stop(struct Camera_Stream *stream)
{
    pthread_rwlock_wrlock(&notelock);
    printf("%s \n", __func__);
    if (stream->input)
    {
        if (stream->uvc_flow)
            stream->input->RemoveDownFlow(stream->uvc_flow);
        stream->input.reset();
    }
    if (stream->uvc_flow)
        stream->uvc_flow.reset();

    pthread_mutex_destroy(&stream->record_mutex);
    pthread_cond_destroy(&stream->record_cond);

    if (stream)
        free(stream);

    pthread_rwlock_unlock(&notelock);
}

static void *uvc_camera(void *arg)
{
    struct Camera_Stream *stream = (struct Camera_Stream *)arg;
    printf("%s \n", __func__);
    prctl(PR_SET_NAME, "uvc_camera", 0, 0, 0);

    std::string input_path = "/dev/video0"; //isp main path
    std::string input_format = IMAGE_NV12;

    //Reading yuv from camera
    std::string flow_name = "source_stream";
    std::string flow_param = "";

    PARAM_STRING_APPEND(flow_param, KEY_NAME, "v4l2_capture_stream");
    PARAM_STRING_APPEND(flow_param, KEK_THREAD_SYNC_MODEL, KEY_SYNC);
    PARAM_STRING_APPEND(flow_param, KEK_INPUT_MODEL, KEY_DROPFRONT);
    PARAM_STRING_APPEND_TO(flow_param, KEY_INPUT_CACHE_NUM, 5);
    std::string stream_param = "";
    PARAM_STRING_APPEND_TO(stream_param, KEY_USE_LIBV4L2, 1);
    PARAM_STRING_APPEND(stream_param, KEY_DEVICE, input_path);
    // PARAM_STRING_APPEND(param, KEY_SUB_DEVICE, sub_input_path);
    PARAM_STRING_APPEND(stream_param, KEY_V4L2_CAP_TYPE, KEY_V4L2_C_TYPE(VIDEO_CAPTURE));
    PARAM_STRING_APPEND(stream_param, KEY_V4L2_MEM_TYPE, KEY_V4L2_M_TYPE(MEMORY_DMABUF));
    PARAM_STRING_APPEND_TO(stream_param, KEY_FRAMES, 4); // if not set, default is 2
    PARAM_STRING_APPEND(stream_param, KEY_OUTPUTDATATYPE, input_format);
    PARAM_STRING_APPEND_TO(stream_param, KEY_BUFFER_WIDTH, stream->width);
    PARAM_STRING_APPEND_TO(stream_param, KEY_BUFFER_HEIGHT, stream->height);
    PARAM_STRING_APPEND_TO(stream_param, KEY_BUFFER_VIR_WIDTH, stream->width);
    PARAM_STRING_APPEND_TO(stream_param, KEY_BUFFER_VIR_HEIGHT, stream->height);

    flow_param = easymedia::JoinFlowParam(flow_param, 1, stream_param);
    printf("\n#VideoCapture flow param:\n%s\n", flow_param.c_str());
    stream->input = easymedia::REFLECTOR(Flow)::Create<easymedia::Flow>(
        flow_name.c_str(), flow_param.c_str());
    if (!stream->input)
    {
        fprintf(stderr, "Create flow %s failed\n", flow_name.c_str());
        goto record_exit;
    }

    stream->uvc_flow = std::make_shared<UVCJoinFlow>(stream->deviceid);
    if (!stream->uvc_flow)
    {
        fprintf(stderr, "Create flow UVCJoinFlow failed\n");
        goto record_exit;
    }

    stream->input->AddDownFlow(stream->uvc_flow, 0, 0);

    printf("%s start\n", __func__);

    while (stream->pthread_run)
    {
        camera_control_wait(stream);
    }
    goto record_exit;

record_exit:
    printf("%s exit\n", __func__);
    camera_stop(stream);
    stream = NULL;
    pthread_exit(NULL);
}

extern "C" int camera_control_start(int id, int width, int height, int fps)
{
    struct Camera_Stream *stream;
    int ret = 0;
    if (id < 0)
        return -1;
    printf("%s!\n", __func__);
    pthread_rwlock_wrlock(&notelock);

    stream = (struct Camera_Stream *)calloc(1, sizeof(struct Camera_Stream));
    if (!stream)
    {
        printf("no memory!\n");
        goto addvideo_exit;
    }
    pthread_mutex_init(&stream->record_mutex, NULL);
    pthread_cond_init(&stream->record_cond, NULL);
    stream->pthread_run = 1;
    stream->input = NULL;
    stream->uvc_flow = NULL;
    stream->fps = fps;
    stream->width = width;
    stream->height = height;
    stream->deviceid = id;

    printf("stream%d is uvc video device\n", stream->deviceid);
    stream_list = stream;
    if (pthread_create(&stream->record_id, NULL, uvc_camera, stream))
    {
        printf("%s pthread create err!\n", __func__);
        goto addvideo_exit;
    }
    record_id_list.push_back(stream->record_id);
    ret = 0;
    goto addvideo_ret;

addvideo_exit:

    if (stream)
    {
        if (stream->input)
        {
            if (stream->uvc_flow)
                stream->input->RemoveDownFlow(stream->uvc_flow);
            stream->input.reset();
        }
        if (stream->uvc_flow)
            stream->uvc_flow.reset();

        pthread_mutex_destroy(&stream->record_mutex);
        pthread_cond_destroy(&stream->record_cond);
        stream->input = NULL;
        stream->uvc_flow = NULL;
        free(stream);
        stream = NULL;
    }

    printf("stream%d exit!\n", id);
    ret = -1;

addvideo_ret:
    printf("%s!end\n", __func__);
    pthread_rwlock_unlock(&notelock);
    return ret;
}

extern "C" int camera_control_stop(int deviceid)
{
    struct Camera_Stream *stream;

    pthread_rwlock_rdlock(&notelock);
    stream = getfastvideo();
    while (stream)
    {
        if (stream->deviceid == deviceid)
        {
            video_record_signal(stream);
            break;
        }
    }
    pthread_rwlock_unlock(&notelock);

    return 0;
}

extern "C" void camera_control_init()
{
    //todo
}

extern "C" void camera_control_deinit()
{
    printf("%s!start\n", __func__);
    struct Camera_Stream *stream;
    std::list<pthread_t> save_list;

    pthread_rwlock_wrlock(&notelock);
    stream = getfastvideo();
    while (stream)
    {
        video_record_signal(stream);
        break;
    }
    save_list.clear();
    for (std::list<pthread_t>::iterator it = record_id_list.begin();
         it != record_id_list.end(); ++it)
        save_list.push_back(*it);
    record_id_list.clear();
    pthread_rwlock_unlock(&notelock);
    for (std::list<pthread_t>::iterator it = save_list.begin();
         it != save_list.end(); ++it)
    {
        printf("pthread_join record id: %lu\n", *it);
        pthread_join(*it, NULL);
    }
    save_list.clear();
    printf("%s!end\n", __func__);
}
