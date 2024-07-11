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

G_BEGIN_DECLS

struct ems_arguments
{
	GFile *stream_debug_file;
};

struct ems_arguments *
ems_arguments_get(void);

gboolean
ems_arguments_parse(int argc, char *argv[]);

G_END_DECLS