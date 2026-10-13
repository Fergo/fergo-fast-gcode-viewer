#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

// Build-only tool shared by the portable Windows script and CMake. The viewer
// needs neither this executable nor the GLSL files after compilation.
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: embed-shaders SHADER_DIRECTORY OUTPUT_HEADER");

        std::string header = "// Generated from src/shaders; edit the GLSL files instead.\n#pragma once\nnamespace EmbeddedShaders {\n";
        const char *names[] = {"vertex", "geometry", "fragment"};
        const char *files[] = {"line.vert", "line.geom", "line.frag"};
        for (int i = 0; i < 3; ++i) {
            const auto path = std::filesystem::path(argv[1]) / files[i];
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read " + path.string());

            const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (input.bad() || source.empty())
                throw std::runtime_error("Invalid or empty shader: " + path.string());

            // Reject a delimiter collision instead of silently emitting broken C++.
            if (source.find(")FERGO_SHADER\"") != std::string::npos)
                throw std::runtime_error("Reserved raw-string delimiter in " + path.string());

            header += "inline constexpr char ";
            header += names[i];
            header += "[] = R\"FERGO_SHADER(" + source + ")FERGO_SHADER\";\n";
        }

        header += "}\n";
        const auto outputPath = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(outputPath.parent_path());
        // Avoid changing timestamps (and triggering a viewer rebuild) if the
        // generated content already matches the sources.
        std::ifstream previous(outputPath, std::ios::binary);
        const std::string old((std::istreambuf_iterator<char>(previous)), std::istreambuf_iterator<char>());
        previous.close();
        if (old != header) {
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output << header;
            output.close();
            if (!output)
                throw std::runtime_error("Cannot write " + outputPath.string());
        }

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
