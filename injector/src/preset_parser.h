#pragma once

#include <string>
#include <vector>

struct AgsPresetEntry {
    std::string key;
    std::string value;
    std::string source_dir;
};

bool ags_preset_load_flat(const std::string &path,
                          std::vector<AgsPresetEntry> &entries,
                          std::string &error);

const AgsPresetEntry *ags_preset_find(const std::vector<AgsPresetEntry> &entries,
                                      const std::string &key);

std::string ags_preset_resolve_path(const AgsPresetEntry &entry);

bool ags_preset_write_flat(const std::string &source_path,
                           const std::string &destination_path,
                           std::string &error);
