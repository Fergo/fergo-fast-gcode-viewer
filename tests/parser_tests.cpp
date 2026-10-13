#include "gcode.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-5;
}

gc::Model parse(const std::string &content) {
    std::ofstream file("parser-test.tmp", std::ios::binary);
    file << content;
    file.close();
    try {
        auto m = gc::parse("parser-test.tmp");
        std::filesystem::remove("parser-test.tmp");
        return m;
    } catch (...) {
        std::filesystem::remove("parser-test.tmp");
        throw;
    }
}

int main(int argc, char **argv) {
    try {
        if (argc > 2 && std::string(argv[1]) == "--inspect") {
            auto m = gc::parse(std::filesystem::u8path(argv[2]));
            std::cout << "INSPECT lines=" << m.lines << " moves=" << m.moves.size() << " unsupported=" << m.unsupported << " invalid_arcs=" << m.invalidArcs
                      << " seconds=" << m.seconds << '\n';
            return m.unsupported == 0 && m.invalidArcs == 0 ? 0 : 1;
        }
        if (argc > 1 && std::string(argv[1]) == "--benchmark") {
            {
                std::ofstream f("parser-benchmark.tmp");
                f << "G21\nG90\nM83\n";
                for (int i = 0; i < 1000000; ++i)
                    f << "G1 X" << (i % 200) << " Y" << ((i / 200) % 200) << " Z" << (i / 40000) * .2 << " E.01\n";
            }
            auto bytes = std::filesystem::file_size("parser-benchmark.tmp");
            auto m = gc::parse("parser-benchmark.tmp");
            std::filesystem::remove("parser-benchmark.tmp");
            std::cout << "BENCHMARK bytes=" << bytes << " moves=" << m.moves.size() << " seconds=" << m.seconds << " vertices=" << m.vertices.size() << '\n';
            return 0;
        }
        auto m = parse("G21\nG90\nM82\nG1X10Y20Z.2E1\nX20E2\nG91\nG1X5\nM83\nG1Y-2E."
                       "3\nG92X0E0\nG90\nG1X2E.5\n");
        check(m.moves.size() == 5, "modal movements");
        check(near(m.moves.back().end.x, 27) && near(m.moves.back().end.y, 18), "G92 physical coordinates");
        check(m.vertices[0].a == 255 && m.vertices[4].a == 0, "extrusion colors");
        m = parse("G20\nG91\nG1X1\nG1X1\n");
        check(near(m.moves.back().end.x, 50.8), "inches");
        m = parse("G1 X10\nG3 X0 Y10 I-10 J0 E1\n");
        check(m.moves.size() == 2 && m.vertices.size() > 20 && m.invalidArcs == 0, "IJK arc");
        check(near(m.moves.back().end.y, 10), "arc endpoint");
        for (int plane : {17, 18, 19}) {
            std::string start = plane == 17 ? "G1 X10\nG3 X0 Y10 I-10 J0\n" : plane == 18 ? "G1 Z10\nG3 Z0 X10 K-10 I0\n"
                                                                                          : "G1 Y10\nG3 Y0 Z10 J-10 K0\n";
            m = parse("G" + std::to_string(plane) + "\n" + start);
            check(m.invalidArcs == 0 && m.vertices.size() > 20, "arc plane");
        }
        auto minor = parse("G1 X10\nG3 X0 Y10 R10\n");
        auto major = parse("G1 X10\nG3 X0 Y10 R-10\n");
        check(major.vertices.size() > minor.vertices.size() && major.invalidArcs == 0, "signed R arc");
        m = parse("G1 X10\nG2 I-10 J0\n");
        check(m.moves.size() == 2 && m.vertices.size() > 100, "full circle");
        m = parse("G1 X10\nG2 X20 R1\n");
        check(m.invalidArcs == 1 && near(m.moves.back().end.x, 20), "invalid arc fallback");
        m = parse("G1X1 (X500) Y2 ; X900\nM104 S210\nG123 X0\nG1X3\n");
        check(m.moves.size() == 2 && near(m.moves[0].end.x, 1) && m.unsupported == 1, "comments and unsupported commands");
        m = parse("G90 G1 X10 Y0\nG90.1\nG3 X0 Y10 I0 J0\n");
        check(m.invalidArcs == 0, "absolute arc centers");
        m = parse("M83\nG1X1E1\nG1E-1\nG1E1\nG1X2E1\n");
        check(m.moves.size() == 2, "stationary extruder commands");
        m = parse("G1 X+1.5 Y-2.5\n");
        check(near(m.moves.back().end.x, 1.5), "signed values");
        m = parse("; empty\nM104 S200\n");
        check(m.moves.empty(), "empty input");
        m = parse("G1 X10 Y20 Z30 E1\nG92 X100 Y200 Z300\nG28 X Y\nG91\nG1 X2 "
                  "Y3 Z1 E2\n");
        check(m.unsupported == 0 && m.moves.size() == 2, "homing has no invented geometry");
        check(near(m.vertices[2].x, 0) && near(m.vertices[2].y, 0) && near(m.vertices[2].z, 30), "bare-axis selective homing");
        check(near(m.moves.back().end.x, 2) && near(m.moves.back().end.y, 3) && near(m.moves.back().end.z, 31), "relative move after home");
        m = parse("G1 X10 Y20 Z30\nG92 X100\nG28\nG1 X1 Y2 Z3\n");
        check(m.moves.size() == 2 && near(m.vertices[2].x, 0) && near(m.vertices[2].z, 0) && near(m.moves.back().end.x, 1),
              "full home resets workspace offsets");
        m = parse("G28\nG1 X10\nG28 O\nG91\nG1 X1\n");
        check(near(m.moves.back().end.x, 11), "G28 O skips homed axes");
        m = parse("M82\nM221 S0\nG1 X1 E1\nM221 S200\nG1 X2 E1\nG1 X3 E2\nM221 "
                  "T1 S0\nT1\nG1 X4 E1\nT0\nG1 X5 E3\n");
        check(m.vertices[0].a == 0 && m.vertices[2].a == 0 && m.vertices[4].a == 255 && m.vertices[6].a == 0 && m.vertices[8].a == 255,
              "flow suppression and per-tool logical E continuity");
        m = parse("G1 X1 Y2 Z3 E1\nM73 P25 R54\nM201 X5000 Y5000 Z500 E5000\nM203 "
                  "X500 Y500 Z30 E100\nM204 P5000 R2500 T5000\nM205 X10 Y10 Z.4 "
                  "E5\nM220 S150\nM221 S100\nM900 K.04\nG1 X2 E2\n");
        check(m.unsupported == 0 && m.moves.size() == 2 && m.vertices.size() == 4, "all logged commands recognized without spurious moves");
        check(near(m.settings.remainingMinutes, 54) && near(m.settings.maxFeedrate[2], 30) && near(m.settings.linearAdvance[0], .04),
              "slicer configuration retained");
        std::atomic<bool> cancel{true};
        std::ofstream f("parser-test.tmp");
        for (int i = 0; i < 5000; ++i)
            f << "G1 X" << i << "\n";
        f.close();
        bool cancelled = false;
        try {
            gc::parse("parser-test.tmp", &cancel);
        } catch (...) {
            cancelled = true;
        }
        std::filesystem::remove("parser-test.tmp");
        check(cancelled, "cancellation");
        {
            std::ofstream source("parser-test.tmp");
            source << "; M999 ignored comment\nG123 X0 ; home\nM999 "
                      "P1000\nG59.1 M998 K0.1\nM999 P2000\nG1 X2\nM104 S200\n";
            source.close();
            auto logged = gc::parse("parser-test.tmp", nullptr, nullptr, "parser-test.unsupported.log");
            check(logged.unsupported == 5 && logged.logError.empty(), "log count");
            std::ifstream input("parser-test.unsupported.log");
            std::string contents((std::istreambuf_iterator<char>(input)), {});
            input.close();
            check(contents.find("2 | G123 | G123 X0 ; home") != std::string::npos, "log line and source");
            check(contents.find("4 | G59.1 |") != std::string::npos && contents.find("4 | M998 |") != std::string::npos, "multiple unknown commands");
            check(contents.find("5 | M999 | M999 P2000") != std::string::npos, "repeated occurrences");
            check(contents.find("7 | M104") == std::string::npos && contents.find("COMPLETE: 7 lines, 5 unsupported commands.") != std::string::npos,
                  "log footer and supported exclusion");
            source.open("parser-test.tmp", std::ios::trunc);
            source << "G1 X3\n";
            source.close();
            gc::parse("parser-test.tmp", nullptr, nullptr, "parser-test.unsupported.log");
            input.open("parser-test.unsupported.log");
            contents.assign(std::istreambuf_iterator<char>(input), {});
            input.close();
            check(contents.find("G123") == std::string::npos && contents.find("0 unsupported commands.") != std::string::npos, "log overwrite and zero count");
            logged = gc::parse("parser-test.tmp", nullptr, nullptr, "parser-missing-folder/log.txt");
            check(!logged.logError.empty() && logged.moves.size() == 1, "log failure preserves geometry");
            std::filesystem::remove("parser-test.tmp");
            std::filesystem::remove("parser-test.unsupported.log");
        }
        std::cout << "All parser tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
