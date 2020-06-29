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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "uevent.h"
#include "uvc_control.h"
#include "uvc_log.h"

static bool find_video;

static void video_uevent(const struct _uevent *event)
{
    const char dev_name[] = "DEVNAME=";
    char *tmp = NULL;
    char *act = event->strs[0] + 7;
    int i, id;

    for (i = 3; i < event->size; i++) {
        tmp = event->strs[i];
        /* search "DEVNAME=" */
        if (!strncmp(dev_name, tmp, strlen(dev_name)))
            break;
    }

    if (i < event->size) {
        tmp = strchr(tmp, '=') + 1;

        if (sscanf((char *)&tmp[strlen("video")], "%d", &id) < 1) {
            LOG_ERROR("failed to parse video id\n");
            return;
        }

        if (!strcmp(act, "add")) {
            LOG_INFO("add video...\n");
            uvc_control_signal();
            find_video = true;
            //video_record_addvideo(id, 1920, 1080, 30);
        } else {
            LOG_INFO("delete video...\n");
            find_video = false;
            //video_record_deletevideo(id);
        }
    }
}

int read_sysfs_string(const char *filename, char *str)
{
	int ret = 0;
	FILE  *sysfsfp;

	sysfsfp = fopen(filename, "r");
	if (!sysfsfp) {
		ret = -errno;
		goto error_free;
	}

	errno = 0;
	if (fscanf(sysfsfp, "%s\n", str) != 1) {
		ret = errno ? -errno : -ENODATA;
		if (fclose(sysfsfp))
			perror("read_sysfs_string(): Failed to close dir");

		goto error_free;
	}
    LOG_INFO("read_sysfs_string:file:%s,str:%s\n",filename,str);
	if (fclose(sysfsfp))
		ret = -errno;

error_free:
	return ret;
}

/*
 * e.g uevent info
 * ACTION=change
 * DEVPATH=/devices/11050000.i2c/i2c-0/0-0012/cvr_uevent/gsensor
 * SUBSYSTEM=cvr_uevent
 * CVR_DEV_NAME=gsensor
 * CVR_DEV_TYPE=2
 */
static const char udc_path[] = {"/sys/devices/virtual/android_usb/android0/state"};

static void parse_event(const struct _uevent *event)
{
    char *sysfs = NULL;
    char tmp[64];
    int ret;

    if (event->size <= 0)
        return;

    sysfs = event->strs[2] + 10;
    if (!strcmp(sysfs, "video4linux")) {
        video_uevent(event);
    } else if (!strcmp(sysfs, "udc")) {
        ret = read_sysfs_string(udc_path, tmp);
        if (ret < 0) {
            LOG_ERROR("[ERR] failed to get sysfs\n");
        }
        if (memcmp(tmp, "CONFIGURED", sizeof("CONFIGURED")))
            uvc_control_signal();
    }
}

static void *event_monitor_thread(void *arg)
{
    int sockfd;
    int i, j, len;
    char buf[512];
    struct iovec iov;
    struct msghdr msg;
    struct sockaddr_nl sa;
    struct _uevent event;
    uint32_t flags = *(uint32_t *)arg;

    prctl(PR_SET_NAME, "event_monitor", 0, 0, 0);

    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = NETLINK_KOBJECT_UEVENT;
    sa.nl_pid = 0;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = (void *)buf;
    iov.iov_len = sizeof(buf);
    msg.msg_name = (void *)&sa;
    msg.msg_namelen = sizeof(sa);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (sockfd == -1) {
        LOG_ERROR("socket creating failed:%s\n", strerror(errno));
        goto err_event_monitor;
    }

    if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        LOG_ERROR("bind error:%s\n", strerror(errno));
        goto err_event_monitor;
    }

    find_video = false;
    while (1) {
        event.size = 0;
        len = recvmsg(sockfd, &msg, 0);
        if (len < 0) {
            LOG_ERROR("receive error\n");
        } else if (len < 32 || len > sizeof(buf)) {
            LOG_INFO("invalid message");
        } else {
            for (i = 0, j = 0; i < len; i++) {
                if (*(buf + i) == '\0' && (i + 1) != len) {
                    event.strs[j++] = buf + i + 1;
                    event.size = j;
                }
            }
        }
        parse_event(&event);
        if ((flags & UVC_CONTROL_LOOP_ONCE) && find_video)
            break;
    }

err_event_monitor:
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

int uevent_monitor_run(uint32_t flags)
{
    pthread_t tid;

    return pthread_create(&tid, NULL, event_monitor_thread, &flags);
}

