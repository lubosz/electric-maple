// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief  Header for remote rendering pipeline arguments.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 */

#pragma once

#include <gio/gio.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef enum {
  EMS_ENCODER_TYPE_X264,
  EMS_ENCODER_TYPE_NVH264,
} EmsEncoderType;

struct ems_arguments
{
    GFile *stream_debug_file;
    uint32_t bitrate;
    EmsEncoderType encoder_type;
};

struct ems_arguments * ems_arguments_get(void);

gboolean ems_arguments_parse(int argc, char *argv[]);

G_END_DECLS