#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

struct RGB {
    unsigned char r, g, b;

    float red() const {
        return r / 255.f;
    }

    float green() const {
        return g / 255.f;
    }

    float blue() const {
        return b / 255.f;
    }

    std::string hex() const {
        char value[8];
        std::snprintf(value, sizeof value, "#%02X%02X%02X", r, g, b);
        return value;
    }
};

struct Colors {
    enum Slot { Background,
                Grid,
                Travel,
                Line,
                Nozzle,
                AxisX,
                AxisY,
                AxisZ,
                Count };

    static constexpr std::array<const char *, Count> names = {"background", "grid", "travel", "line", "nozzle", "axis_x", "axis_y", "axis_z"};
    std::array<RGB, Count> values = {
        {{9, 13, 20}, {26, 38, 51}, {79, 101, 139}, {40, 210, 185}, {255, 184, 64}, {179, 61, 71}, {64, 166, 110}, {77, 140, 230}}};

    RGB &operator[](Slot slot) {
        return values[slot];
    }

    const RGB &operator[](Slot slot) const {
        return values[slot];
    }

    static std::string trim(const std::string &s) {
        auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    }

    // A line is just: name = #RRGGBB. No JSON, regex, or parser dependency.
    // Invalid/unknown entries retain the default; later duplicates win.
    bool load(const std::filesystem::path &path) {
        std::ifstream file(path);
        if (!file)
            return false;
        Colors loaded;
        std::string line;
        while (std::getline(file, line)) {
            line = line.substr(0, line.find(';'));
            auto equal = line.find('=');
            if (equal == std::string::npos)
                continue;
            auto name = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
            if (value.size() != 7 || value[0] != '#')
                continue;
            unsigned int rgb = 0;
            auto parsed = std::from_chars(value.data() + 1, value.data() + 7, rgb, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + 7)
                continue;
            for (size_t i = 0; i < names.size(); ++i)
                if (name == names[i]) {
                    loaded.values[i] = {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb)};
                    break;
                }
        }

        if (file.bad())
            return false;
        *this = loaded;
        return true;
    }

    bool save(const std::filesystem::path &path) const {
        // Stage the complete file before replacement, so a failed write does not
        // truncate the user's existing configuration.
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::trunc);
        if (!file)
            return false;
        file << "; Fergo Fast GCode Viewer colors\n; name = #RRGGBB\n\n";
        for (size_t i = 0; i < names.size(); ++i)
            file << names[i] << " = " << values[i].hex() << '\n';
        file.close();
        if (!file)
            return false;
        std::error_code error;
        // Windows rename does not replace existing destinations. Keep a backup
        // until the completed temporary file has been moved into place.
        auto backup = path;
        backup += ".bak";
        bool existing = std::filesystem::exists(path, error);
        if (error)
            return false;
        if (existing) {
            std::filesystem::remove(backup, error);
            if (error)
                return false;
            std::filesystem::rename(path, backup, error);
            if (error)
                return false;
        }

        std::filesystem::rename(temporary, path, error);
        if (error) {
            if (existing) {
                std::error_code ignored;
                std::filesystem::rename(backup, path, ignored);
            }

            return false;
        }

        if (existing)
            std::filesystem::remove(backup, error);
        return true;
    }
};
