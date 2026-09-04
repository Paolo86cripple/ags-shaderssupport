#include "preset_parser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string trim(const std::string &s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

std::string parent_dir(const std::string &path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool absolute_path(const std::string &path) {
    return !path.empty() &&
           (path.front() == '/' ||
            (path.size() > 1 && path[1] == ':'));
}

std::string join_path(const std::string &dir, const std::string &path) {
    if (path.empty() || absolute_path(path)) return path;
    if (dir.empty() || dir == ".") return "./" + path;
    return dir + "/" + path;
}

std::string canonical_path(const std::string &path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved)) return resolved;
    return path;
}

void merge_entry(std::vector<AgsPresetEntry> &entries,
                 const AgsPresetEntry &incoming) {
    for (AgsPresetEntry &entry : entries) {
        if (entry.key == incoming.key) {
            entry = incoming;
            return;
        }
    }
    entries.push_back(incoming);
}

bool parse_reference(const std::string &line, std::string &reference) {
    static const std::string directive = "#reference";
    if (line.compare(0, directive.size(), directive) != 0) return false;
    if (line.size() > directive.size()) {
        const char c = line[directive.size()];
        if (c != ' ' && c != '\t' && c != '=') return false;
    }

    std::string rest = trim(line.substr(directive.size()));
    if (!rest.empty() && rest.front() == '=') rest = trim(rest.substr(1));
    if (rest.empty()) return true;

    if (rest.front() == '"') {
        const std::size_t end = rest.find('"', 1);
        if (end != std::string::npos) {
            reference = rest.substr(1, end - 1);
            return true;
        }
    }

    std::istringstream input(rest);
    input >> reference;
    reference = unquote(reference);
    return true;
}

bool load_recursive(const std::string &path,
                    std::vector<AgsPresetEntry> &entries,
                    std::vector<std::string> &stack,
                    std::string &error) {
    const std::string canonical = canonical_path(path);
    if (std::find(stack.begin(), stack.end(), canonical) != stack.end()) {
        error = "cyclic GLSLP #reference involving: " + path;
        return false;
    }

    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        error = "cannot read shader preset: " + path;
        return false;
    }

    stack.push_back(canonical);
    const std::string directory = parent_dir(path);
    std::vector<std::string> references;
    std::vector<AgsPresetEntry> local_entries;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::string reference;
        if (parse_reference(line, reference)) {
            if (reference.empty()) {
                error = "empty GLSLP #reference in: " + path;
                stack.pop_back();
                return false;
            }
            references.push_back(join_path(directory, reference));
            continue;
        }

        if (line.front() == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        AgsPresetEntry entry;
        entry.key = trim(line.substr(0, equals));
        entry.value = unquote(line.substr(equals + 1));
        entry.source_dir = directory;
        if (!entry.key.empty()) local_entries.push_back(entry);
    }

    // RetroArch preset inheritance treats referenced presets as bases/overlays;
    // values in the referencing file itself win over every referenced value.
    for (const std::string &reference : references) {
        if (!load_recursive(reference, entries, stack, error)) {
            stack.pop_back();
            return false;
        }
    }
    for (const AgsPresetEntry &entry : local_entries) merge_entry(entries, entry);

    stack.pop_back();
    return true;
}

std::vector<std::string> split_semicolon(const std::string &value) {
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string item;
    while (std::getline(input, item, ';')) {
        item = trim(item);
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

bool shader_key(const std::string &key) {
    if (key.compare(0, 6, "shader") != 0 || key.size() <= 6) return false;
    for (std::size_t i = 6; i < key.size(); ++i)
        if (key[i] < '0' || key[i] > '9') return false;
    return true;
}

bool frame_count_mod_key(const std::string &key) {
    static const std::string prefix = "frame_count_mod";
    if (key.compare(0, prefix.size(), prefix) != 0 || key.size() <= prefix.size())
        return false;
    for (std::size_t i = prefix.size(); i < key.size(); ++i)
        if (key[i] < '0' || key[i] > '9') return false;
    return true;
}

std::string normalize_uint_prefix(const std::string &value) {
    const std::string text = trim(value);
    if (text.empty() || text.front() == '-') return "0";

    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
    if (end == text.c_str() || errno == ERANGE) return "0";

    const unsigned long limited = std::min<unsigned long>(parsed, UINT_MAX);
    return std::to_string(static_cast<unsigned>(limited));
}
}

bool ags_preset_load_flat(const std::string &path,
                          std::vector<AgsPresetEntry> &entries,
                          std::string &error) {
    entries.clear();
    std::vector<std::string> stack;
    return load_recursive(path, entries, stack, error);
}

const AgsPresetEntry *ags_preset_find(const std::vector<AgsPresetEntry> &entries,
                                      const std::string &key) {
    for (const AgsPresetEntry &entry : entries)
        if (entry.key == key) return &entry;
    return nullptr;
}

std::string ags_preset_resolve_path(const AgsPresetEntry &entry) {
    if (entry.value.empty() || absolute_path(entry.value)) return entry.value;
    return canonical_path(join_path(entry.source_dir, entry.value));
}

bool ags_preset_write_flat(const std::string &source_path,
                           const std::string &destination_path,
                           std::string &error) {
    std::vector<AgsPresetEntry> entries;
    if (!ags_preset_load_flat(source_path, entries, error)) return false;

    std::vector<std::string> textures;
    if (const AgsPresetEntry *texture_list = ags_preset_find(entries, "textures"))
        textures = split_semicolon(texture_list->value);

    std::ofstream output(destination_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create flattened shader preset: " + destination_path;
        return false;
    }

    output << "# Flattened by AGS shader injector; #reference inheritance resolved.\n";
    for (const AgsPresetEntry &entry : entries) {
        std::string value = entry.value;
        const bool texture_path =
            std::find(textures.begin(), textures.end(), entry.key) != textures.end();
        if ((shader_key(entry.key) || texture_path) && !value.empty())
            value = ags_preset_resolve_path(entry);
        else if (frame_count_mod_key(entry.key))
            value = normalize_uint_prefix(value);
        output << entry.key << " = " << value << '\n';
    }

    if (!output) {
        error = "cannot write flattened shader preset: " + destination_path;
        return false;
    }
    return true;
}
