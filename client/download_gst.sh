#!/bin/bash

# Copyright 2023, Pluto VR, Inc.
# Copyright 2024, Collabora, Ltd.
#
# SPDX-License-Identifier: BSL-1.0

# Go to where the script is
cd $(dirname $0)

export GSTREAMER_VERSION=1.24.4

# Delete old gstreamer directory if it exists
rm -fR ./deps/gstreamer_android

# Create the destination directory if it doesn't exist
mkdir -p ./deps/gstreamer_android

# Download the file using curl
curl -L -o ./deps/gstreamer_android/gstreamer-1.0-android-universal-${GSTREAMER_VERSION}.tar.xz https://gstreamer.freedesktop.org/data/pkg/android/${GSTREAMER_VERSION}/gstreamer-1.0-android-universal-${GSTREAMER_VERSION}.tar.xz

# Extract the contents of the downloaded file to the destination directory
tar -xf ./deps/gstreamer_android/gstreamer-1.0-android-universal-${GSTREAMER_VERSION}.tar.xz -C ./deps/gstreamer_android

rm ./deps/gstreamer_android/gstreamer-1.0-android-universal-${GSTREAMER_VERSION}.tar.xz
