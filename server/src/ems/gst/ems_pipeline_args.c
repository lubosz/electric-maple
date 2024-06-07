// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief  Implementation for remote rendering pipeline arguments.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 */

#include "ems_pipeline_args.h"

static struct ems_arguments arguments_instance;

struct ems_arguments * ems_arguments_get() {
    return &arguments_instance;
}

gchar *output_file_name = NULL;
gchar *encoder_name = NULL;
gboolean benchmark_down_msg = FALSE;

// defaults
static gint bitrate = 16384;
static EmsEncoderType default_encoder_type = EMS_ENCODER_TYPE_X264;

gboolean ems_arguments_parse(int argc, char *argv[]) {

  GError *error = NULL;
  GOptionContext *context;

  static GOptionEntry entries[] =
    {
    { "stream-output-file-path", 'o', 0, G_OPTION_ARG_FILENAME, &output_file_name, "Path to store the stream in a MKV file.", "path" },
    { "bitrate", 'b', 0, G_OPTION_ARG_INT, &bitrate, "Stream bitrate", "N" },
    { "encoder", 'e', 0, G_OPTION_ARG_STRING, &encoder_name, "Encoder (x264, nvh264)", "str" },
    { "benchmark-down-msg", 0, 0, G_OPTION_ARG_NONE, &benchmark_down_msg, "Benchmark DownMessage Loss", NULL },
    G_OPTION_ENTRY_NULL
    };

  context = g_option_context_new("- Elecric Maple streaming server");
  g_option_context_add_main_entries(context, entries, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error))
  {
    g_print ("option parsing failed: %s\n", error->message);
    return FALSE;
  }

  if (output_file_name) {
    arguments_instance.stream_debug_file = g_file_new_for_path(output_file_name);
  }

  arguments_instance.bitrate = bitrate;
  arguments_instance.benchmark_down_msg = benchmark_down_msg;

  if (encoder_name) {
    if (g_strcmp0(encoder_name, "nvh264") == 0) {
      arguments_instance.encoder_type = EMS_ENCODER_TYPE_NVH264;
    } else if (g_strcmp0(encoder_name, "x264") == 0) {
      arguments_instance.encoder_type = EMS_ENCODER_TYPE_X264;
    } else {
      arguments_instance.encoder_type = default_encoder_type;
    }
  } else {
    arguments_instance.encoder_type = default_encoder_type;
  }

  g_option_context_free(context);

  return TRUE;
}
