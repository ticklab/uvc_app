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

#include "mpi_enc.h"
#include "uvc_video.h"

#if 0
static OptionInfo mpi_enc_cmd[] = {
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
    if (NULL == ptr) {
        *value = default_value;
    } else {
        char *endptr;
        int base = (ptr[0] == '0' && ptr[1] == 'x') ? (16) : (10);
        *value = strtoul(ptr, &endptr, base);
        if (ptr == endptr) {
            *value = default_value;
        }
    }
    return 0;
}

static MPP_RET test_ctx_init(MpiEncTestData **data, MpiEncTestCmd *cmd)
{
    MpiEncTestData *p = NULL;
    MPP_RET ret = MPP_OK;

    if (!data || !cmd) {
        printf("invalid input data %p cmd %p\n", data, cmd);
        return MPP_ERR_NULL_PTR;
    }

    p = calloc(sizeof(MpiEncTestData), 1);
    if (!p) {
        printf("create MpiEncTestData failed\n");
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

    if (cmd->have_output || !access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0)) {
        p->fp_output = fopen(RK_MPP_DEBUG_OUT_FILE, "w+b");
        if (NULL == p->fp_output) {
            printf("failed to open output file %s\n", RK_MPP_DEBUG_OUT_FILE);
            ret = MPP_ERR_OPEN_FILE;
        }
        printf("debug out file open\n");
    }

    if (!access(RK_MPP_DYNAMIC_DEBUG_IN_CHECK, 0)) {
        p->fp_input = fopen(RK_MPP_DEBUG_IN_FILE, "w+b");
        if (NULL == p->fp_input) {
            printf("failed to open in file %s\n", RK_MPP_DEBUG_IN_FILE);
            ret = MPP_ERR_OPEN_FILE;
        }
        printf("warnning:debug in file open, open it will lower the fps\n");
    }

    // update resource parameter
    if (p->fmt <= MPP_FMT_YUV420SP_VU)
        p->frame_size = MPP_ALIGN(cmd->width, 16) * MPP_ALIGN(cmd->height, 16) * 2;
    else if (p->fmt <= MPP_FMT_YUV422_UYVY) {
        // NOTE: yuyv and uyvy need to double stride
        p->hor_stride *= 2;
        p->frame_size = p->hor_stride * p->ver_stride;
    } else
        p->frame_size = p->hor_stride * p->ver_stride * 4;
    p->packet_size  = p->frame_size;//p->width * p->height;

RET:
    *data = p;
    return ret;
}

static MPP_RET test_ctx_deinit(MpiEncTestData **data)
{
    MpiEncTestData *p = NULL;

    if (!data) {
        printf("invalid input data %p\n", data);
        return MPP_ERR_NULL_PTR;
    }

    p = *data;
    if (p) {
        if (p->fp_input) {
            fclose(p->fp_input);
            p->fp_input = NULL;
        }
        if (p->fp_output) {
            fclose(p->fp_output);
            p->fp_output = NULL;
        }
        free(p);
        *data = NULL;
    }

    return MPP_OK;
}

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
    printf("enc_version:%d,RK_MPP_USE_FULL_RANGE:%d\n",
           p->enc_version, RK_MPP_USE_FULL_RANGE);

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
    switch (rc_mode) {
    case MPP_ENC_RC_MODE_FIXQP : {
        /* do not set bps on fix qp mode */
    } break;
    case MPP_ENC_RC_MODE_CBR : {
        /* CBR mode has narrow bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->bps);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps * 15 / 16);
    } break;
    case MPP_ENC_RC_MODE_VBR : {
        /* CBR mode has wide bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->bps);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps * 1 / 16);
    } break;
    default : {
        printf("unsupport encoder rc mode %d\n", rc_mode);
    } break;
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
    switch (p->type) {
    case MPP_VIDEO_CodingAVC : {
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
    } break;
    case MPP_VIDEO_CodingMJPEG : {
        mpp_enc_cfg_set_s32(cfg, "jpeg:quant", 7);
    } break;
    case MPP_VIDEO_CodingVP8 : {
    } break;
    case MPP_VIDEO_CodingHEVC : {
        mpp_enc_cfg_set_s32(cfg, "h265:qp_init", rc_mode == MPP_ENC_RC_MODE_FIXQP ? -1 : 26);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_max", 51);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_min", 10);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_max_i", 46);
        mpp_enc_cfg_set_s32(cfg, "h265:qp_min_i", 24);
    } break;
    default : {
        printf("unsupport encoder coding type %d\n", p->type);
    } break;
    }

    p->split_mode= MPP_ENC_SPLIT_NONE;
    p->split_arg = 0;

    if (p->split_mode) {
        printf("split_mode %d split_arg %d\n", p->split_mode, p->split_arg);
        mpp_enc_cfg_set_s32(cfg, "split:mode", p->split_mode);
        mpp_enc_cfg_set_s32(cfg, "split:arg", p->split_arg);
    }

#if RK_MPP_USE_FULL_RANGE
    ret = mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_JPEG);
#else
    ret = mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_UNSPECIFIED);
#endif
    if (ret) {
        printf("mpi control enc set prep:range failed ret %d\n", ret);
        goto RET;
    }

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret) {
        printf("mpi control enc set cfg failed ret %d\n", ret);
        goto RET;
    }

    /* optional */
     p->sei_mode = MPP_ENC_SEI_MODE_DISABLE;
     ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->sei_mode);
     if (ret) {
         printf("mpi control enc set sei cfg failed ret %d\n", ret);
         goto RET;
     }

    if (p->enc_version == 1 &&
       (p->type == MPP_VIDEO_CodingAVC ||
        p->type == MPP_VIDEO_CodingHEVC)) {
        int header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);
        if (ret) {
            printf("mpi control enc set codec cfg failed ret %d\n", ret);
            goto RET;
        }
    }

RET:
    return ret;
}

static MPP_RET test_mpp_run(MpiEncTestData *p, int fd, size_t size)
{
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppBuffer buf = NULL;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi = p->mpi;
    ctx = p->ctx;

    if (p->enc_version == 1) {
        //no need to get the sps/pps in addition
        if (p->type == MPP_VIDEO_CodingAVC) {//force set idr when begin enc
            if (p->h264_frm_count < (RK_MPP_H264_FORCE_IDR_COUNT * RK_MPP_H264_FORCE_IDR_PERIOD)) {
                if ((p->h264_frm_count % RK_MPP_H264_FORCE_IDR_PERIOD) == 0) {
                   ret = mpi->control(ctx, MPP_ENC_SET_IDR_FRAME, NULL);
                   if (ret) {
                       printf("mpi force idr frame control failed\n");
                       goto RET;
                   } else {
                       printf("mpi force idr frame control ok, h264_frm_count:%d\n",p->h264_frm_count);
                   }
                }
                p->h264_frm_count++;
            }
        }
    } else {
        if (p->type == MPP_VIDEO_CodingAVC) {
            MppPacket packet = NULL;

            ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
            if (ret) {
                printf("mpi control enc get extra info failed\n");
                goto RET;
            }

            /* get and write sps/pps for H.264 */
            if (packet) {
                void *ptr   = mpp_packet_get_pos(packet);
                size_t len  = mpp_packet_get_length(packet);

                if (p->fp_output)
                    fwrite(ptr, 1, len, p->fp_output);

                packet = NULL;
            }
        }
    }

    do {
        MppFrame frame = NULL;

        if (p->packet)
            mpp_packet_deinit(&p->packet);
        p->packet = NULL;

        ret = mpp_frame_init(&frame);
        if (ret) {
            printf("mpp_frame_init failed\n");
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
        if (ret) {
            printf("import input picture buffer failed\n");
            goto RET;
        }
        mpp_frame_set_buffer(frame, buf);
#endif
        mpp_frame_set_eos(frame, p->frm_eos);

        ret = mpi->encode_put_frame(ctx, frame);
        if (ret) {
            printf("mpp encode put frame failed\n");
            mpp_frame_deinit(&frame);
            goto RET;
        }
        mpp_frame_deinit(&frame);

        ret = mpi->encode_get_packet(ctx, &p->packet);
        if (ret) {
            printf("mpp encode get packet failed\n");
            goto RET;
        }

        if (p->packet) {
            // write packet to file here
            void *ptr   = mpp_packet_get_pos(p->packet);
            size_t len  = mpp_packet_get_length(p->packet);

            p->pkt_eos = mpp_packet_get_eos(p->packet);

            if (p->fp_output) {
                fwrite(ptr, 1, len, p->fp_output);
#if RK_MPP_DYNAMIC_DEBUG_ON
                if (access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0)) {
                    fclose(p->fp_output);
                    p->fp_output = NULL;
                    printf("debug out file close\n");
                }
            } else if (!access(RK_MPP_DYNAMIC_DEBUG_OUT_CHECK, 0)) {
                p->fp_output = fopen(RK_MPP_DEBUG_OUT_FILE, "w+b");
                if (p->fp_output) {
                    fwrite(ptr, 1, len, p->fp_output);
                    printf("debug out file open\n");
                }
#endif
            }

            mpp_packet_deinit(&p->packet);
            p->enc_data = ptr;
            p->enc_len = len;
        }
    } while (0);
RET:

    if (buf) {
        mpp_buffer_put(buf);
        buf = NULL;
    }
    return ret;
}

MPP_RET mpi_enc_test_init(MpiEncTestCmd *cmd, MpiEncTestData **data)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = NULL;

    printf("mpi_enc_test start\n");

    ret = test_ctx_init(&p, cmd);
    if (ret) {
        printf("test data init failed ret %d\n", ret);
        return ret;
    }
    *data = p;

#if 0
    ret = mpp_buffer_get(NULL, &p->frm_buf, p->frame_size);
    if (ret) {
        printf("failed to get buffer for input frame ret %d\n", ret);
        return ret;
    }
#endif

    printf("mpi_enc_test encoder test start w %d h %d type %d\n",
            p->width, p->height, p->type);

    // encoder demo
    ret = mpp_create(&p->ctx, &p->mpi);
    if (ret) {
        printf("mpp_create failed ret %d\n", ret);
        return ret;
    }

    ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
    if (ret) {
        printf("mpp_init failed ret %d\n", ret);
        return ret;
    }
    ret = mpp_enc_cfg_init(&p->cfg);
    if (ret) {
        printf("mpp_enc_cfg_init failed ret %d\n", ret);
        return ret;
    }

    ret = test_mpp_setup(p);
    if (ret) {
        printf("test mpp setup failed ret %d\n", ret);
        return ret;
    }
}

MPP_RET mpi_enc_test_run(MpiEncTestData **data, int fd, size_t size)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = *data;

    ret = test_mpp_run(p, fd, size);
    if (ret)
        printf("test mpp run failed ret %d\n", ret);
    return ret;
}

MPP_RET mpi_enc_test_deinit(MpiEncTestData **data)
{
    MPP_RET ret = MPP_OK;
    MpiEncTestData *p = *data;
    if (p->packet)
        mpp_packet_deinit(&p->packet);
    ret = p->mpi->reset(p->ctx);
    if (ret) {
        printf("mpi->reset failed\n");
    }
    if (p->ctx) {
        mpp_destroy(p->ctx);
        p->ctx = NULL;
    }

#if 0
    if (p->frm_buf) {
        mpp_buffer_put(p->frm_buf);
        p->frm_buf = NULL;
    }
#endif

    if (MPP_OK == ret){
        printf("mpi_enc_test success \n");
        //printf("mpi_enc_test success total frame %d bps %lld\n",
        //        p->frame_count, (RK_U64)((p->stream_size * 8 * p->fps) / p->frame_count));
    } else {
        printf("mpi_enc_test failed ret %d\n", ret);
    }
    test_ctx_deinit(&p);

    return ret;
}

void mpi_enc_cmd_config(MpiEncTestCmd *cmd, int width, int height,int fcc)
{
    memset((void*)cmd, 0, sizeof(*cmd));
    cmd->width = width;
    cmd->height = height;
    cmd->format = g_format;
    switch (fcc) {
    case V4L2_PIX_FMT_YUYV:
        printf("%s: yuyv not need mpp encodec: %d\n", __func__, fcc);
        break;
    case V4L2_PIX_FMT_MJPEG:
        cmd->type = MPP_VIDEO_CodingMJPEG;
        break;
    case V4L2_PIX_FMT_H264:
        cmd->type = MPP_VIDEO_CodingAVC;
        break;
    default:
        printf("%s: not support fcc: %d\n", __func__, fcc);
        break;
    }

}

void mpi_enc_cmd_config_mjpg(MpiEncTestCmd *cmd, int width, int height)
{
    memset((void*)cmd, 0, sizeof(*cmd));
    cmd->width = width;
    cmd->height = height;
    cmd->format = g_format;
    cmd->type = MPP_VIDEO_CodingMJPEG;
}

void mpi_enc_cmd_config_h264(MpiEncTestCmd *cmd, int width, int height)
{
    memset((void*)cmd, 0, sizeof(*cmd));
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
    if (NULL == p) {
        *size = 0;
        return -1;
    }
    mpi = p->mpi;
    ctx = p->ctx;
    MppPacket packet = NULL;
    ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
    if (ret) {
        printf("mpi control enc get extra info failed\n");
        *size = 0;
        return -1;
    }
    if (packet) {
        void *ptr   = mpp_packet_get_pos(packet);
        size_t len  = mpp_packet_get_length(packet);
        printf("%s: len = %d\n", __func__, len);
        if (*size >= len) {
            memcpy(buffer, ptr, len);
            *size = len;
        } else {
            printf("%s: input buffer size = %d\n", __func__, *size);
            *size = 0;
        }
        packet = NULL;
    }
    return 0;
}
