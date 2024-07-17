#!/usr/bin/env bash

# Copyright 2024, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Based on Monado install-ndk.sh and install-android-sdk.sh
# https://gitlab.freedesktop.org/monado/monado/-/blob/main/.gitlab-ci/install-ndk.sh
# https://gitlab.freedesktop.org/monado/monado/-/blob/main/.gitlab-ci/install-android-sdk.sh

pushd /tmp

NDK_VERSION=r26d
NDK_FILE=android-ndk-${NDK_VERSION}-linux.zip
curl -LO https://dl.google.com/android/repository/$NDK_FILE
unzip $NDK_FILE -d /opt/

TOOLS_VERSION=11076708
TOOLS_FILE=commandlinetools-linux-${TOOLS_VERSION}_latest.zip
curl -LO https://dl.google.com/android/repository/$TOOLS_FILE
unzip $TOOLS_FILE

ANDROID_VERSION=34
ANDROID_TOOLS_VERSION=34.0.0
SDK_ROOT=/opt/android-sdk
SDK_MANAGER="/tmp/cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT"

echo y | $SDK_MANAGER "platforms;android-${ANDROID_VERSION}" >> /dev/null
echo y | $SDK_MANAGER "platform-tools" >> /dev/null
echo y | $SDK_MANAGER "build-tools;${ANDROID_TOOLS_VERSION}" >> /dev/null
yes | $SDK_MANAGER --licenses >> /dev/null
