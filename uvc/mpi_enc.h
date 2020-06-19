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
#ifndef __MPI_ENC_H__
#define __MPI_ENC_H__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#include "vld.h"
#endif

#define MODULE_TAG "mpi_enc"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rockchip/rk_mpi.h>

//#include "mpp_env.h"
//#include "mpp_mem.h"
//#include "printf.h"
//#include "mpp_time.h"
#include "mpp_common.h"

//#include "utils.h"

#define MAX_FILE_NAME_LENGTH        256
#define RK_MPP_VERSION_DEFAULT 1
#define RK_MPP_USE_FULL_RANGE 0
#define RK_MPP_H264_FORCE_IDR_COUNT 20
#define RK_MPP_H264_FORCE_IDR_PERIOD 5 //must >=1
#define RK_MPP_ENC_TEST_NATIVE 0
#define RK_MPP_DYNAMIC_DEBUG_ON 1 //release version can set to 0
#define RK_MPP_RANGE_DEBUG_ON 1 //release version can set to 0

#define RK_MPP_DYNAMIC_DEBUG_OUT_CHECK "/tmp/uvc_enc_out"
#define RK_MPP_DYNAMIC_DEBUG_IN_CHECK "/tmp/uvc_enc_in" //open it will lower the fps
#define RK_MPP_RANGE_DEBUG_IN_CHECK "/tmp/uvc_range_in"

#define RK_MPP_DEBUG_OUT_FILE "/data/uvc_enc_out.bin"
#define RK_MPP_DEBUG_IN_FILE "/data/uvc_enc_in.bin"

//#define DEBUG_OUTPUT 1
#if RK_MPP_ENC_TEST_NATIVE
extern struct uvc_encode uvc_enc;
extern int uvc_encode_init(struct uvc_encode *e, int width, int height,int fcc);
#define TEST_ENC_TPYE V4L2_PIX_FMT_H264 //V4L2_PIX_FMT_MJPEG
#endif

typedef struct {
    MppCodingType   type;
    RK_U32          width;
    RK_U32          height;
    MppFrameFormat  format;
    RK_U32          debug;
    RK_U32          num_frames;

    RK_U32          have_output;
} MpiEncTestCmd;

typedef struct {
    // global flow control flag
    RK_U32 frm_eos;
    RK_U32 pkt_eos;
    RK_S32 frame_count;
    RK_U64 stream_size;
#if RK_MPP_RANGE_DEBUG_ON
#define RANGE_PATH_MAX_LEN 128
    FILE *fp_range_path;
    FILE *fp_range_file;
    char *range_path;

#endif
    // src and dst
    FILE *fp_input;
    FILE *fp_output;

    // base flow context
    MppCtx ctx;
    MppApi *mpi;
    RK_U32 use_legacy_cfg;
    MppEncCfg cfg;
    MppEncPrepCfg prep_cfg;
    MppEncRcCfg rc_cfg;
    MppEncCodecCfg codec_cfg;
    MppEncSliceSplit split_cfg;
    MppEncOSDPltCfg osd_plt_cfg;
    MppEncOSDPlt 	 osd_plt;
    MppEncOSDData	 osd_data;
    MppEncROIRegion roi_region[3];
    MppEncROICfg 	 roi_cfg;

    // input / output
    MppBuffer frm_buf;
    MppEncSeiMode sei_mode;
    MppEncHeaderMode header_mode;
    MppBuffer osd_idx_buf;

    // paramter for resource malloc
    RK_U32 width;
    RK_U32 height;
    RK_U32 hor_stride;
    RK_U32 ver_stride;
    MppFrameFormat fmt;
    MppCodingType type;
    RK_S32 num_frames;
    RK_S32 loop_times;

    // resources
    size_t header_size;
    size_t frame_size;
    /* NOTE: packet buffer may overflow */
    size_t packet_size;
    /*
    * osd idx size range from 16x16 bytes(pixels) to hor_stride*ver_stride(bytes).
    * for general use, 1/8 Y buffer is enough.
    */
    size_t osd_idx_size;
    RK_U32 plt_table[8];

    RK_U32 osd_enable;
    RK_U32 osd_mode;
    RK_U32 split_mode;
    RK_U32 split_arg;

    RK_U32 user_data_enable;
    RK_U32 roi_enable;

    // rate control runtime parameter
    RK_S32 gop;
    RK_S32 fps_in_flex;
    RK_S32 fps_in_den;
    RK_S32 fps_in_num;
    RK_S32 fps_out_flex;
    RK_S32 fps_out_den;
    RK_S32 fps_out_num;
    RK_S32 bps;
    RK_U32 gop_mode;

    MppPacket packet;
    void *enc_data;
    size_t enc_len;
    RK_U32 enc_version;
    RK_U32 h264_frm_count;
} MpiEncTestData;

MPP_RET mpi_enc_test_init(MpiEncTestCmd *cmd, MpiEncTestData **data);
MPP_RET mpi_enc_test_run(MpiEncTestData **data, int fd, size_t size);
MPP_RET mpi_enc_test_deinit(MpiEncTestData **data);
void mpi_enc_cmd_config(MpiEncTestCmd *cmd, int width, int height,int fcc);
void mpi_enc_cmd_config_mjpg(MpiEncTestCmd *cmd, int width, int height);
void mpi_enc_cmd_config_h264(MpiEncTestCmd *cmd, int width, int height);
void mpi_enc_set_format(MppFrameFormat format);
int mpi_enc_get_h264_extra(MpiEncTestData *p, void *buffer, size_t *size);
RK_S32 mpi_get_env_u32(const char *name, RK_U32 *value, RK_U32 default_value);
MPP_RET mpp_enc_cfg_set_s32(MppEncCfg cfg, const char *name, RK_S32 val);
MPP_RET mpp_enc_cfg_set_u32(MppEncCfg cfg, const char *name, RK_U32 val);
MPP_RET mpp_enc_cfg_set_s64(MppEncCfg cfg, const char *name, RK_S64 val);
MPP_RET mpp_enc_cfg_set_u64(MppEncCfg cfg, const char *name, RK_U64 val);
MPP_RET mpp_enc_cfg_set_ptr(MppEncCfg cfg, const char *name, void *val);
MPP_RET mpp_enc_cfg_get_s32(MppEncCfg cfg, const char *name, RK_S32 *val);
MPP_RET mpp_enc_cfg_get_u32(MppEncCfg cfg, const char *name, RK_U32 *val);
MPP_RET mpp_enc_cfg_get_s64(MppEncCfg cfg, const char *name, RK_S64 *val);
MPP_RET mpp_enc_cfg_get_u64(MppEncCfg cfg, const char *name, RK_U64 *val);

#ifdef __cplusplus
}
#endif

#endif
