#pragma once

#include <string>

bool ags_lut_load_preset(const std::string &preset_path, std::string &error);
void ags_lut_clear();
int ags_lut_bind(unsigned program, int first_unit, int max_units);
