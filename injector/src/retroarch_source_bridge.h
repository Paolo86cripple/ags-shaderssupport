#pragma once

/*
 * AGS Shader Injector - RetroArch source backing bridge.
 * Copyright (C) 2026 Paolo86cripple and contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

bool ags_ra_source_bridge_prepare(unsigned input_texture,
                                  int logical_width,
                                  int logical_height,
                                  unsigned &output_texture,
                                  int &backing_width,
                                  int &backing_height);

void ags_ra_source_bridge_release();
