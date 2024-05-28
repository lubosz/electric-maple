// Copyright 2019-2024, Collabora, Ltd.
// Copyright 2023, Pluto VR, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A GStreamer pipeline for WebRTC streaming
 * @author Moshi Turner <moses@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_util
 */

#include <glib.h>

#include "util/u_misc.h"
#include "util/u_debug.h"


#include "ems_gstreamer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gstreamer_pipeline;
struct ems_callbacks;

typedef struct _em_proto_DownMessage em_proto_DownMessage;

GBytes *
ems_gstreamer_pipeline_encode_down_msg(em_proto_DownMessage *msg);

void
ems_gstreamer_pipeline_play(struct gstreamer_pipeline *gp);

void
ems_gstreamer_pipeline_stop(struct gstreamer_pipeline *gp);

void
ems_gstreamer_pipeline_stop_if_playing(struct gstreamer_pipeline *gp);

void
ems_gstreamer_pipeline_create(struct xrt_frame_context *xfctx,
                              const char *appsrc_name,
                              struct ems_callbacks *callbacks_collection,
                              struct gstreamer_pipeline **out_gp);

#ifdef __cplusplus
}
#endif
