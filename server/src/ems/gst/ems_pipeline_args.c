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
static gint bitrate = 16384;

gboolean ems_arguments_parse(int argc, char *argv[]) {

  GError *error = NULL;
  GOptionContext *context;

  static GOptionEntry entries[] =
    {
    { "stream-output-file-path", 'o', 0, G_OPTION_ARG_FILENAME, &output_file_name, "Path to store the stream in a MKV file.", "path" },
    { "bitrate", 'b', 0, G_OPTION_ARG_INT, &bitrate, "Stream bitrate", "N" },
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
  
  g_option_context_free(context);

  return TRUE;
}
