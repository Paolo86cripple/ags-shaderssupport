/*
 * Test-only native-source stub.
 *
 * The real AGS native-source detector interposes dynamic OpenGL loader entry
 * points and therefore belongs exclusively in libags-shader.so. Pipeline/load
 * tests exercise shader behavior in a synthetic SDL/OpenGL host and must not
 * replace dlsym(), GLX or SDL loader functions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ags_native_source_hook.h"

bool ags_native_source_acquire(int,
                               int,
                               AgsNativeSource &source) {
    source = AgsNativeSource();
    return false;
}

void ags_native_source_set_pipeline_active(bool) {
}
