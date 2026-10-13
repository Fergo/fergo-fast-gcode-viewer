#include "colors.h"
#include <iostream>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    const std::filesystem::path path = "colors-test.txt";
    try {
        Colors colors;
        check(!colors.load("colors-test-missing-folder/colors.txt"), "Missing file should fail");
        {
            std::ofstream file(path);
            file << "; Comment\n background = #aAbBcC ; Comment\r\n"
                    "line = #FF00GG\ngrid = #1234567\ntravel = #123\n"
                    "unknown = #FFFFFF\nnozzle = #010203\nnozzle = #040506\n";
        }

        check(colors.load(path), "Load failed");
        check(colors[Colors::Background].hex() == "#AABBCC", "Whitespace and hex case");
        check(colors[Colors::Line].hex() == Colors{}[Colors::Line].hex(), "Invalid value must retain default");
        check(colors[Colors::Nozzle].hex() == "#040506", "Last duplicate must win");
        check(colors.save(path), "Save failed");
        Colors restored;
        check(restored.load(path), "Reload failed");
        for (int i = 0; i < Colors::Count; ++i)
            check(restored.values[i].hex() == colors.values[i].hex(), "Round trip mismatch");
        // A write failure must preserve an existing configuration.
        auto blocked = path;
        blocked += ".tmp";
        std::filesystem::create_directory(blocked);
        colors[Colors::Background] = {0, 0, 0};
        check(!colors.save(path), "Blocked write should fail");
        check(restored.load(path) && restored[Colors::Background].hex() == "#AABBCC", "Failed save damaged existing file");
        std::filesystem::remove(blocked);
        std::filesystem::remove(path);
        std::cout << "All color settings tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
