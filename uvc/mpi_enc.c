/*
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/inotify.h>
#include "mpi_enc.h"
#include "uvc_video.h"
#include "uvc_encode.h"
#include "uvc_log.h"
#include "../cJSON/cJSON.h"
void *thread_check_mpp_enc_chenge_loop(void *user);

static int mpp_enc_cfg_set(MpiEncTestData *p, bool init);
static int check_mpp_enc_cfg_file_init(MpiEncTestData *p);
static void mpp_enc_cfg_default(MpiEncTestData *p);
static int parse_check_mpp_enc_cfg(cJSON *root, MpiEncTestData *p, bool init);
static void dump_mpp_enc_cfg(MpiEncTestData *p);
static int read_mpp_enc_cfg_modify_file(MpiEncTestData *p, bool init);

#if 0
static OptionInfo mpi_enc_cmd[] =
{
    {"i",               "input_file",           "input bitstream file"},
    {"o",               "output_file",          "output bitstream file, "},
    {"w",               "width",                "the width of input picture"},
    {"h",               "height",               "the height of input picture"},
    {"f",               "format",               "the format of input picture"},
    {"t",               "type",                 "output stream coding type"},
    {"n",               "max frame number",     "max encoding frame number"},
    {"d",               "debug",                "debug flag"},
};
#endif
static MppFrameFormat g_format = MPP_FMT_YUV420SP;

RK_S32 mpi_get_env_u32(const char *name, RK_U32 *value, RK_U32 default_value)
{
    char *ptr = getenv(name);
    if (NULL == ptr)
    {
        *value = default_value;
    }
    else
    {
        char *endptr;
        int base = (ptr[0] == '0' && ptr[1] == 'x') ? (16) : (10);
        *value = strtoul(ptr, &endptr, base);
        if (ptr == endptr)
        {
            *value = default_value;
        }
    }
    return 0;
}

static MPP_RET test_ctx_init(MpiEncTestData **data, MpiEncTestCmd *cmd)
{
    MpiEncTestData *p = NULL;
    MPP_RET ret = MPP_OK;

    if (!data || !cmd)
    {
        LOG_ERROR("invalid input data %p cmd %p\n", data, cmd);
        return MPP_ERR_NULL_PTR;
    }

    p = calloc(sizeof(MpiEncTestData), 1);
    if (!p)
    {
        LOG_ERROR("create MpiEncTestData failed\n");
        ret = MPP_ERR_MALLOC;
        goto RET;
    }

    // get paramter from cmd
    p->width        = cmd->width;
    p->height       = cmd->height;
    p->hor_stride   = cmd->width;//MPP_ALIGN(cmd->width, 16);
    p->ver_stride   = cmd->height;//MPP_ALIGN(cmd->height, 16);
    p->fmt          = cmd->format;
    p->type         = cmd->type;
    if (cmd->type == MPP_VIDEO_CodingMJPEG)
        cmd->num_frames = 1;
    p->num_frames   = cmd->num_frames;

    mpi_get_env_u32("uvc_enc_out", &cmd->have_output, 0);

    if (cmd->have_output || !access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0))
    {
        p->fp_output = fopen(RK_MPP_DEBUG_OUT_FILE, "w+b");
        if (NULL == p->fp_output)
        {
            LOG_ERROR("failed to open output file %s\n", RK_MPP_DEBUG_OUT_FILE);
            ret = MPP_ERR_OPEN_FILE;
        }
        LOG_INFO("debug out file open\n");
    }

    if (!access(RK_MPP_DYNAMIC_DEBUG_IN_CHECK, 0))
    {
        p->fp_input = fopen(RK_MPP_DEBUG_IN_FILE, "w+b");
        if (NULL == p->fp_input)
        {
            LOG_ERROR("failed to open in file %s\n", RK_MPP_DEBUG_IN_FILE);
            ret = MPP_ERR_OPEN_FILE;
        }
        LOG_INFO("warnning:debug in file open, open it will lower the fps\n");
    }

    // update resource parameter
    if (p->fmt <= MPP_FMT_YUV420SP_VU)
        p->frame_size = MPP_ALIGN(cmd->width, 16) * MPP_ALIGN(cmd->height, 16) * 2;
    else if (p->fmt <= MPP_FMT_YUV422_UYVY)
    {
        // NOTE: yuyv and uyvy need to double stride
        p->hor_stride *= 2;
        p->frame_size = p->hor_stride * p->ver_stride;
    }
    else
        p->frame_size = p->hor_stride * p->ver_stride * 4;
    p->packet_size  = p->frame_size;//p->width * p->height;

RET:
    *data = p;
    return ret;
}

static MPP_RET test_ctx_deinit(MpiEncTestData **data)
{
    MpiEncTestData *p = NULL;

    if (!data)
    {
        LOG_ERROR("invalid input data %p\n", data);
        return MPP_ERR_NULL_PTR;
    }

    p = *data;
    if (p)
    {
        if (p->fp_input)
        {
            fclose(p->fp_input);
            p->fp_input = NULL;
        }
        if (p->fp_output)
        {
            fclose(p->fp_output);
            p->fp_output = NULL;
        }
        free(p);
        *data = NULL;
    }

    return MPP_OK;
}

//abandoned interface
static MPP_RET test_mpp_setup(MpiEncTestData *p)
{
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppEncCfg cfg;
    MppEncRcMode rc_mode = MPP_ENC_RC_MODE_CBR;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi_get_env_u32("enc_version", &p->enc_version, RK_MPP_VERSION_DEFAULT);
    LOG_INFO("enc_version:%d,RK_MPP_USE_FULL_RANGE:%d\n",
             p->enc_version, RK_MPP_USE_FULL_RANGE);

    int need_full_range = 1;
    char* full_range = getenv("ENABLE_FULL_RANGE");
    if (full_range) {
        need_full_range = atoi(full_range);
        LOG_INFO("mpp full_range use env setting:%d \n",need_full_range);
    }

    mpi = p->mpi;
    ctx = p->ctx;
    cfg = p->cfg;

    /* setup default parameter */
    if (p->fps_in_den == 0)
        p->fps_in_den = 1;
    if (p->fps_in_num == 0)
        p->fps_in_num = 30;
    if (p->fps_out_den == 0)
        p->fps_out_den = 1;
    if (p->fps_out_num == 0)
        p->fps_out_num = 30;
    p->gop = 60;
    if (!p->bps)
        p->bps = p->width * p->height / 8 * (p->fps_out_num / p->fps_out_den);
    mpp_enc_cfg_set_s32(cfg, "prep:width", p->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", p->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", p->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", p->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", p->fmt);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", rc_mode);
    switch (rc_mode)
    {
    case MPP_ENC_RC_MODE_FIXQP :
    {
        /* do not set bps on fix qp mode */
    } break;
    case MPP_ENC_RC_MODE_CBR :
    {
        /* CBR mode has narrow bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->bps);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps * 15 / 16);
    }
    break;
    case MPP_ENC_RC_MODE_VBR :
    {
        /* CBR mode has wide bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->bps);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps * 1 / 16);
    }
    break;
    default :
    {
        LOG_ERROR("unsupport encoder rc mode %d\n", rc_mode);
    }
    break;
    }

    /* fix input / output frame rate */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", p->fps_in_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", p->fps_in_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", p->fps_in_den);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", p->fps_out_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", p->fps_out_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", p->fps_out_den);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", p->gop);

    /* setup codec  */
    mpp_enc_cfg_set_s32(cfg, "codec:type", p->type);
    switch (p->type)
    {
    case MPP_VIDEO_CodingAVC :
    {
        /*
        * H.264 profile_idc parameter
        * 66  - Baseline profile
        * 77  - Main profile
        * 100 - High profile
        */
        mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);
        /*
        * H.264 level_idc parameter
        * 10 / 11 / 12 / 13        - qcif@15fps / cif@7.5fps / cif@15fps / cif@30fps
        * 20 / 21 / 22                 - cif@30fps / half-D1@@25fps / D1@12.5fps
        * 30 / 31 / 32                 - D1@25fps / 720p@30fps / 720p@60fps
        * 40 / 41 / 42                 - 1080p@30fps / 1080p@30fps / 1080p@60fps
        * 50 / 51 / 52                 - 4K@30fps
        */
        mpp_enc_cfg_set_s32(cfg, "h264:level", 40);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
        mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);
    }
    break;
    case MPP_VIDEO_CodingMJPEG :
    {
        mpp_enc_cfg_set_s32(cfg, "jpeg:quant", 7);
    }
    break;
    case MPP_VIDEO_CodingVP8 :
    {
    } break;
    case MPP_VIDEO_CodingHEVC :
    {
        mpp_enc_cfg_set_s32(cfg, "h265:qp_init", rc_mode == MPP_ENC_RC_MODE_FIXQP ? -1 : 26);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_max", 51);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_min", 10);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_max_i", 46);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_min_i", 24);
    }
    break;
    default :
    {
        LOG_ERROR("unsupport encoder coding type %d\n", p->type);
    }
    break;
    }

    p->split_mode = MPP_ENC_SPLIT_NONE;
    p->split_arg = 0;

    if (p->split_mode)
    {
        LOG_INFO("split_mode %d split_arg %d\n", p->split_mode, p->split_arg);
        mpp_enc_cfg_set_s32(cfg, "split:mode", p->split_mode);
        mpp_enc_cfg_set_s32(cfg, "split:arg", p->split_arg);
    }

#if RK_MPP_USE_FULL_RANGE
    ret = mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_JPEG);
#else
    if (need_full_range)
       ret = mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_JPEG);
    else
       ret = mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_UNSPECIFIED);
#endif
    if (ret)
    {
        LOG_ERROR("mpi control enc set prep:range failed ret %d\n", ret);
        goto RET;
    }

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret)
    {
        LOG_ERROR("mpi control enc set cfg failed ret %d\n", ret);
        goto RET;
    }

    /* optional */
    p->sei_mode = MPP_ENC_SEI_MODE_DISABLE;
    ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->sei_mode);
    if (ret)
    {
        LOG_ERROR("mpi control enc set sei cfg failed ret %d\n", ret);
        goto RET;
    }

    if (p->enc_version == 1 &&
            (p->type == MPP_VIDEO_CodingAVC ||
             p->type == MPP_VIDEO_CodingHEVC))
    {
        int header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);
        if (ret)
        {
            LOG_ERROR("mpi control enc set codec cfg failed ret %d\n", ret);
            goto RET;
        }
    }

RET:
    return ret;
}

#ifdef RK_MPP_USE_DESTORY_BUFF_THREAD
void do_destory_mpp_buf(MpiEncTestData *p)
{
    if (p->destory_info.destory_frame)
    {
        mpp_frame_deinit(&p->destory_info.destory_frame);
        p->destory_info.destory_frame = NULL;
    }
    if (p->destory_info.destory_buf)
    {
        mpp_buffer_put(p->destory_info.destory_buf);
        p->destory_info.destory_buf = NULL;
    }
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
    if (p->destory_info.destory_pkt_buf_out)
    {
        mpp_buffer_put(p->destory_info.destory_pkt_buf_out);
        p->destory_info.destory_pkt_buf_out = NULL;
    }
#endif
    p->destory_info.unfinished = false;
    p->destory_info.count ++;

}

void *thread_destory_mpp_buf(void *user)
{
    MpiEncTestData *p = (MpiEncTestData *)user;
    pthread_mutex_init(&p->cond_mutex, NULL);
    pthread_cond_init(&p->cond, NULL);
    p->destory_info.unfinished = false;
    while (1)
    {
        pthread_mutex_lock(&p->cond_mutex);
        pthread_cond_wait(&p->cond, &p->cond_mutex);
        if(p->destory_info.unfinished)
            do_destory_mpp_buf(p);
        pthread_mutex_unlock(&p->cond_mutex);
    }
}

#endif
static MPP_RET test_mpp_run(MpiEncTestData *p, int fd, size_t size)
{
#if RK_MPP_USE_ZERO_COPY
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppPacket packet = NULL;
    MppFrame frame = NULL;
    RK_S32 i;
    MppBuffer buf = NULL;
    MppBuffer pkt_buf_out = NULL;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi = p->mpi;
    ctx = p->ctx;

    ret = mpp_frame_init(&frame);
    if (ret)
    {
        printf("mpp_frame_init failed\n");
        goto RET;
    }

    mpp_frame_set_width(frame, p->width);
    mpp_frame_set_height(frame, p->height);
    mpp_frame_set_hor_stride(frame, p->hor_stride);
    mpp_frame_set_ver_stride(frame, p->ver_stride);
    mpp_frame_set_fmt(frame, p->fmt);

    MppTask task = NULL;
    RK_S32 index = i++;
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
    struct uvc_buffer *uvc_buf;
    MppBufferInfo outputCommit;
//   memset(&outputCommit, 0, sizeof(outputCommit));
    outputCommit.type = MPP_BUFFER_TYPE_DRM;

    if ((!uvc_get_user_run_state(uvc_enc.video_id) || !uvc_buffer_write_enable(uvc_enc.video_id)))
    {
        LOG_ERROR("not get write buff,read too slow.\n");
        return ret;
    }

    uvc_buf = uvc_buffer_write_get(uvc_enc.video_id);
    if (!uvc_buf)
    {
        printf("uvc_buffer_write_get failed\n");
        goto RET;
    }

    outputCommit.size = uvc_buf->drm_buf_size;
    outputCommit.fd = uvc_buf->fd;
    ret = mpp_buffer_import(&pkt_buf_out, &outputCommit);
    if (ret)
    {
        printf("import output picture buffer failed\n");
        goto RET;
    }
#else
    pkt_buf_out = p->pkt_buf;
#endif
    mpp_packet_init_with_buffer(&packet, pkt_buf_out);
    mpp_packet_set_length(packet, 0);

#if 0
    mpp_frame_set_buffer(frame, p->frm_buf);
#else
    MppBufferInfo inputCommit;
//    memset(&inputCommit, 0, sizeof(inputCommit));
    inputCommit.type = MPP_BUFFER_TYPE_ION;
    inputCommit.size = size;
    inputCommit.fd = fd;
    ret = mpp_buffer_import(&buf, &inputCommit);
    if (ret)
    {
        printf("import input picture buffer failed\n");
        goto RET;
    }
    mpp_frame_set_buffer(frame, buf);
#endif
    if (p->enc_version == 1)
    {
        //no need to get the sps/pps in addition
        if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)  //force set idr when begin enc
        {
            if (p->h2645_frm_count < (p->common_cfg.force_idr_count * p->common_cfg.force_idr_period))
            {
                if ((p->h2645_frm_count % p->common_cfg.force_idr_period) == 0)
                {
                    ret = mpi->control(ctx, MPP_ENC_SET_IDR_FRAME, NULL);
                    if (ret)
                    {
                        LOG_INFO("mpi force idr frame control failed\n");
                        goto RET;
                    }
                    else
                    {
                        LOG_INFO("mpi force idr frame control ok, h2645_frm_count:%d\n", p->h2645_frm_count);
                    }
                }
                p->h2645_frm_count++;
            }
        }
    }

    ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret)
    {
        printf("mpp task input poll failed ret %d\n", ret);
        goto RET;
    }

    ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
    if (ret || NULL == task)
    {
        printf("mpp task input dequeue failed ret %d task %p\n", ret, task);
        goto RET;
    }

    mpp_task_meta_set_frame(task, KEY_INPUT_FRAME,  frame);
    mpp_task_meta_set_packet(task, KEY_OUTPUT_PACKET, packet);
    //800us+

    ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
    if (ret)
    {
        printf("mpp task input enqueue failed\n");
        goto RET;
    }
    //800us+
#if 1
    ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret)
    {
        printf("mpp task output poll failed ret %d\n", ret);
        goto RET;
    }//9028 us for 1080p
#else
    do
    {
        ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_NON_BLOCK);
        usleep(100);
    }
    while (ret);//8882 us for 1080p
#endif

    ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task);
    if (ret || NULL == task)
    {
        printf("mpp task output dequeue failed ret %d task %p\n", ret, task);
        goto RET;
    }

    if (task)
    {
        MppFrame packet_out = NULL;
        mpp_task_meta_get_packet(task, KEY_OUTPUT_PACKET, &packet_out);
        assert(packet_out == packet);
        if (packet)
        {
            // write packet to file here
            size_t len = mpp_packet_get_length(packet);
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
            uvc_buf->size = len;
            uvc_buffer_read_set(uvc_enc.video_id, uvc_buf);
#else
            void *ptr = mpp_packet_get_pos(packet);
#endif
            if (p->fp_output)
            {
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
                fwrite(uvc_buf->buffer, 1, len, p->fp_output);
#else
                fwrite(ptr, 1, len, p->fp_output);
#endif
#if RK_MPP_DYNAMIC_DEBUG_ON
                if (access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0))
                {
                    fclose(p->fp_output);
                    p->fp_output = NULL;
                    printf("debug out file close\n");
                }
            }
            else if (!access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0))
            {
                p->fp_output = fopen(RK_MPP_DEBUG_OUT_FILE, "w+b");
                if (p->fp_output)
                {
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
                    fwrite(uvc_buf->buffer, 1, len, p->fp_output);
#else
                    fwrite(ptr, 1, len, p->fp_output);
#endif
                    printf("debug out file open\n");
                }
#endif
            }
#ifndef RK_MPP_USE_UVC_VIDEO_BUFFER
            p->enc_data = ptr;
            p->enc_len = len;
#endif
            mpp_packet_deinit(&packet);
        }
        //9175 us for 1080p
        p->frame_count++;
    }

    ret = mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
    if (ret)
    {
        printf("mpp task output enqueue failed\n");
        goto RET;
    }//9195 us for 1080p

RET:
#ifdef RK_MPP_USE_DESTORY_BUFF_THREAD
    pthread_mutex_lock(&p->cond_mutex);
    if (p->destory_info.unfinished == false)
    {
        p->destory_info.unfinished = true;
        p->destory_info.destory_frame = frame;
        p->destory_info.destory_buf = buf;
        p->destory_info.destory_pkt_buf_out = pkt_buf_out;
        //if (p->frame_count % 100 != 0) //for test
        pthread_cond_signal(&p->cond);
    }
    else
    {
        //pthread_cond_signal(&p->cond);
        do_destory_mpp_buf(p);
        LOG_INFO("not go here normal,count=%d,frm=%d\n",p->destory_info.count, p->frame_count);
#endif
        if (frame)
        {
            mpp_frame_deinit(&frame);
            frame = NULL;
        }
        if (buf)
        {
            mpp_buffer_put(buf);
            buf = NULL;
        }
#ifdef RK_MPP_USE_UVC_VIDEO_BUFFER
        if (pkt_buf_out)
        {
            mpp_buffer_put(pkt_buf_out);
            pkt_buf_out = NULL;
        }
#endif
#ifdef RK_MPP_USE_DESTORY_BUFF_THREAD
    }
    pthread_mutex_unlock(&p->cond_mutex);
#endif

    return ret;
#else
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppBuffer buf = NULL;
    if (NULL == p)
        return MPP_ERR_NULL_PTR;
#if 1
    mpi = p->mpi;
    ctx = p->ctx;

    if (p->enc_version == 1)
    {
        //no need to get the sps/pps in addition
        if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)  //force set idr when begin enc
        {
            if (p->h2645_frm_count < (p->common_cfg.force_idr_count * p->common_cfg.force_idr_period))
            {
                if ((p->h2645_frm_count % p->common_cfg.force_idr_period) == 0)
                {
                    ret = mpi->control(ctx, MPP_ENC_SET_IDR_FRAME, NULL);
                    if (ret)
                    {
                        LOG_INFO("mpi force idr frame control failed\n");
                        goto RET;
                    }
                    else
                    {
                        LOG_INFO("mpi force idr frame control ok, h2645_frm_count:%d\n", p->h2645_frm_count);
                    }
                }
                p->h2645_frm_count++;
            }
        }
    }
    else
    {
        if (p->type == MPP_VIDEO_CodingAVC)
        {
            MppPacket packet = NULL;

            ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
            if (ret)
            {
                LOG_ERROR("mpi control enc get extra info failed\n");
                goto RET;
            }

            /* get and write sps/pps for H.264 */
            if (packet)
            {
                void *ptr   = mpp_packet_get_pos(packet);
                size_t len  = mpp_packet_get_length(packet);

                if (p->fp_output)
                    fwrite(ptr, 1, len, p->fp_output);

                packet = NULL;
            }
        }
    }

    do
    {
        MppFrame frame = NULL;

        if (p->packet)
            mpp_packet_deinit(&p->packet);
        p->packet = NULL;

        ret = mpp_frame_init(&frame);
        if (ret)
        {
            LOG_ERROR("mpp_frame_init failed\n");
            goto RET;
        }

        mpp_frame_set_width(frame, p->width);
        mpp_frame_set_height(frame, p->height);
        mpp_frame_set_hor_stride(frame, p->hor_stride);
        mpp_frame_set_ver_stride(frame, p->ver_stride);
        mpp_frame_set_fmt(frame, p->fmt);
#if 0
        mpp_frame_set_buffer(frame, p->frm_buf);
#else
        MppBufferInfo inputCommit;
        memset(&inputCommit, 0, sizeof(inputCommit));
        inputCommit.type = MPP_BUFFER_TYPE_ION;
        inputCommit.size = size;
        inputCommit.fd = fd;
        ret = mpp_buffer_import(&buf, &inputCommit);
        if (ret)
        {
            LOG_ERROR("import input picture buffer failed\n");
            goto RET;
        }
        mpp_frame_set_buffer(frame, buf);
#endif
        mpp_frame_set_eos(frame, p->frm_eos);

        ret = mpi->encode_put_frame(ctx, frame);
        if (ret)
        {
            LOG_ERROR("mpp encode put frame failed\n");
            mpp_frame_deinit(&frame);
            goto RET;
        }
        mpp_frame_deinit(&frame);

        ret = mpi->encode_get_packet(ctx, &p->packet);
        if (ret)
        {
            LOG_ERROR("mpp encode get packet failed\n");
            goto RET;
        }

        if (p->packet)
        {
            // write packet to file here
            void *ptr   = mpp_packet_get_pos(p->packet);
            size_t len  = mpp_packet_get_length(p->packet);

            p->pkt_eos = mpp_packet_get_eos(p->packet);

            if (p->fp_output)
            {
                fwrite(ptr, 1, len, p->fp_output);
#if RK_MPP_DYNAMIC_DEBUG_ON
                if (access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0))
                {
                    fclose(p->fp_output);
                    p->fp_output = NULL;
                    LOG_INFO("debug out file close\n");
                }
            }
            else if (!access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0))
            {
                p->fp_output = fopen(RK_MPP_DEBUG_OUT_FILE, "w+b");
                if (p->fp_output)
                {
                    fwrite(ptr, 1, len, p->fp_output);
                    LOG_INFO("debug out file open\n");
                }
#endif
            }

            mpp_packet_deinit(&p->packet);
            p->enc_data = ptr;
            p->enc_len = len;
        }
    }
    while (0);
#endif

RET:

    if (buf)
    {
        mpp_buffer_put(buf);
        buf = NULL;
    }
    return ret;
#endif
}

MPP_RET mpi_enc_test_init(MpiEncTestCmd *cmd, MpiEncTestData **data)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = NULL;

    LOG_INFO("mpi_enc_test start\n");

    ret = test_ctx_init(&p, cmd);
    if (ret)
    {
        LOG_ERROR("test data init failed ret %d\n", ret);
        return ret;
    }
    *data = p;

#if 0
    ret = mpp_buffer_get(NULL, &p->frm_buf, p->frame_size);
    if (ret)
    {
        LOG_INFO("failed to get buffer for input frame ret %d\n", ret);
        return ret;
    }
#endif

    LOG_INFO("mpi_enc_test encoder test start w %d h %d type %d\n",
             p->width, p->height, p->type);

    // encoder demo
    ret = mpp_create(&p->ctx, &p->mpi);
    if (ret)
    {
        LOG_ERROR("mpp_create failed ret %d\n", ret);
        return ret;
    }

    ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
    if (ret)
    {
        LOG_ERROR("mpp_init failed ret %d\n", ret);
        return ret;
    }
    ret = mpp_enc_cfg_init(&p->cfg);
    if (ret)
    {
        LOG_ERROR("mpp_enc_cfg_init failed ret %d\n", ret);
        return ret;
    }
#if RK_MPP_USE_ZERO_COPY
#ifndef RK_MPP_USE_UVC_VIDEO_BUFFER
    ret = mpp_buffer_group_get_internal(&p->pkt_grp, MPP_BUFFER_TYPE_ION);
    if (ret)
    {
        LOG_ERROR("failed to get buffer group for output packet ret %d\n", ret);
        return ret;
    }
    p->packet_size = p->width * p->height;

    ret = mpp_buffer_get(p->pkt_grp, &p->pkt_buf, p->packet_size);
    if (ret)
    {
        LOG_ERROR("failed to get buffer for pkt_buf ret %d\n", ret);
        return ret;
    }
#endif
#endif
    mpp_enc_cfg_default(p);

    if (!check_mpp_enc_cfg_file_init(p))
        LOG_INFO("check_mpp_enc_cfg_file ok\n");
    dump_mpp_enc_cfg(p);
    ret = mpp_enc_cfg_set(p, true);
    if (ret)
    {
        LOG_ERROR("mpp_enc_cfg_set failed ret %d\n", ret);
        return ret;
    }
    pthread_create(&p->check_cfg_change_hd, NULL, thread_check_mpp_enc_chenge_loop, p);
#ifdef RK_MPP_USE_DESTORY_BUFF_THREAD
    pthread_create(&p->destory_buf_hd, NULL, thread_destory_mpp_buf, p);
#endif
}

MPP_RET mpi_enc_test_run(MpiEncTestData **data, int fd, size_t size)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = *data;

    ret = test_mpp_run(p, fd, size);
    if (ret)
        LOG_ERROR("test mpp run failed ret %d\n", ret);
    return ret;
}

MPP_RET mpi_enc_test_deinit(MpiEncTestData **data)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = *data;
#ifdef RK_MPP_USE_DESTORY_BUFF_THREAD
    pthread_cancel(p->destory_buf_hd);
    pthread_join(p->destory_buf_hd, NULL);
    pthread_mutex_destroy(&p->cond_mutex);
    pthread_cond_destroy(&p->cond);
#endif
    pthread_cancel(p->check_cfg_change_hd);
    pthread_join(p->check_cfg_change_hd, NULL);
    if (p->cfg_notify_fd)
    {
        inotify_rm_watch(p->cfg_notify_fd, p->cfg_notify_wd);
        close(p->cfg_notify_fd);
    }
    if (p->packet)
        mpp_packet_deinit(&p->packet);
    ret = p->mpi->reset(p->ctx);
    if (ret)
    {
        LOG_ERROR("mpi->reset failed\n");
    }
    if (p->ctx)
    {
        mpp_destroy(p->ctx);
        p->ctx = NULL;
    }

#if 0
    if (p->frm_buf)
    {
        mpp_buffer_put(p->frm_buf);
        p->frm_buf = NULL;
    }
#endif

#if RK_MPP_USE_ZERO_COPY
#ifndef RK_MPP_USE_UVC_VIDEO_BUFFER
    if (p->pkt_buf)
    {
        mpp_buffer_put(p->pkt_buf);
        p->pkt_buf = NULL;
    }

    if (p->pkt_grp)
    {
        mpp_buffer_group_put(p->pkt_grp);
        p->pkt_grp = NULL;
    }
#endif
#endif

    if (MPP_OK == ret)
    {
        LOG_INFO("mpi_enc_test success \n");
        //LOG_INFO("mpi_enc_test success total frame %d bps %lld\n",
        //        p->frame_count, (RK_U64)((p->stream_size * 8 * p->fps) / p->frame_count));
    }
    else
    {
        LOG_ERROR("mpi_enc_test failed ret %d\n", ret);
    }
    test_ctx_deinit(&p);

    return ret;
}

void mpi_enc_cmd_config(MpiEncTestCmd *cmd, int width, int height, int fcc, int h265)
{
    memset((void *)cmd, 0, sizeof(*cmd));
    cmd->width = width;
    cmd->height = height;
    cmd->format = g_format;

    char* env_h265 = getenv("ENABLE_UVC_H265");
    if (env_h265) {
        h265 = atoi(env_h265);
        LOG_INFO("V4L2_PIX_FMT_H264 force use h265 ?:%d \n",h265);
    }

    switch (fcc)
    {
    case V4L2_PIX_FMT_YUYV:
        LOG_INFO("%s: yuyv not need mpp encodec: %d\n", __func__, fcc);
        break;
    case V4L2_PIX_FMT_MJPEG:
        cmd->type = MPP_VIDEO_CodingMJPEG;
        break;
    case V4L2_PIX_FMT_H264:
    {
        if(h265)
           cmd->type = MPP_VIDEO_CodingHEVC;
        else
           cmd->type = MPP_VIDEO_CodingAVC;//MPP_VIDEO_CodingAVC;//MPP_VIDEO_CodingHEVC
        break;
    }
    default:
        LOG_INFO("%s: not support fcc: %d\n", __func__, fcc);
        break;
    }

}

void mpi_enc_cmd_config_mjpg(MpiEncTestCmd *cmd, int width, int height)
{
    memset((void *)cmd, 0, sizeof(*cmd));
    cmd->width = width;
    cmd->height = height;
    cmd->format = g_format;
    cmd->type = MPP_VIDEO_CodingMJPEG;
}

void mpi_enc_cmd_config_h264(MpiEncTestCmd *cmd, int width, int height)
{
    memset((void *)cmd, 0, sizeof(*cmd));
    cmd->width = width;
    cmd->height = height;
    cmd->format = g_format;
    cmd->type = MPP_VIDEO_CodingAVC;
}
void mpi_enc_set_format(MppFrameFormat format)
{
    g_format = format;
}
int mpi_enc_get_h264_extra(MpiEncTestData *p, void *buffer, size_t *size)
{
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    if (NULL == p)
    {
        *size = 0;
        return -1;
    }
    mpi = p->mpi;
    ctx = p->ctx;
    MppPacket packet = NULL;
    ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
    if (ret)
    {
        LOG_ERROR("mpi control enc get extra info failed\n");
        *size = 0;
        return -1;
    }
    if (packet)
    {
        void *ptr   = mpp_packet_get_pos(packet);
        size_t len  = mpp_packet_get_length(packet);
        LOG_INFO("%s: len = %d\n", __func__, len);
        if (*size >= len)
        {
            memcpy(buffer, ptr, len);
            *size = len;
        }
        else
        {
            LOG_INFO("%s: input buffer size = %d\n", __func__, *size);
            *size = 0;
        }
        packet = NULL;
    }
    return 0;
}

static unsigned long get_file_size(const char *filename)
{
    struct stat buf;
    if (stat(filename, &buf) < 0)
        return 0;
    return (unsigned long)buf.st_size;
}

static void mpp_enc_cfg_default(MpiEncTestData *p)
{
    if (NULL == p)
        return -1;

    //common set
    p->common_cfg.fbc = false;
    p->common_cfg.split_mode = 0;
    p->common_cfg.split_arg = 0;
    p->common_cfg.force_idr_count = RK_MPP_H264_FORCE_IDR_COUNT;
    p->common_cfg.force_idr_period = RK_MPP_H264_FORCE_IDR_PERIOD;

    //mjpeg set
    p->mjpeg_cfg.quant = 7;
    p->mjpeg_cfg.range = MPP_FRAME_RANGE_JPEG; //default for full(only full)

    //h264 set
    p->h264_cfg.gop = 60;
    p->h264_cfg.rc_mode = MPP_ENC_RC_MODE_CBR;
    p->h264_cfg.framerate = 30;
    p->h264_cfg.range = MPP_FRAME_RANGE_JPEG; //default for full
    p->h264_cfg.head_each_idr = true;
    p->h264_cfg.sei = MPP_ENC_SEI_MODE_DISABLE;
    p->h264_cfg.qp.init = 26;
    p->h264_cfg.qp.max = 48;
    p->h264_cfg.qp.min = 8;
    p->h264_cfg.qp.step = 8;
    p->h264_cfg.profile = MPP_ENC_CFG_H264_DEFAULT_PROFILE;
    p->h264_cfg.cabac_en = 1;
    p->h264_cfg.cabac_idc = 0;
    p->h264_cfg.trans_8x8 = 1;
    p->h264_cfg.level = MPP_ENC_CFG_H264_DEFAULT_LEVEL;
    p->h264_cfg.bps = p->width * p->height / 8 * p->h264_cfg.framerate / 2;

    //h265 set
    p->h265_cfg.gop = 60;
    p->h265_cfg.rc_mode = MPP_ENC_RC_MODE_CBR;
    p->h265_cfg.framerate = 30;
    p->h265_cfg.range = MPP_FRAME_RANGE_JPEG; //default for full
    p->h265_cfg.head_each_idr = true;
    p->h265_cfg.sei = MPP_ENC_SEI_MODE_DISABLE;
    p->h265_cfg.qp.init = 24;
    p->h265_cfg.qp.max = 51;
    p->h265_cfg.qp.min = 10;
    p->h265_cfg.qp.step = 4;
    p->h265_cfg.qp.max_i_qp = 46;
    p->h265_cfg.qp.min_i_qp = 24;
    p->h265_cfg.bps = p->width * p->height / 8 * p->h265_cfg.framerate / 2;

}

static void dump_mpp_enc_cfg(MpiEncTestData *p)
{
    LOG_INFO("### dump_mpp_enc_cfg for common cfg:\n");
    LOG_INFO("fbc=%d,split_mode=%d,split_arg=%d,force_idr_count=%d,force_idr_period=%d\n",
             p->common_cfg.fbc, p->common_cfg.split_mode, p->common_cfg.split_arg,
             p->common_cfg.force_idr_count, p->common_cfg.force_idr_period);

    LOG_INFO("###dump_mpp_enc_cfg for mjpeg cfg:\n");
    LOG_INFO("quant=%d,range=%d\n", p->mjpeg_cfg.quant, p->mjpeg_cfg.range);

    LOG_INFO("### dump_mpp_enc_cfg for h264 cfg:\n");
    LOG_INFO("gop=%d,re_mode=%d,framerate=%d,range=%d,head_each_idr=%d \n\
sei=%d,qp.init=%d,qp.max=%d,qp.min=%d,qp.step=%d,profile=%d \n\
cabac_en=%d,cabac_idc=%d,trans_8x8=%d,level=%d,bps=%d \n",
             p->h264_cfg.gop, p->h264_cfg.rc_mode, p->h264_cfg.framerate,
             p->h264_cfg.range, p->h264_cfg.head_each_idr,
             p->h264_cfg.sei, p->h264_cfg.qp.init,
             p->h264_cfg.qp.max, p->h264_cfg.qp.min,
             p->h264_cfg.qp.step, p->h264_cfg.profile,
             p->h264_cfg.cabac_en, p->h264_cfg.cabac_idc,
             p->h264_cfg.trans_8x8, p->h264_cfg.level,
             p->h264_cfg.bps);

    LOG_INFO("### dump_mpp_enc_cfg for h265 cfg:\n");
    LOG_INFO("gop=%d,re_mode=%d,framerate=%d,range=%d,head_each_idr=%d \n\
sei=%d,qp.init=%d,qp.max=%d,qp.min=%d,qp.step=%d,max_i_qp=%d \n\
min_i_qp=%d,bps=%d \n",
             p->h265_cfg.gop, p->h265_cfg.rc_mode, p->h265_cfg.framerate,
             p->h265_cfg.range, p->h265_cfg.head_each_idr,
             p->h265_cfg.sei, p->h265_cfg.qp.init,
             p->h265_cfg.qp.max, p->h265_cfg.qp.min,
             p->h265_cfg.qp.step, p->h265_cfg.qp.max_i_qp,
             p->h265_cfg.qp.min_i_qp, p->h265_cfg.bps);

}

static int parse_check_mpp_enc_cfg(cJSON *root, MpiEncTestData *p, bool init)
{
    int ret = 0;

    if (NULL == p)
        return -1;

    LOG_INFO("parse_mpp_enc_cfg type: %d init:%d\n", p->type, init);
    if (!init)
    {
        p->common_cfg.change = 0;
        p->mjpeg_cfg.change = 0;
        p->h264_cfg.change = 0;
        p->h265_cfg.change = 0;
    }
    cJSON *child = cJSON_GetObjectItem(root, "mpp_enc_cfg");
    if (!child)
    {
        LOG_ERROR("parse_mpp_enc_cfg mpp_enc_cfg err\n");
        return -1;
    }

    cJSON *child_common = cJSON_GetObjectItem(child, "common");
    if (!child_common)
    {
        LOG_ERROR("parse_mpp_enc_cfg common err\n");
        return -1;
    }
    else
    {
        cJSON *child_common_param = NULL;
        if (init)
            child_common_param = cJSON_GetObjectItem(child_common, "param_init");
        else
            child_common_param = cJSON_GetObjectItem(child_common, "param_change");
        if (child_common_param)
        {
            cJSON *child_common_fbc = cJSON_GetObjectItem(child_common_param, "fbc");
            if (child_common_fbc)
            {
                p->common_cfg.fbc = strstr(child_common_fbc->valuestring, "off") ? 0 : MPP_FRAME_FBC_AFBC_V1;
                p->common_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(0);
            }
            cJSON *child_common_split_mode = cJSON_GetObjectItem(child_common_param, "split_mode");
            if (child_common_split_mode)
            {
                p->common_cfg.split_mode = strstr(child_common_split_mode->valuestring, "none") ? MPP_ENC_SPLIT_NONE :
                                           strstr(child_common_split_mode->valuestring, "byte") ? MPP_ENC_SPLIT_BY_BYTE :
                                           strstr(child_common_split_mode->valuestring, "ctu") ? MPP_ENC_SPLIT_BY_CTU : MPP_ENC_SPLIT_NONE;
                p->common_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(1);
            }
            cJSON *child_common_split_arg = cJSON_GetObjectItem(child_common_param, "split_arg");
            if (child_common_split_arg)
            {
                p->common_cfg.split_arg = child_common_split_arg->valueint;
                p->common_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(2);
            }
            cJSON *child_common_idr_count = cJSON_GetObjectItem(child_common_param, "force_idr_count");
            if (child_common_idr_count)
            {
                p->common_cfg.force_idr_count = child_common_idr_count->valueint;
                p->common_cfg.force_idr_count = p->common_cfg.force_idr_count < 0 ? 0 :
                                                p->common_cfg.force_idr_count > 100 ? 100 :
                                                p->common_cfg.force_idr_count;
                p->common_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(3);
            }
            cJSON *child_common_idr_period = cJSON_GetObjectItem(child_common_param, "force_idr_period");
            if (child_common_idr_period)
            {
                p->common_cfg.force_idr_period = child_common_idr_period->valueint;
                p->common_cfg.force_idr_period = p->common_cfg.force_idr_period < 1 ? 1 :
                                                 p->common_cfg.force_idr_period > 100 ? 100 :
                                                 p->common_cfg.force_idr_period;
                p->common_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(4);
            }
            LOG_INFO("common_cfg.change:0x%x\n", p->common_cfg.change);
        }
        else
        {
            LOG_INFO("no find common param_init or param_change\n");
        }
    }

    switch (p->type)
    {
        case MPP_VIDEO_CodingMJPEG :
        {
            cJSON *child_mjpeg = cJSON_GetObjectItem(child, "mjpeg");
            if (!child_mjpeg)
            {
                LOG_ERROR("parse_mpp_enc_cfg mjpeg err\n");
                return -1;
            }
            else
            {
                cJSON *child_mjpeg_param = NULL;
                if (init)
                    child_mjpeg_param = cJSON_GetObjectItem(child_mjpeg, "param_init");
                else
                    child_mjpeg_param = cJSON_GetObjectItem(child_mjpeg, "param_change");
                if (child_mjpeg_param)
                {
                    cJSON *child_mjpeg_quant = cJSON_GetObjectItem(child_mjpeg_param, "quant");
                    if (child_mjpeg_quant)
                    {
                        p->mjpeg_cfg.quant = child_mjpeg_quant->valueint;
                        p->mjpeg_cfg.quant = p->mjpeg_cfg.quant < 1 ? 1 :
                                             p->mjpeg_cfg.quant > 10 ? 10 :
                                             p->mjpeg_cfg.quant;
                        p->mjpeg_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(0);
                    }
                    cJSON *child_mjpeg_range = cJSON_GetObjectItem(child_mjpeg_param, "range");
                    if (child_mjpeg_range)
                    {
                        p->mjpeg_cfg.range = strstr(child_mjpeg_range->valuestring, "limit") ?
                                             MPP_FRAME_RANGE_MPEG : MPP_FRAME_RANGE_JPEG;
                        p->mjpeg_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(1);
                    }
                    LOG_INFO("mjpeg_cfg.change:0x%x\n", p->mjpeg_cfg.change);
                }
                else
                {
                    LOG_INFO("no find mjpeg param_init or param_change\n");
                }
            }
        }
        break;
        case MPP_VIDEO_CodingAVC :
        {
            cJSON *child_h264 = cJSON_GetObjectItem(child, "h264");
            if (!child_h264)
            {
                LOG_ERROR("parse_mpp_enc_cfg h264 err\n");
                return -1;
            }
            else
            {
                cJSON *child_h264_param = NULL;
                if (init)
                    child_h264_param = cJSON_GetObjectItem(child_h264, "param_init");
                else
                    child_h264_param = cJSON_GetObjectItem(child_h264, "param_change");
                if (child_h264_param)
                {
                    cJSON *child_h264_gop = cJSON_GetObjectItem(child_h264_param, "gop");
                    if (child_h264_gop)
                    {
                        p->h264_cfg.gop = child_h264_gop->valueint;
                        p->h264_cfg.gop = p->h264_cfg.gop < 1 ? 1 :
                                          p->h264_cfg.gop > 100 ? 100 :
                                          p->h264_cfg.gop;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(0);
                    }
                    cJSON *child_h264_rc_mode = cJSON_GetObjectItem(child_h264_param, "rc_mode");
                    if (child_h264_rc_mode)
                    {
                        p->h264_cfg.rc_mode = strstr(child_h264_rc_mode->valuestring, "cbr") ? MPP_ENC_RC_MODE_CBR :
                                              strstr(child_h264_rc_mode->valuestring, "vbr") ? MPP_ENC_RC_MODE_VBR :
                                              strstr(child_h264_rc_mode->valuestring, "fixqp") ? MPP_ENC_RC_MODE_FIXQP : MPP_ENC_RC_MODE_CBR;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(1);
                    }
                    cJSON *child_h264_framerate = cJSON_GetObjectItem(child_h264_param, "framerate");
                    if (child_h264_framerate)
                    {
                        p->h264_cfg.framerate = child_h264_framerate->valueint;
                        p->h264_cfg.framerate = p->h264_cfg.framerate < MPP_ENC_CFG_MIN_FPS ? MPP_ENC_CFG_MIN_FPS :
                                                p->h264_cfg.framerate > MPP_ENC_CFG_MAX_FPS ? MPP_ENC_CFG_MAX_FPS :
                                                p->h264_cfg.framerate;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(2);
                    }
                    cJSON *child_h264_range = cJSON_GetObjectItem(child_h264_param, "range");
                    if (child_h264_range)
                    {
                        p->h264_cfg.range = strstr(child_h264_range->valuestring, "limit") ?
                                            MPP_FRAME_RANGE_MPEG : MPP_FRAME_RANGE_JPEG;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(3);
                    }
                    cJSON *child_h264_head_each_idr = cJSON_GetObjectItem(child_h264_param, "head_each_idr");
                    if (child_h264_head_each_idr)
                    {
                        p->h264_cfg.head_each_idr = strstr(child_h264_head_each_idr->valuestring, "on") ?
                                                    MPP_ENC_HEADER_MODE_EACH_IDR : MPP_ENC_HEADER_MODE_DEFAULT;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(4);
                    }
                    cJSON *child_h264_sei = cJSON_GetObjectItem(child_h264_param, "sei");
                    if (child_h264_sei)
                    {
                        p->h264_cfg.sei = strstr(child_h264_sei->valuestring, "SEQ") ? MPP_ENC_SEI_MODE_ONE_SEQ :
                                          strstr(child_h264_sei->valuestring, "FRAME") ? MPP_ENC_SEI_MODE_ONE_FRAME :
                                          MPP_ENC_SEI_MODE_DISABLE;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(5);
                    }
                    cJSON *child_h264_qp_init = cJSON_GetObjectItem(child_h264_param, "qp_init");
                    if (child_h264_qp_init)
                    {
                        p->h264_cfg.qp.init = child_h264_qp_init->valueint;
                        p->h264_cfg.qp.init = p->h264_cfg.qp.init < 1 ? 1 :
                                              p->h264_cfg.qp.init > 51 ? 51 :
                                              p->h264_cfg.qp.init;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(6);
                    }
                    cJSON *child_h264_qp_max = cJSON_GetObjectItem(child_h264_param, "qp_max");
                    if (child_h264_qp_max)
                    {
                        p->h264_cfg.qp.max = child_h264_qp_max->valueint;
                        p->h264_cfg.qp.max = p->h264_cfg.qp.max < 8 ? 8 :
                                             p->h264_cfg.qp.max > 51 ? 51 :
                                             p->h264_cfg.qp.max;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(7);
                    }
                    cJSON *child_h264_qp_min = cJSON_GetObjectItem(child_h264_param, "qp_min");
                    if (child_h264_qp_min)
                    {
                        p->h264_cfg.qp.min = child_h264_qp_min->valueint;
                        p->h264_cfg.qp.min = p->h264_cfg.qp.min < 0 ? 0 :
                                             p->h264_cfg.qp.min > 48 ? 48 :
                                             p->h264_cfg.qp.min;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(8);
                    }
                    cJSON *child_h264_qp_step = cJSON_GetObjectItem(child_h264_param, "qp_step");
                    if (child_h264_qp_step)
                    {
                        p->h264_cfg.qp.step = child_h264_qp_step->valueint;
                        p->h264_cfg.qp.step = p->h264_cfg.qp.step < 1 ? 1 :
                                              p->h264_cfg.qp.step > 51 ? 51 :
                                              p->h264_cfg.qp.step;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(9);
                    }
                    cJSON *child_h264_profile = cJSON_GetObjectItem(child_h264_param, "profile");
                    if (child_h264_profile)
                    {
                        p->h264_cfg.profile = child_h264_profile->valueint;
                        if (p->h264_cfg.profile != 66 && p->h264_cfg.profile != 77 && p->h264_cfg.profile != 100)
                        {
                            LOG_INFO("set h264_cfg.profile err %d, set default to %d\n",
                                     p->h264_cfg.profile, MPP_ENC_CFG_H264_DEFAULT_PROFILE);
                            p->h264_cfg.profile = MPP_ENC_CFG_H264_DEFAULT_PROFILE;
                        }
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(10);
                    }
                    cJSON *child_h264_cabac_en = cJSON_GetObjectItem(child_h264_param, "cabac_en");
                    if (child_h264_cabac_en)
                    {
                        p->h264_cfg.cabac_en = child_h264_cabac_en->valueint;
                        p->h264_cfg.cabac_en = p->h264_cfg.cabac_en < 0 ? 0 :
                                               p->h264_cfg.cabac_en > 1 ? 1 :
                                               p->h264_cfg.cabac_en;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(11);
                    }
                    cJSON *child_h264_cabac_idc = cJSON_GetObjectItem(child_h264_param, "cabac_idc");
                    if (child_h264_cabac_idc)
                    {
                        p->h264_cfg.cabac_idc = child_h264_cabac_idc->valueint;
                        p->h264_cfg.cabac_idc = p->h264_cfg.cabac_idc < 0 ? 0 :
                                                p->h264_cfg.cabac_idc > 1 ? 1 :
                                                p->h264_cfg.cabac_idc;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(12);
                    }
                    cJSON *child_h264_trans_8x8 = cJSON_GetObjectItem(child_h264_param, "trans_8x8");
                    if (child_h264_trans_8x8)
                    {
                        p->h264_cfg.trans_8x8 = child_h264_trans_8x8->valueint;
                        p->h264_cfg.trans_8x8 = p->h264_cfg.trans_8x8 < 0 ? 0 :
                                                p->h264_cfg.trans_8x8 > 1 ? 1 :
                                                p->h264_cfg.trans_8x8;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(13);
                    }
                    cJSON *child_h264_level = cJSON_GetObjectItem(child_h264_param, "level");
                    if (child_h264_level)
                    {
                        p->h264_cfg.level = child_h264_level->valueint;
                        if (!((p->h264_cfg.level >= 10 && p->h264_cfg.level <= 13) ||
                                (p->h264_cfg.level >= 20 && p->h264_cfg.level <= 22) ||
                                (p->h264_cfg.level >= 30 && p->h264_cfg.level <= 32) ||
                                (p->h264_cfg.level >= 40 && p->h264_cfg.level <= 42) ||
                                (p->h264_cfg.level >= 50 && p->h264_cfg.level <= 52)))
                        {
                            LOG_INFO("set h264_cfg.level err %d, set default to %d\n",
                                     p->h264_cfg.level, MPP_ENC_CFG_H264_DEFAULT_LEVEL);
                            p->h264_cfg.level = MPP_ENC_CFG_H264_DEFAULT_LEVEL;
                        }
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(14);
                    }
                    cJSON *child_h264_bps = cJSON_GetObjectItem(child_h264_param, "bps");
                    if (child_h264_bps)
                    {
                        p->h264_cfg.bps = child_h264_bps->valueint;
                        p->h264_cfg.bps = p->h264_cfg.bps < MPP_ENC_CFG_MIN_BPS ? MPP_ENC_CFG_MIN_BPS :
                                          p->h264_cfg.bps > MPP_ENC_CFG_MAX_BPS ? MPP_ENC_CFG_MAX_BPS :
                                          p->h264_cfg.bps;
                        p->h264_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(15);
                    }
                    LOG_INFO("h264_cfg.change:0x%x\n", p->h264_cfg.change);
                }
                else
                {
                    LOG_INFO("no find h264 param_init or param_change\n");
                }
            }
        }
        break;
        case MPP_VIDEO_CodingVP8 :
        {
            ret = -1;
        }
        break;
        case MPP_VIDEO_CodingHEVC :
        {
            cJSON *child_h265 = cJSON_GetObjectItem(child, "h265");
            if (!child_h265)
            {
                LOG_ERROR("parse_mpp_enc_cfg h265 err\n");
                return -1;
            }
            else
            {
                cJSON *child_h265_param = NULL;
                if (init)
                    child_h265_param = cJSON_GetObjectItem(child_h265, "param_init");
                else
                    child_h265_param = cJSON_GetObjectItem(child_h265, "param_change");
                if (child_h265_param)
                {
                    cJSON *child_h265_gop = cJSON_GetObjectItem(child_h265_param, "gop");
                    if (child_h265_gop)
                    {
                        p->h265_cfg.gop = child_h265_gop->valueint;
                        p->h265_cfg.gop = p->h265_cfg.gop < 1 ? 1 :
                                          p->h265_cfg.gop > 100 ? 100 :
                                          p->h265_cfg.gop;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(0);
                    }
                    cJSON *child_h265_rc_mode = cJSON_GetObjectItem(child_h265_param, "rc_mode");
                    if (child_h265_rc_mode)
                    {
                        p->h265_cfg.rc_mode = strstr(child_h265_rc_mode->valuestring, "cbr") ? MPP_ENC_RC_MODE_CBR :
                                              strstr(child_h265_rc_mode->valuestring, "vbr") ? MPP_ENC_RC_MODE_VBR :
                                              strstr(child_h265_rc_mode->valuestring, "fixqp") ? MPP_ENC_RC_MODE_FIXQP : MPP_ENC_RC_MODE_CBR;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(1);
                    }
                    cJSON *child_h265_framerate = cJSON_GetObjectItem(child_h265_param, "framerate");
                    if (child_h265_framerate)
                    {
                        p->h265_cfg.framerate = child_h265_framerate->valueint;
                        p->h265_cfg.framerate = p->h265_cfg.framerate < MPP_ENC_CFG_MIN_FPS ? MPP_ENC_CFG_MIN_FPS :
                                                p->h265_cfg.framerate > MPP_ENC_CFG_MAX_FPS ? MPP_ENC_CFG_MAX_FPS :
                                                p->h265_cfg.framerate;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(2);
                    }
                    cJSON *child_h265_range = cJSON_GetObjectItem(child_h265_param, "range");
                    if (child_h265_range)
                    {
                        p->h265_cfg.range = strstr(child_h265_range->valuestring, "limit") ?
                                            MPP_FRAME_RANGE_MPEG : MPP_FRAME_RANGE_JPEG;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(3);
                    }
                    cJSON *child_h265_head_each_idr = cJSON_GetObjectItem(child_h265_param, "head_each_idr");
                    if (child_h265_head_each_idr)
                    {
                        p->h265_cfg.head_each_idr = strstr(child_h265_head_each_idr->valuestring, "on") ?
                                                    MPP_ENC_HEADER_MODE_EACH_IDR : MPP_ENC_HEADER_MODE_DEFAULT;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(4);
                    }
                    cJSON *child_h265_sei = cJSON_GetObjectItem(child_h265_param, "sei");
                    if (child_h265_sei)
                    {
                        p->h265_cfg.sei = strstr(child_h265_sei->valuestring, "SEQ") ? MPP_ENC_SEI_MODE_ONE_SEQ :
                                          strstr(child_h265_sei->valuestring, "FRAME") ? MPP_ENC_SEI_MODE_ONE_FRAME :
                                          MPP_ENC_SEI_MODE_DISABLE;;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(5);
                    }
                    cJSON *child_h265_qp_init = cJSON_GetObjectItem(child_h265_param, "qp_init");
                    if (child_h265_qp_init)
                    {
                        p->h265_cfg.qp.init = child_h265_qp_init->valueint;
                        p->h265_cfg.qp.init = p->h265_cfg.qp.init < 1 ? 1 :
                                              p->h265_cfg.qp.init > 51 ? 51 :
                                              p->h265_cfg.qp.init;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(6);
                    }
                    cJSON *child_h265_qp_max = cJSON_GetObjectItem(child_h265_param, "qp_max");
                    if (child_h265_qp_max)
                    {
                        p->h265_cfg.qp.max = child_h265_qp_max->valueint;
                        p->h265_cfg.qp.max = p->h265_cfg.qp.max < 8 ? 8 :
                                             p->h265_cfg.qp.max > 51 ? 51 :
                                             p->h265_cfg.qp.max;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(7);
                    }
                    cJSON *child_h265_qp_min = cJSON_GetObjectItem(child_h265_param, "qp_min");
                    if (child_h265_qp_min)
                    {
                        p->h265_cfg.qp.min = child_h265_qp_min->valueint;
                        p->h265_cfg.qp.min = p->h265_cfg.qp.min < 0 ? 0 :
                                             p->h265_cfg.qp.min > 48 ? 48 :
                                             p->h265_cfg.qp.min;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(8);
                    }
                    cJSON *child_h265_qp_step = cJSON_GetObjectItem(child_h265_param, "qp_step");
                    if (child_h265_qp_step)
                    {
                        p->h265_cfg.qp.step = child_h265_qp_step->valueint;
                        p->h265_cfg.qp.step = p->h265_cfg.qp.step < 1 ? 1 :
                                              p->h265_cfg.qp.step > 51 ? 51 :
                                              p->h265_cfg.qp.step;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(9);
                    }
                    cJSON *child_h265_qp_max_i = cJSON_GetObjectItem(child_h265_param, "max_i_qp");
                    if (child_h265_qp_max_i)
                    {
                        p->h265_cfg.qp.max_i_qp = child_h265_qp_max_i->valueint;
                        p->h265_cfg.qp.max_i_qp = p->h265_cfg.qp.max_i_qp < 8 ? 8 :
                                                  p->h265_cfg.qp.max_i_qp > 51 ? 51 :
                                                  p->h265_cfg.qp.max_i_qp;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(10);
                    }
                    cJSON *child_h265_qp_min_i = cJSON_GetObjectItem(child_h265_param, "min_i_qp");
                    if (child_h265_qp_min_i)
                    {
                        p->h265_cfg.qp.min_i_qp = child_h265_qp_min_i->valueint;
                        p->h265_cfg.qp.min_i_qp = p->h265_cfg.qp.min_i_qp < 0 ? 0 :
                                                  p->h265_cfg.qp.min_i_qp > 48 ? 48 :
                                                  p->h265_cfg.qp.min_i_qp;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(11);
                    }
                    cJSON *child_h265_bps = cJSON_GetObjectItem(child_h265_param, "bps");
                    if (child_h265_bps)
                    {
                        p->h265_cfg.bps = child_h265_bps->valueint;
                        p->h265_cfg.bps = p->h265_cfg.bps < MPP_ENC_CFG_MIN_BPS ? MPP_ENC_CFG_MIN_BPS :
                                          p->h265_cfg.bps > MPP_ENC_CFG_MAX_BPS ? MPP_ENC_CFG_MAX_BPS :
                                          p->h265_cfg.bps;
                        p->h265_cfg.change |= MPP_ENC_CFG_CHANGE_BIT(12);
                    }

                    LOG_INFO("h265_cfg.change:0x%x\n", p->h265_cfg.change);
                }
                else
                {
                    LOG_INFO("no find h265 param_init or param_change\n");
                }
            }
        }
        break;
        default :
        {
            LOG_ERROR("unsupport encoder coding type %d\n", p->type);
            ret = -1;
        }
        break;
    }

    return ret;
}

static MPP_RET mpp_enc_cfg_set(MpiEncTestData *p, bool init)
{
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppEncCfg cfg;
    MppEncRcMode rc_mode = MPP_ENC_RC_MODE_CBR;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi_get_env_u32("enc_version", &p->enc_version, RK_MPP_VERSION_DEFAULT);

    char* full_range = getenv("ENABLE_FULL_RANGE");
    if (full_range) {
        int need_full_range = atoi(full_range);
        p->h264_cfg.range = need_full_range ? MPP_FRAME_RANGE_JPEG : MPP_FRAME_RANGE_MPEG;
        p->h265_cfg.range = need_full_range ? MPP_FRAME_RANGE_JPEG : MPP_FRAME_RANGE_MPEG;
        LOG_INFO("mpp full_range use env setting:%d \n",need_full_range);
    }

    mpi = p->mpi;
    ctx = p->ctx;
    cfg = p->cfg;

    if (init)
    {
        mpp_enc_cfg_set_s32(cfg, "prep:width", p->width);
        mpp_enc_cfg_set_s32(cfg, "prep:height", p->height);
        mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", p->hor_stride);
        mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", p->ver_stride);
    }
    if (init || (p->common_cfg.change & BIT(0)))
    {
        p->fmt |= p->common_cfg.fbc;
        mpp_enc_cfg_set_s32(cfg, "prep:format", p->fmt);
    }

    if (p->common_cfg.split_mode && (init || (p->common_cfg.change & BIT(1))))
    {
        mpp_enc_cfg_set_u32(cfg, "split:mode", p->common_cfg.split_mode);
    }

    if (p->common_cfg.split_mode && (init || (p->common_cfg.change & BIT(2))))
    {
        mpp_enc_cfg_set_u32(cfg, "split:arg", p->common_cfg.split_arg);
    }

    mpp_enc_cfg_set_s32(cfg, "codec:type", p->type);
    switch (p->type)
    {
        case MPP_VIDEO_CodingMJPEG :
        {
            if (init || (p->mjpeg_cfg.change & BIT(0)))
                mpp_enc_cfg_set_s32(cfg, "jpeg:quant", p->mjpeg_cfg.quant);
        }
        break;
        case MPP_VIDEO_CodingAVC :
        {
            if (init || (p->h264_cfg.change & BIT(0)))
                mpp_enc_cfg_set_s32(cfg, "rc:gop", p->h264_cfg.gop);
            if (init || (p->h264_cfg.change & BIT(1)))
                mpp_enc_cfg_set_s32(cfg, "rc:mode", p->h264_cfg.rc_mode);
            if (init || (p->h264_cfg.change & BIT(2)))
            {
                /* setup default parameter */
                p->fps_in_den = 1;
                p->fps_in_num = p->h264_cfg.framerate;
                p->fps_out_den = 1;
                p->fps_out_num = p->h264_cfg.framerate;
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", p->fps_in_flex);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", p->fps_in_num);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", p->fps_in_den);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", p->fps_out_flex);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", p->fps_out_num);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", p->fps_out_den);
            }
            if (init || (p->h264_cfg.change & BIT(3)))
            {
                ret = mpp_enc_cfg_set_s32(cfg, "prep:range", p->h264_cfg.range);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set prep:range failed ret %d\n", ret);
                    goto RET;
                }
            }

            if (init || (p->h264_cfg.change & BIT(4)))
            {
                ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &p->h264_cfg.head_each_idr);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set codec cfg failed ret %d\n", ret);
                    goto RET;
                }
            }
            if (init || (p->h264_cfg.change & BIT(5)))
            {
                ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->h264_cfg.sei);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set sei cfg failed ret %d\n", ret);
                    goto RET;
                }
            }
            if (init || (p->h264_cfg.change & BIT(6)))
                mpp_enc_cfg_set_s32(cfg, "h264:qp_init", p->h264_cfg.qp.init);
            if (init || (p->h264_cfg.change & BIT(7)))
                mpp_enc_cfg_set_s32(cfg, "h264:qp_max", p->h264_cfg.qp.max);
            if (init || (p->h264_cfg.change & BIT(8)))
                mpp_enc_cfg_set_s32(cfg, "h264:qp_min", p->h264_cfg.qp.min);
            if (init || (p->h264_cfg.change & BIT(9)))
                mpp_enc_cfg_set_s32(cfg, "h264:qp_step", p->h264_cfg.qp.step);
            if (init || (p->h264_cfg.change & BIT(10)))
                mpp_enc_cfg_set_s32(cfg, "h264:profile", p->h264_cfg.profile);
            if (init || (p->h264_cfg.change & BIT(11)))
                mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", p->h264_cfg.cabac_en);
            if (init || (p->h264_cfg.change & BIT(12)))
                mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", p->h264_cfg.cabac_idc);
            if (init || (p->h264_cfg.change & BIT(13)))
                mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", p->h264_cfg.trans_8x8);
            if (init || (p->h264_cfg.change & BIT(14)))
                mpp_enc_cfg_set_s32(cfg, "h264:level", p->h264_cfg.level);

            if (init || (p->h264_cfg.change & BIT(15)))
            {
                switch (rc_mode)
                {
                case MPP_ENC_RC_MODE_FIXQP :
                {
                    /* do not set bps on fix qp mode */
                } break;
                case MPP_ENC_RC_MODE_CBR :
                {
                    /* CBR mode has narrow bound */
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->h264_cfg.bps);
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->h264_cfg.bps * 17 / 16);
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->h264_cfg.bps * 15 / 16);
                }
                break;
                case MPP_ENC_RC_MODE_VBR :
                {
                    /* CBR mode has wide bound */
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->h264_cfg.bps);
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->h264_cfg.bps * 17 / 16);
                    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->h264_cfg.bps * 1 / 16);
                }
                break;
                default :
                {
                    LOG_ERROR("unsupport encoder rc mode %d\n", rc_mode);
                }
                break;
                }
            }
        }
        break;
        case MPP_VIDEO_CodingVP8 :
        {
        } break;
        case MPP_VIDEO_CodingHEVC :
        {
            if (init || (p->h265_cfg.change & BIT(0)))
                mpp_enc_cfg_set_s32(cfg, "rc:gop", p->h265_cfg.gop);
            if (init || (p->h265_cfg.change & BIT(1)))
                mpp_enc_cfg_set_s32(cfg, "rc:mode", p->h265_cfg.rc_mode);
            if (init || (p->h265_cfg.change & BIT(2)))
            {
                /* setup default parameter */
                p->fps_in_den = 1;
                p->fps_in_num = p->h265_cfg.framerate;
                p->fps_out_den = 1;
                p->fps_out_num = p->h265_cfg.framerate;
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", p->fps_in_flex);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", p->fps_in_num);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", p->fps_in_den);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", p->fps_out_flex);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", p->fps_out_num);
                mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", p->fps_out_den);
            }
            if (init || (p->h265_cfg.change & BIT(3)))
            {
                ret = mpp_enc_cfg_set_s32(cfg, "prep:range", p->h265_cfg.range);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set prep:range failed ret %d\n", ret);
                    goto RET;
                }
            }
            if (init || (p->h265_cfg.change & BIT(4)))
            {
                ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &p->h265_cfg.head_each_idr);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set codec cfg failed ret %d\n", ret);
                    goto RET;
                }
            }
            if (init || (p->h265_cfg.change & BIT(5)))
            {
                ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->h265_cfg.sei);
                if (ret)
                {
                    LOG_ERROR("mpi control enc set sei cfg failed ret %d\n", ret);
                    goto RET;
                }
            }
            if (init || (p->h265_cfg.change & BIT(6)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_init", p->h265_cfg.qp.init);
            if (init || (p->h265_cfg.change & BIT(7)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_max", p->h265_cfg.qp.max);
            if (init || (p->h265_cfg.change & BIT(8)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_min", p->h265_cfg.qp.min);
            if (init || (p->h265_cfg.change & BIT(9)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_step", p->h265_cfg.qp.step);
            if (init || (p->h265_cfg.change & BIT(10)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_max_i", p->h265_cfg.qp.max_i_qp);
            if (init || (p->h265_cfg.change & BIT(11)))
                mpp_enc_cfg_set_s32(cfg, "h265:qp_min_i", p->h265_cfg.qp.min_i_qp);
            if (init || (p->h265_cfg.change & BIT(12)))
            {
                switch (rc_mode)
                {
                    case MPP_ENC_RC_MODE_FIXQP :
                    {
                        /* do not set bps on fix qp mode */
                    } break;
                    case MPP_ENC_RC_MODE_CBR :
                    {
                        /* CBR mode has narrow bound */
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->h265_cfg.bps);
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->h265_cfg.bps * 17 / 16);
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->h265_cfg.bps * 15 / 16);
                    }
                    break;
                    case MPP_ENC_RC_MODE_VBR :
                    {
                        /* CBR mode has wide bound */
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->h265_cfg.bps);
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->h265_cfg.bps * 17 / 16);
                        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->h265_cfg.bps * 1 / 16);
                    }
                    break;
                    default :
                    {
                        LOG_ERROR("unsupport encoder rc mode %d\n", rc_mode);
                    }
                    break;
                }
            }
        }
        break;
        default :
        {
            LOG_ERROR("unsupport encoder coding type %d\n", p->type);
        }
        break;
    }
    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret)
    {
        LOG_ERROR("mpi control enc set cfg failed ret %d\n", ret);
        goto RET;
    }
RET:
    return ret;
}

static int read_mpp_enc_cfg_modify_file(MpiEncTestData *p, bool init)
{
    int ret = -1;
    unsigned long read_size = 0;

    int modify_fd = fopen(RK_MPP_ENC_CFG_MODIFY_PATH, "rb");
    unsigned long size = get_file_size(RK_MPP_ENC_CFG_MODIFY_PATH);
    LOG_INFO("get cfg size=%ld\n", size);
    char *cfg = (char *)malloc(size);
    while (read_size != size)
    {
        read_size += fread(cfg, 1, size - read_size, modify_fd);
    }
    //LOG_INFO("get cfg =%s read_size=%ld\n", cfg, read_size);
    cJSON *root = cJSON_Parse(cfg);
    if (root == NULL)
    {
        LOG_ERROR("the %s is broken\n", RK_MPP_ENC_CFG_MODIFY_PATH);
    }
    else
    {
        ret = parse_check_mpp_enc_cfg(root, p, init);
    }
    if (modify_fd)
        fclose(modify_fd);
    if (cfg)
        free(cfg);
    if (root)
        cJSON_Delete(root);

    return ret;
}

static int check_mpp_enc_cfg_file_init(MpiEncTestData *p)
{
    int ret = -1;
    int cmd[128] = {0};
    if (NULL == p)
        return -1;

    if (!access(RK_MPP_ENC_CFG_MODIFY_PATH, F_OK))
    {
        ret = read_mpp_enc_cfg_modify_file(p, true);
    }

    if (ret)
    {
        if (!access(RK_MPP_ENC_CFG_ORIGINAL_PATH, F_OK))
        {
            sprintf(cmd, "cp %s %s", RK_MPP_ENC_CFG_ORIGINAL_PATH, RK_MPP_ENC_CFG_MODIFY_PATH);
            system(cmd);
            LOG_INFO("copy enc cfg file...\n");
            ret = read_mpp_enc_cfg_modify_file(p, true);
        }
        else
        {
            LOG_ERROR("file :%s not exit!\n", RK_MPP_ENC_CFG_ORIGINAL_PATH);
            ret = -1;
        }
    }
    else
    {

    }

    p->cfg_notify_fd = inotify_init();
    return ret;
}

void *thread_check_mpp_enc_chenge_loop(void *user)
{
    int ret = 0;
    MPP_RET mpp_ret;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, PTHREAD_CREATE_JOINABLE);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    MpiEncTestData *p = (MpiEncTestData *)user;
    int last_wd = 0;
    while (1)
    {
        last_wd = p->cfg_notify_wd;
        p->cfg_notify_wd = inotify_add_watch(p->cfg_notify_fd, RK_MPP_ENC_CFG_MODIFY_PATH, IN_MODIFY);//IN_ALL_EVENTS);

        if (p->cfg_notify_wd == IN_MODIFY || (last_wd == -1 && p->cfg_notify_wd == 1))
        {
            LOG_INFO("the enc cfg file change or creat, do update.wd=%d,last_wd=%d\n", p->cfg_notify_wd, last_wd);
            ret = read_mpp_enc_cfg_modify_file(p, false);
            if (ret)
                LOG_ERROR("error: the enc cfg file is broken.please check.\n");
            else
            {
                dump_mpp_enc_cfg(p);
                mpp_ret = mpp_enc_cfg_set(p, false);
                if (mpp_ret)
                {
                    LOG_ERROR("mpp_enc_cfg_set failed ret %d\n", mpp_ret);
                }
            }
            inotify_rm_watch(p->cfg_notify_fd, p->cfg_notify_wd);
            close(p->cfg_notify_fd);
            p->cfg_notify_fd = inotify_init();
        }
        sleep(1);
    }
}

