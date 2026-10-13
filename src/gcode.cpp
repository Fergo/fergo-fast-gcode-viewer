#include "gcode.h"
#include "app_info.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gc {
namespace {
constexpr double pi = 3.14159265358979323846;

double length(Vec3 a, Vec3 b) {
    return std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z);
}

double sweep(double a, double b, bool cw) {
    double d = b - a;
    if (cw) {
        while (d >= -1e-10)
            d -= 2 * pi;
    } else {
        while (d <= 1e-10)
            d += 2 * pi;
    }
    return d;
}
} // namespace

Model parse(const std::filesystem::path &path, const std::atomic<bool> *cancel, std::atomic<float> *progress, const std::filesystem::path &logPath) {
    auto start = std::chrono::steady_clock::now();
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open file");

    const auto bytes = std::filesystem::file_size(path);
    Model m;
    m.name = path.filename().u8string();

    std::ofstream log;
    if (!logPath.empty()) {
        log.open(logPath, std::ios::binary | std::ios::trunc);
        if (log)
            log << FERGO_APP_NAME " - Unsupported commands\nFile: " << path.u8string() << "\nFormat: physical line | command | original source\n"
                << "Unknown G commands block movement on that line. Unknown M "
                   "commands are ignored.\n"
                << "A COMPLETE footer indicates a finished scan.\n\n";
        else
            m.logError = "Cannot create unsupported-command log: " + logPath.u8string();
    }
    // Reserve conservatively; a massive file must not trigger a massive allocation up front.
    m.vertices.reserve(static_cast<size_t>(std::min<std::uint64_t>(bytes / 8, 2000000)));
    Vec3 p{}, offset{};
    double e = 0, eo = 0, unit = 1;
    bool absolute = true, eAbsolute = true, arcAbsolute = false;
    int motion = 0, plane = 17, tool = 0;
    std::array<double, 256> toolE{}, toolOffset{};
    std::array<bool, 3> homed{};
    double savedFeedrate = 100;
    std::uint64_t consumed = 0;

    auto add = [&](Vec3 a, Vec3 b, bool print) {
        if (length(a, b) < 1e-9)
            return;
        if (m.vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max() - 2))
            throw std::runtime_error("File exceeds GPU vertex limit");
        const std::uint8_t r = print ? 40 : 79, g = print ? 210 : 101, bl = print ? 185 : 139;
        for (auto v : {a, b}) {
            m.vertices.push_back(
                {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), r, g, bl, static_cast<std::uint8_t>(print ? 255 : 0)});
            if (m.vertices.size() == 1)
                m.min = m.max = v;
            else {
                m.min.x = std::min(m.min.x, v.x);
                m.min.y = std::min(m.min.y, v.y);
                m.min.z = std::min(m.min.z, v.z);
                m.max.x = std::max(m.max.x, v.x);
                m.max.y = std::max(m.max.y, v.y);
                m.max.z = std::max(m.max.z, v.z);
            }
        }
    };
    std::string line;

    auto unsupported = [&](char letter, double code) {
        ++m.unsupported;
        if (log) {
            char number[64];
            auto result = std::to_chars(number, number + sizeof number, code);
            log << m.lines << " | " << letter;
            if (result.ec == std::errc{})
                log.write(number, result.ptr - number);
            else
                log << code;
            log << " | " << line << '\n';
        }
    };
    while (std::getline(in, line)) {
        ++m.lines;
        consumed += line.size() + 1;
        if ((m.lines & 4095) == 0) {
            if (cancel && cancel->load())
                throw std::runtime_error("Cancelled");
            if (progress)
                progress->store(bytes ? std::min(1.f, float(double(consumed) / bytes)) : 1.f);
        }
        std::array<double, 26> val{};
        std::array<bool, 26> has{}, seen{};
        std::vector<double> gs, ms;
        int comment = 0;
        for (size_t i = 0; i < line.size();) {
            char c = line[i++];
            if (c == ';' || c == '*')
                break;
            if (c == '(') {
                ++comment;
                continue;
            }
            if (c == ')') {
                comment = std::max(0, comment - 1);
                continue;
            }
            if (comment)
                continue;
            if (c >= 'a' && c <= 'z')
                c -= 32;
            if (c < 'A' || c > 'Z')
                continue;
            seen[c - 'A'] = true;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            auto first = line.data() + i, last = line.data() + line.size();
            if (first != last && *first == '+')
                ++first;
            double v = 0;
            auto result = std::from_chars(first, last, v, std::chars_format::fixed);
            if (result.ec != std::errc{} || !std::isfinite(v))
                continue;
            i = static_cast<size_t>(result.ptr - line.data());
            if (c == 'G')
                gs.push_back(v);
            else if (c == 'M')
                ms.push_back(v);
            else {
                has[c - 'A'] = true;
                val[c - 'A'] = v;
            }
        }
        auto h = [&](char c) { return has[c - 'A']; };
        auto v = [&](char c) { return val[c - 'A']; };
        bool setPosition = false, home = false, block = false, explicitMotion = false;
        for (double g : gs) {
            if (g >= 0 && g <= 3 && g == std::floor(g)) {
                motion = int(g);
                explicitMotion = true;
            } else if (g == 90)
                absolute = true;
            else if (g == 91)
                absolute = false;
            else if (g == 90.1)
                arcAbsolute = true;
            else if (g == 91.1)
                arcAbsolute = false;
            else if (g == 20)
                unit = 25.4;
            else if (g == 21)
                unit = 1;
            else if (g == 17 || g == 18 || g == 19)
                plane = int(g);
            else if (g == 92)
                setPosition = true;
            else if (g == 28)
                home = true;
            else {
                unsupported('G', g);
                block = true;
            }
        }
        for (double code : ms) {
            if (code == 82)
                eAbsolute = true;
            else if (code == 83)
                eAbsolute = false;
            else if (code == 73) {
                if (h('P'))
                    m.settings.progressPercent = std::clamp(v('P'), 0., 100.);
                if (h('R'))
                    m.settings.remainingMinutes = std::max(0., v('R'));
            } else if (code == 201 || code == 203 || code == 205) {
                auto &axes = code == 201 ? m.settings.maxAcceleration : code == 203 ? m.settings.maxFeedrate
                                                                                    : m.settings.jerk;
                const char names[] = "XYZE";
                for (int axis = 0; axis < 4; ++axis)
                    if (h(names[axis]))
                        axes[axis] = v(names[axis]) * unit;
                if (code == 205) {
                    if (h('S'))
                        m.settings.minPrintFeedrate = v('S') * unit;
                    if (h('T'))
                        m.settings.minTravelFeedrate = v('T') * unit;
                    if (h('B'))
                        m.settings.minSegmentTime = v('B');
                    if (h('J'))
                        m.settings.junctionDeviation = v('J') * unit;
                }
            } else if (code == 204) {
                if (h('S'))
                    m.settings.printAcceleration = m.settings.travelAcceleration = v('S') * unit;
                if (h('P'))
                    m.settings.printAcceleration = v('P') * unit;
                if (h('R'))
                    m.settings.retractAcceleration = v('R') * unit;
                if (h('T'))
                    m.settings.travelAcceleration = v('T') * unit;
            } else if (code == 220) {
                if (seen['B' - 'A'])
                    savedFeedrate = m.settings.feedratePercent;
                if (seen['R' - 'A'])
                    m.settings.feedratePercent = savedFeedrate;
                if (h('S'))
                    m.settings.feedratePercent = std::max(0., v('S'));
            } else if (code == 221 || code == 900) {
                int selected = h('T') ? int(std::clamp(v('T'), 0., 255.)) : tool;
                if (code == 221 && h('S'))
                    m.settings.flowPercent[selected] = std::max(0., v('S'));
                if (code == 900 && h('K'))
                    m.settings.linearAdvance[selected] = std::max(0., v('K'));
            } else if (code == 104 || code == 109 || code == 140 || code == 190 || code == 106 || code == 107 || code == 84 || code == 400) {
            } else
                unsupported('M', code);
        }
        if (home && !block) {
            // Without a machine profile, home is assumed to be zero. Do not
            // invent a homing trajectory or connect it to the previous printed
            // segment.
            bool all = !seen['X' - 'A'] && !seen['Y' - 'A'] && !seen['Z' - 'A'];
            bool x = all || seen['X' - 'A'], y = all || seen['Y' - 'A'], z = all || seen['Z' - 'A'];
            bool skip = seen['O' - 'A'] && (!x || homed[0]) && (!y || homed[1]) && (!z || homed[2]);
            if (!skip) {
                if (x) {
                    p.x = offset.x = 0;
                    homed[0] = true;
                }
                if (y) {
                    p.y = offset.y = 0;
                    homed[1] = true;
                }
                if (z) {
                    p.z = offset.z = 0;
                    homed[2] = true;
                }
            }
            continue;
        }
        if (h('T') && gs.empty() && ms.empty()) {
            int next = int(std::clamp(v('T'), 0., 255.));
            toolE[tool] = e;
            toolOffset[tool] = eo;
            tool = next;
            e = toolE[tool];
            eo = toolOffset[tool];
            continue;
        }
        if (setPosition) {
            if (h('X'))
                offset.x = p.x - v('X') * unit;
            if (h('Y'))
                offset.y = p.y - v('Y') * unit;
            if (h('Z'))
                offset.z = p.z - v('Z') * unit;
            if (h('E'))
                eo = e - v('E') * unit;
            continue;
        }
        if (block || (!ms.empty() && !explicitMotion))
            continue;
        if (!h('X') && !h('Y') && !h('Z') && !h('E') && !h('I') && !h('J') && !h('K') && !h('R'))
            continue;
        Vec3 q = p;
        if (h('X'))
            q.x = absolute ? v('X') * unit + offset.x : p.x + v('X') * unit;
        if (h('Y'))
            q.y = absolute ? v('Y') * unit + offset.y : p.y + v('Y') * unit;
        if (h('Z'))
            q.z = absolute ? v('Z') * unit + offset.z : p.z + v('Z') * unit;
        double ne = h('E') ? (eAbsolute ? v('E') * unit + eo : e + v('E') * unit) : e;
        // Flow affects deposited material, never the logical E coordinate
        // (G92/M82).
        bool print = ne > e + 1e-8 && m.settings.flowPercent[tool] > 0;
        m.extrusion |= print;
        auto before = m.vertices.size();
        if (motion == 2 || motion == 3) {
            // Right-handed plane coordinates: XY, ZX, YZ.
            auto components = [&](Vec3 a) {
                return plane == 17   ? std::array<double, 3>{a.x, a.y, a.z}
                       : plane == 18 ? std::array<double, 3>{a.z, a.x, a.y}
                                     : std::array<double, 3>{a.y, a.z, a.x};
            };
            auto from = [&](double u, double w, double t) { return plane == 17 ? Vec3{u, w, t} : plane == 18 ? Vec3{w, t, u}
                                                                                                             : Vec3{t, u, w}; };
            const char ca = plane == 17 ? 'I' : plane == 18 ? 'K'
                                                            : 'J',
                       cb = plane == 17 ? 'J' : plane == 18 ? 'I'
                                                            : 'K';
            auto a = components(p), b = components(q), off = components(offset);
            double cu = a[0], cv = a[1];
            bool valid = true, cw = motion == 2;
            if (h(ca) || h(cb)) {
                cu = h(ca) ? (arcAbsolute ? v(ca) * unit + off[0] : a[0] + v(ca) * unit) : a[0];
                cv = h(cb) ? (arcAbsolute ? v(cb) * unit + off[1] : a[1] + v(cb) * unit) : a[1];
            } else if (h('R')) {
                double radius = std::abs(v('R') * unit), dx = b[0] - a[0], dy = b[1] - a[1], d = std::hypot(dx, dy);
                if (d < 1e-9 || d > 2 * radius + 1e-7)
                    valid = false;
                else {
                    double height = std::sqrt(std::max(0., radius * radius - d * d / 4));
                    double sign = cw ? -1. : 1.;
                    if (v('R') < 0)
                        sign = -sign;
                    cu = (a[0] + b[0]) / 2 - sign * dy / d * height;
                    cv = (a[1] + b[1]) / 2 + sign * dx / d * height;
                }
            } else
                valid = false;
            double radius = std::hypot(a[0] - cu, a[1] - cv);
            if (radius < 1e-9 || std::abs(std::hypot(b[0] - cu, b[1] - cv) - radius) > std::max(.05, radius * .001))
                valid = false;
            if (valid) {
                double angle = std::atan2(a[1] - cv, a[0] - cu), delta = sweep(angle, std::atan2(b[1] - cv, b[0] - cu), cw);
                double step = 2 * std::acos(std::clamp(1 - .05 / radius, -1., 1.));
                int count = int(std::clamp(std::ceil(std::abs(delta) / std::min(.12, std::max(step, .001))), 1., 100000.));
                Vec3 prev = p;
                for (int k = 1; k <= count; ++k) {
                    double t = double(k) / count, ang = angle + delta * t;
                    Vec3 cur = k == count ? q : from(cu + radius * std::cos(ang), cv + radius * std::sin(ang), a[2] + (b[2] - a[2]) * t);
                    add(prev, cur, print);
                    prev = cur;
                }
            } else {
                ++m.invalidArcs;
                add(p, q, print);
            }
        } else
            add(p, q, print);
        if (m.vertices.size() != before)
            m.moves.push_back({q, static_cast<std::uint32_t>(m.vertices.size()), m.lines});
        p = q;
        e = ne;
    }
    if (in.bad())
        throw std::runtime_error("Error reading file");
    if (cancel && cancel->load())
        throw std::runtime_error("Cancelled");
    if (log.is_open()) {
        log << "\nCOMPLETE: " << m.lines << " lines, " << m.unsupported << " unsupported commands.\n";
        log.flush();
        if (!log)
            m.logError = "Cannot finish unsupported-command log: " + logPath.u8string();
    }
    m.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (progress)
        progress->store(1);
    return m;
}
} // namespace gc
