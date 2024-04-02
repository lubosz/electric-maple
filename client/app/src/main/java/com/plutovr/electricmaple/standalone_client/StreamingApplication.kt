// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

package com.plutovr.electricmaple.standalone_client

import android.app.Application;
import android.system.Os
import android.util.Log
import org.freedesktop.gstreamer.GStreamer

class StreamingApplication : Application() {
    override fun onCreate() {
        super.onCreate()

        // TODO: Implement toggling this via `adb shell setprop` as it enables debug plotting
        val externalFilesDir = getExternalFilesDir(null)
        Log.d("ElectricMaple", "Will dump GStreamer plots to $externalFilesDir")
        Os.setenv("GST_DEBUG_DUMP_DOT_DIR", externalFilesDir.toString(), true)

        Log.i("ElectricMaple", "StreamingApplication: In onCreate")
        System.loadLibrary("gstreamer_android")
        Log.i("ElectricMaple", "StreamingApplication: loaded gstreamer_android")

        Log.i("ElectricMaple", "StreamingApplication: Calling GStreamer.init")
        GStreamer.init(this)
        Log.i("ElectricMaple", "StreamingApplication: Done with GStreamer.init")

    }
}
