#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gc {
struct Vec3 {
    double x = 0, y = 0, z = 0;
};

struct Vertex {
    float x, y, z;
    std::uint8_t r, g, b, a;
};

struct Move {
    Vec3 end;
    std::uint32_t vertexEnd;
    std::uint64_t line;
};

// Last declared slicer/firmware settings. Timing and pressure are not simulated.
struct PrintSettings {
    std::array<double, 4> maxAcceleration{}, maxFeedrate{}, jerk{}; // XYZ E, mm units
    double printAcceleration = 0, retractAcceleration = 0, travelAcceleration = 0;
    double minPrintFeedrate = 0, minTravelFeedrate = 0, minSegmentTime = 0, junctionDeviation = 0;
    double feedratePercent = 100, progressPercent = -1, remainingMinutes = -1;
    std::array<double, 256> flowPercent{}, linearAdvance{};

    PrintSettings() {
        flowPercent.fill(100);
    }
};

struct Model {
    std::vector<Vertex> vertices;
    std::vector<Move> moves;
    Vec3 min{}, max{};
    std::uint64_t lines = 0, unsupported = 0, invalidArcs = 0;
    double seconds = 0;
    bool extrusion = false;
    std::string name;
    std::string logError;
    PrintSettings settings;
};

Model parse(const std::filesystem::path &, const std::atomic<bool> *cancel = nullptr, std::atomic<float> *progress = nullptr,
            const std::filesystem::path &logPath = {});
} // namespace gc
