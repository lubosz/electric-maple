// Copyright 2023, Pluto VR, Inc.
// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0

package org.freedesktop.electricmaple.standalone_client

import android.app.Application
import android.system.Os
import android.util.Log
import org.freedesktop.gstreamer.GStreamer
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader


class StreamingApplication : Application() {

    private val ENABLE_PLOTTING_PROPERY_NAME: String = "debug.electric_maple.enable_plotting"

    private fun isPlottingEnabled(): Boolean {
        var enablePlotting = ""

        val command = arrayOf("/system/bin/getprop", ENABLE_PLOTTING_PROPERY_NAME)

        try {
            val process = Runtime.getRuntime().exec(command)
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            enablePlotting = reader.readLine()
            reader.close()
        } catch (e: IOException) {
            Log.e("ElectricMaple", "Unable to read system property $ENABLE_PLOTTING_PROPERY_NAME.")
        }

        return enablePlotting.lowercase() in listOf("1", "true").map { it.lowercase() }
    }

    override fun onCreate() {
        super.onCreate()

        if (isPlottingEnabled()) {
            Log.i("ElectricMaple", "Enabling GStreamer pipeline plotting")
            val externalFilesDir = getExternalFilesDir(null)
            Log.i("ElectricMaple", "Will dump GStreamer plots to $externalFilesDir")
            Os.setenv("GST_DEBUG_DUMP_DOT_DIR", externalFilesDir.toString(), true)
        }

        Log.i("ElectricMaple", "StreamingApplication: In onCreate")
        System.loadLibrary("gstreamer_android")
        Log.i("ElectricMaple", "StreamingApplication: loaded gstreamer_android")

        Log.i("ElectricMaple", "StreamingApplication: Calling GStreamer.init")
        GStreamer.init(this)
        Log.i("ElectricMaple", "StreamingApplication: Done with GStreamer.init")

    }
}
