#include "app_icon.h"
#include "app_info.h"
#include "colors.h"
#include "gcode.h"
#include "line_shader.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr double pi = 3.14159265358979323846;
using gc::Vec3;

Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(Vec3 a) {
    return a * (1 / std::sqrt(dot(a, a)));
}

using GenBuffers = void(APIENTRY *)(GLsizei, GLuint *);
using BindBuffer = void(APIENTRY *)(GLenum, GLuint);
using BufferData = void(APIENTRY *)(GLenum, std::ptrdiff_t, const void *, GLenum);
using DeleteBuffers = void(APIENTRY *)(GLsizei, const GLuint *);
GenBuffers genBuffers;
BindBuffer bindBuffer;
BufferData bufferData;
DeleteBuffers deleteBuffers;

struct Camera {
    Vec3 target{};
    double yaw = -.7, pitch = .85, distance = 250, span = 200;

    Vec3 direction() const {
        return {std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};
    }

    void fit(const gc::Model &m) {
        target = (m.min + m.max) * .5;
        span = std::max({m.max.x - m.min.x, m.max.y - m.min.y, m.max.z - m.min.z, 10.});
        distance = span * 2.2;
    }

    void apply(int w, int h) const {
        double nearZ = std::max(.001, distance * .0001), farZ = distance + span * 20 + 1000;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        double top = nearZ * std::tan(45 * pi / 360), aspect = double(w) / std::max(h, 1);
        glFrustum(-top * aspect, top * aspect, -top, top, nearZ, farZ);
        Vec3 eye = target + direction() * distance, forward = normalized(target - eye);
        Vec3 right = normalized(cross(forward, {0, 0, 1})), up = cross(right, forward);
        double matrix[] = {right.x, up.x, -forward.x, 0, right.y, up.y, -forward.y, 0, right.z, up.z, -forward.z, 0, 0, 0, 0, 1};
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(matrix);
        glTranslated(-eye.x, -eye.y, -eye.z);
    }
};

// Compact 5x7 bitmap alphabet. No font library or runtime asset loading.
const char *glyph(char c) {
    static const char *letters[] = {"0E11111F111111", "1E11111E11111E", "0E11101010110E", "1E11111111111E", "1F10101E10101F", "1F10101E101010",
                                    "0E11101711110F", "1111111F111111", "0E04040404040E", "0702020212120C", "11121418141211", "1010101010101F",
                                    "111B1515111111", "11191513111111", "0E11111111110E", "1E11111E101010", "0E11111115120D", "1E11111E141211",
                                    "0F10100E01011E", "1F040404040404", "1111111111110E", "11111111110A04", "11111115151B11", "11110A040A1111",
                                    "11110A04040404", "1F01020408101F"};
    static const char *digits[] = {"0E11131519110E", "040C040404040E", "0E11010204081F", "1E01010E01011E", "02060A121F0202",
                                   "1F10101E01011E", "0E10101E11110E", "1F010204080808", "0E11110E11110E", "0E11110F01010E"};
    if (c >= 'a' && c <= 'z')
        c -= 32;
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    switch (c) {
    case '.':
        return "00000000000C0C";
    case ':':
        return "000C0C000C0C00";
    case '-':
        return "0000001F000000";
    case '/':
        return "01010204081010";
    case '%':
        return "19190204081313";
    case '+':
        return "0004041F040400";
    case '>':
        return "10080402040810";
    case '<':
        return "01020408040201";
    case '_':
        return "0000000000001F";
    case '#':
        return "0A0A1F0A1F0A0A";
    default:
        return "00000000000000";
    }
}

int hex(char c) {
    return c <= '9' ? c - '0' : c - 'A' + 10;
}

void color(float r, float g, float b) {
    glColor3f(r, g, b);
}

void color(RGB c) {
    glColor3f(c.red(), c.green(), c.blue());
}

void rect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

float uiPixelX = 1, uiPixelY = 1;

void text(float x, float y, const std::string &s, float scale = 2) {
    // Quantize both glyph cell dimensions and origin to physical framebuffer
    // pixels. Fractional cells (1.3/1.4/1.6) produced alternating one/two-pixel
    // strokes.
    float sx = std::max(1.f, std::round(scale * uiPixelX)) / uiPixelX;
    float sy = std::max(1.f, std::round(scale * uiPixelY)) / uiPixelY;
    x = std::round(x * uiPixelX) / uiPixelX;
    y = std::round(y * uiPixelY) / uiPixelY;
    glBegin(GL_QUADS);
    for (char c : s) {
        const char *g = glyph(c);
        for (int row = 0; row < 7; ++row) {
            int bits = hex(g[row * 2]) * 16 + hex(g[row * 2 + 1]);
            for (int col = 0; col < 5; ++col)
                if (bits & (1 << (4 - col))) {
                    float a = x + col * sx, b = y + row * sy;
                    glVertex2f(a, b);
                    glVertex2f(a + sx, b);
                    glVertex2f(a + sx, b + sy);
                    glVertex2f(a, b + sy);
                }
        }

        x += 6 * sx;
    }

    glEnd();
}

void button(float x, const char *label, bool active = false) {
    color(active ? .12f : .12f, active ? .38f : .18f, active ? .35f : .25f);
    rect(x, 15, 100, 32);
    color(.84f, .91f, .94f);
    text(x + 10, 24, label, 2);
}

struct ColorsPanel {
    Colors palette;
    std::filesystem::path path;
    bool open = false, changed = false;
    int selected = Colors::Line, dragging = -1;
    std::string notice = "COLORS.TXT / AUTO SAVE";

    static float left(int w) {
        return float(std::max(20, w - 620));
    }

    static float top(int h) {
        return float(std::max(65, (h - 330) / 2));
    }

    void save() {
        if (palette.save(path)) {
            changed = false;
            notice = "SAVED TO COLORS.TXT";
        } else
            notice = "SAVE FAILED / CHECK FOLDER ACCESS";
    }

    void close() {
        if (changed)
            save();
        dragging = -1;
        open = false;
        SDL_CaptureMouse(false);
    }

    void adjust(float x, int w) {
        int value = int(std::lround(std::clamp((x - left(w) - 320) / 190, 0.f, 1.f) * 255));
        auto &c = palette.values[selected];
        if (dragging == 0)
            c.r = static_cast<unsigned char>(value);
        if (dragging == 1)
            c.g = static_cast<unsigned char>(value);
        if (dragging == 2)
            c.b = static_cast<unsigned char>(value);
        changed = true;
        notice = "PREVIEW / RELEASE TO SAVE";
    }

    void mouseDown(float x, float y, int w, int h) {
        float lx = x - left(w), ly = y - top(h);
        if (lx >= 16 && lx < 260 && ly >= 58 && ly < 58 + Colors::Count * 24) {
            selected = int((ly - 58) / 24);
        } else if (lx >= 310 && lx <= 520 && ly >= 112 && ly < 232) {
            dragging = int((ly - 112) / 40);
            adjust(x, w);
            SDL_CaptureMouse(true);
        } else if (ly >= 280 && ly < 316) {
            if (lx >= 20 && lx < 140)
                save();
            else if (lx >= 160 && lx < 280) {
                if (palette.load(path)) {
                    changed = false;
                    notice = "RELOADED COLORS.TXT";
                } else
                    notice = "LOAD FAILED / CHECK COLORS.TXT";
            } else if (lx >= 300 && lx < 420) {
                palette = Colors{};
                changed = true;
                save();
            } else if (lx >= 460 && lx < 580)
                close();
        }
    }

    void mouseUp() {
        if (dragging >= 0) {
            dragging = -1;
            save();
        }

        SDL_CaptureMouse(false);
    }

    void draw(int w, int h) const {
        float x = left(w), y = top(h);
        color(.035f, .05f, .075f);
        rect(x - 3, y - 3, 606, 336);
        color(.10f, .14f, .19f);
        rect(x, y, 600, 330);
        color(.16f, .83f, .72f);
        text(x + 20, y + 16, "COLORS", 2);
        color(.66f, .76f, .83f);
        text(x + 20, y + 38, notice, 1);
        for (int i = 0; i < Colors::Count; ++i) {
            color(i == selected ? .18f : .12f, i == selected ? .28f : .18f, i == selected ? .34f : .24f);
            rect(x + 16, y + 58 + i * 24, 244, 22);
            color(palette.values[i]);
            rect(x + 24, y + 62 + i * 24, 20, 14);
            color(.85f, .91f, .94f);
            text(x + 56, y + 63 + i * 24, Colors::names[i], 2);
        }

        auto c = palette.values[selected];
        color(c);
        rect(x + 290, y + 60, 50, 34);
        color(.85f, .91f, .94f);
        text(x + 358, y + 70, c.hex(), 2);
        const int channels[] = {c.r, c.g, c.b};
        const char *labels[] = {"R", "G", "B"};
        for (int channel = 0; channel < 3; ++channel) {
            float cy = y + 126 + channel * 40;
            color(.85f, .91f, .94f);
            text(x + 286, cy - 5, labels[channel], 2);
            color(.2f, .28f, .35f);
            rect(x + 320, cy, 190, 6);
            color(channel == 0 ? .9f : .3f, channel == 1 ? .85f : .3f, channel == 2 ? .95f : .3f);
            float amount = channels[channel] / 255.f * 190;
            rect(x + 320, cy, amount, 6);
            rect(x + 315 + amount, cy - 5, 10, 16);
            color(.85f, .91f, .94f);
            text(x + 530, cy - 5, std::to_string(channels[channel]), 2);
        }

        const char *actions[] = {"SAVE", "RELOAD", "DEFAULTS", "CLOSE"};
        const int positions[] = {20, 160, 300, 460};
        for (int i = 0; i < 4; ++i) {
            color(.16f, .23f, .30f);
            rect(x + positions[i], y + 280, 120, 36);
            color(.85f, .91f, .94f);
            text(x + positions[i] + 8, y + 291, actions[i], 2);
        }
    }
};

struct Dialog {
    std::mutex mutex;
    std::optional<std::string> path;
    bool open = false;

    static void SDLCALL callback(void *user, const char *const *paths, int) {
        auto &self = *static_cast<Dialog *>(user);
        std::lock_guard<std::mutex> lock(self.mutex);
        if (paths && paths[0])
            self.path = paths[0];
        self.open = false;
    }

    void show(SDL_Window *w) {
        std::lock_guard<std::mutex> lock(mutex);
        if (open)
            return;
        open = true;
        SDL_ShowOpenFileDialog(callback, this, w, nullptr, 0, nullptr, false);
    }
};

void screenshot(int w, int h, const char *path) {
    std::vector<unsigned char> data(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, data.data());
    const unsigned stride = (w * 3 + 3) & ~3u, size = 54 + stride * h;
    std::array<unsigned char, 54> header{};
    header[0] = 'B';
    header[1] = 'M';
    auto u32 = [&](int at, unsigned value) {
        for (int j = 0; j < 4; ++j)
            header[at + j] = static_cast<unsigned char>(value >> (j * 8));
    };

    u32(2, size);
    u32(10, 54);
    u32(14, 40);
    u32(18, w);
    u32(22, h);
    header[26] = 1;
    header[28] = 24;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<char *>(header.data()), 54);
    for (int row = 0; row < h; ++row) {
        file.write(reinterpret_cast<char *>(data.data() + size_t(row) * w * 3), w * 3);
        for (unsigned k = w * 3; k < stride; ++k)
            file.put(0);
    }
}

int run(int argc, char **argv) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window *window = SDL_CreateWindow(FERGO_APP_NAME, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(FERGO_APP_NAME, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    }

    if (!window)
        throw std::runtime_error(SDL_GetError());
    SDL_SetWindowMinimumSize(window, 720, 400);
    auto *icon = SDL_CreateSurfaceFrom(appIconWidth, appIconHeight, SDL_PIXELFORMAT_RGBA32, appIconPixels, appIconWidth * 4);
    if (!icon)
        throw std::runtime_error(SDL_GetError());
    bool iconSet = SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
    // Some Linux window managers do not support per-window icons.
    if (!iconSet)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Window icon: %s", SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context)
        throw std::runtime_error(SDL_GetError());
    genBuffers = reinterpret_cast<GenBuffers>(SDL_GL_GetProcAddress("glGenBuffers"));
    bindBuffer = reinterpret_cast<BindBuffer>(SDL_GL_GetProcAddress("glBindBuffer"));
    bufferData = reinterpret_cast<BufferData>(SDL_GL_GetProcAddress("glBufferData"));
    deleteBuffers = reinterpret_cast<DeleteBuffers>(SDL_GL_GetProcAddress("glDeleteBuffers"));
    if (!genBuffers || !bindBuffer || !bufferData || !deleteBuffers)
        throw std::runtime_error("OpenGL 3.3 compatibility required");
    LineShader lineShader;
    lineShader.init();
    SDL_GL_SetSwapInterval(1);
    GLuint buffer = 0;
    genBuffers(1, &buffer);
    Camera camera;
    gc::Model model;
    std::atomic<bool> cancel{false};
    std::atomic<float> progress{0};
    std::future<gc::Model> future;
    std::optional<std::string> pending;
    // Dialog callbacks may outlive the window; retain their small state until
    // process exit.
    auto *dialog = new Dialog;
    bool running = true, dirty = true, playing = false, travel = true, scrubbing = false, loading = false, smoke = false, selftest = false, logEnabled = false,
         about = false, speedDragging = false;
    int testStage = 0, exitCode = 0;
    std::string testPath;
    double testDistance = 0, testYaw = 0;
    size_t testStepExpected = 0;
    Vec3 testTarget{};
    size_t visible = 0;
    double playCursor = 0;
    double playbackSpeed = 1.0;
    std::string status = "DROP GCODE HERE OR CLICK OPEN";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--smoke")
            smoke = true;
        else if (std::string(argv[i]) == "--log")
            logEnabled = true;
        else if (std::string(argv[i]) == "--selftest") {
            smoke = true;
            selftest = true;
        } else {
            pending = argv[i];
            testPath = argv[i];
        }
    }

    ColorsPanel colors;
    const char *basePath = SDL_GetBasePath();
    if (!basePath)
        throw std::runtime_error(SDL_GetError());
    colors.path = std::filesystem::u8path(basePath) / (selftest ? "selftest-colors.txt" : "colors.txt");
    std::error_code colorsError;
    if (std::filesystem::exists(colors.path, colorsError)) {
        if (!colors.palette.load(colors.path))
            colors.notice = "LOAD FAILED / USING DEFAULTS";
    } else if (!colorsError)
        colors.save();
    else
        colors.notice = "CANNOT ACCESS COLORS.TXT";
    int w = 1280, h = 800, pw = 1280, ph = 800;
    float mx = 0, my = 0;
    SDL_Event waited{};
    bool hasWaited = false;
    auto last = std::chrono::steady_clock::now();
    auto fit = [&]() {
        camera.fit(model);
        dirty = true;
    };

    auto scrub = [&](float x) {
        if (model.moves.empty())
            return;
        double t = std::clamp(double(x - 36) / std::max(w - 72, 1), 0., 1.);
        visible = size_t(std::llround(t * model.moves.size()));
        playCursor = double(visible);
        dirty = true;
    };

    auto setSpeed = [&](float x) {
        double t = std::clamp(double(x - 210) / std::max(w - 270, 1), 0., 1.);
        // Logarithmic range: 0.1x .. 1x (center) .. 10x.
        playbackSpeed = std::pow(10., 2 * t - 1);
        dirty = true;
    };
    while (running) {
        SDL_GetWindowSize(window, &w, &h);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        SDL_Event event;
        while (hasWaited ? (event = waited, hasWaited = false, true) : SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                dirty = true;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                speedDragging = scrubbing = false;
                if (colors.dragging >= 0)
                    colors.mouseUp();
                SDL_CaptureMouse(false);
                break;
            case SDL_EVENT_DROP_FILE:
                pending = event.drop.data;
                cancel = true;
                playing = false;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (about || colors.open)
                    break;
                camera.distance = std::clamp(camera.distance * std::exp(-event.wheel.y * .12), .01, std::max(1000., camera.span * 1000));
                dirty = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mx = event.button.x;
                my = event.button.y;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (colors.open) {
                        if (my < 62 && mx >= w - 228 && mx < w - 128)
                            colors.close();
                        else
                            colors.mouseDown(mx, my, w, h);
                        dirty = true;
                        break;
                    }

                    if (about) {
                        about = false;
                        dirty = true;
                        break;
                    }

                    if (playing && my >= h - 146 && my < h - 104) {
                        speedDragging = true;
                        SDL_CaptureMouse(true);
                        setSpeed(mx);
                    } else if (my >= h - 65) {
                        scrubbing = true;
                        scrub(mx);
                        playing = false;
                    } else if (my < 62) {
                        if (mx >= 16 && mx < 116)
                            dialog->show(window);
                        else if (mx >= 128 && mx < 228)
                            fit();
                        else if (mx >= 240 && mx < 340) {
                            travel = !travel;
                            dirty = true;
                        } else if (mx >= 352 && mx < 452) {
                            if (visible == model.moves.size()) {
                                visible = 0;
                                playCursor = 0;
                            }

                            playing = !playing;
                            dirty = true;
                        } else if (mx >= w - 228 && mx < w - 128) {
                            colors.open = true;
                            playing = false;
                            speedDragging = scrubbing = false;
                            dirty = true;
                        } else if (mx >= w - 116 && mx < w - 16) {
                            about = true;
                            playing = false;
                            dirty = true;
                        }
                    }
                }

                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (colors.dragging >= 0) {
                        colors.mouseUp();
                        dirty = true;
                    }

                    speedDragging = scrubbing = false;
                    SDL_CaptureMouse(false);
                }

                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (colors.open) {
                    if (colors.dragging >= 0) {
                        colors.adjust(event.motion.x, w);
                        dirty = true;
                    }

                    mx = event.motion.x;
                    my = event.motion.y;
                    break;
                }

                if (about)
                    break;
                float dx = event.motion.x - mx, dy = event.motion.y - my;
                mx = event.motion.x;
                my = event.motion.y;
                if (speedDragging && playing)
                    setSpeed(mx);
                else if (scrubbing)
                    scrub(mx);
                else if (event.motion.state & SDL_BUTTON_RMASK) {
                    camera.yaw -= dx * .007;
                    camera.pitch = std::clamp(camera.pitch + dy * .007, -1.55, 1.55);
                    dirty = true;
                } else if ((event.motion.state & SDL_BUTTON_LMASK) && my > 62 && my < h - 75) {
                    Vec3 forward = camera.direction() * -1, right = normalized(cross(forward, {0, 0, 1})), up = cross(right, forward);
                    double units = 2 * camera.distance * std::tan(pi / 8) / std::max(h - 150, 1);
                    camera.target = camera.target + right * (-dx * units) + up * (dy * units);
                    dirty = true;
                }

                break;
            }

            case SDL_EVENT_KEY_DOWN:
                if (colors.open) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                        colors.close();
                        dirty = true;
                    }

                    break;
                }

                if (event.key.key == SDLK_F1 && !event.key.repeat) {
                    about = !about;
                    playing = false;
                    dirty = true;
                    break;
                }

                if (about) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                        about = false;
                        dirty = true;
                    }

                    break;
                }

                if (event.key.repeat && event.key.key != SDLK_LEFT && event.key.key != SDLK_RIGHT)
                    break;
                if (event.key.key == SDLK_ESCAPE)
                    running = false;
                else if (event.key.key == SDLK_F || event.key.key == SDLK_HOME)
                    fit();
                else if (event.key.key == SDLK_O)
                    dialog->show(window);
                else if (event.key.key == SDLK_T) {
                    travel = !travel;
                    dirty = true;
                } else if (event.key.key == SDLK_SPACE) {
                    if (visible == model.moves.size()) {
                        visible = 0;
                        playCursor = 0;
                    }

                    playing = !playing;
                    dirty = true;
                } else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                    size_t step = (event.key.mod & SDL_KMOD_SHIFT) ? 100 : 1;
                    visible = event.key.key == SDLK_LEFT ? (visible > step ? visible - step : 0) : std::min(visible + step, model.moves.size());
                    playCursor = double(visible);
                    playing = false;
                    dirty = true;
                }

                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(dialog->mutex);
            if (dialog->path) {
                pending = std::move(dialog->path);
                dialog->path.reset();
                cancel = true;
                playing = false;
            }
        }

        if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = future.get();
                if (!pending) {
                    model = std::move(result);
                    visible = model.moves.size();
                    playCursor = double(visible);
                    fit();
                    while (glGetError() != GL_NO_ERROR) {
                    }

                    bindBuffer(GL_ARRAY_BUFFER, buffer);
                    bufferData(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(model.vertices.size() * sizeof(gc::Vertex)), model.vertices.data(), GL_STATIC_DRAW);
                    bindBuffer(GL_ARRAY_BUFFER, 0);
                    if (glGetError() != GL_NO_ERROR) {
                        model = {};
                        visible = 0;
                        throw std::runtime_error("GPU memory allocation failed");
                    }

                    // CPU geometry is no longer needed after GPU upload.
                    std::vector<gc::Vertex>().swap(model.vertices);
                    char summary[256];
                    std::snprintf(summary, sizeof summary,
                                  "%zu MOVES / %.3f S / %llu UNSUPPORTED / "
                                  "%llu INVALID ARCS",
                                  model.moves.size(), model.seconds, (unsigned long long)model.unsupported, (unsigned long long)model.invalidArcs);
                    status = summary;
                    if (!model.logError.empty()) {
                        status += " / LOG ERROR";
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "GCode log", model.logError.c_str(), window);
                    }

                    SDL_SetWindowTitle(window, (std::string(FERGO_APP_NAME) + " | " + model.name).c_str());
                }
            } catch (const std::exception &e) {
                if (!pending) {
                    status = std::string("ERROR: ") + e.what();
                    SDL_SetWindowTitle(window, FERGO_APP_NAME);
                    if (smoke) {
                        std::fprintf(stderr, "%s\n", status.c_str());
                        running = false;
                        exitCode = 1;
                    }
                }
            }

            loading = false;
            dirty = true;
        }

        if (pending && !future.valid()) {
            std::string path = std::move(*pending);
            pending.reset();
            cancel = false;
            progress = 0;
            loading = true;
            status = "LOADING...";
            dirty = true;
            future = std::async(std::launch::async, [&, path]() {
                auto source = std::filesystem::u8path(path);
                std::filesystem::path logPath;
                if (logEnabled) {
                    logPath = source;
                    logPath += ".unsupported.log";
                }

                return gc::parse(source, &cancel, &progress, logPath);
            });
        } else if (pending)
            cancel = true;
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (playing && !model.moves.empty()) {
            playCursor = std::min(double(model.moves.size()), playCursor + dt * playbackSpeed * std::max(50., double(model.moves.size()) / 30));
            visible = size_t(playCursor);
            if (visible == model.moves.size())
                playing = false;
            dirty = true;
        }

        if (loading)
            dirty = true;
        if (dirty && pw > 0 && ph > 0 && w > 0 && h > 0) {
            glViewport(0, 0, pw, ph);
            auto background = colors.palette[Colors::Background];
            glClearColor(background.red(), background.green(), background.blue(), 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            int bottom = int(100. * ph / h), top = int(62. * ph / h), vh = std::max(1, ph - bottom - top);
            glViewport(0, bottom, pw, vh);
            glEnable(GL_DEPTH_TEST);
            camera.apply(pw, vh);
            // Adaptive XY grid, capped at 42 lines regardless of zoom or file
            // size.
            double gridStep = std::pow(10., std::floor(std::log10(std::max(camera.span, 1.))) - 1);
            if (camera.span / gridStep > 50)
                gridStep *= 10;
            else if (camera.span / gridStep > 20)
                gridStep *= 5;
            else
                gridStep *= 2;
            gridStep = std::max(gridStep, 1.);
            double cx = std::floor(camera.target.x / gridStep) * gridStep, cy = std::floor(camera.target.y / gridStep) * gridStep;
            color(colors.palette[Colors::Grid]);
            glBegin(GL_LINES);
            for (int k = -10; k <= 10; ++k) {
                double d = k * gridStep;
                glVertex3d(cx + d, cy - 10 * gridStep, 0);
                glVertex3d(cx + d, cy + 10 * gridStep, 0);
                glVertex3d(cx - 10 * gridStep, cy + d, 0);
                glVertex3d(cx + 10 * gridStep, cy + d, 0);
            }

            glEnd();
            glBegin(GL_LINES);
            color(colors.palette[Colors::AxisX]);
            glVertex3d(0, 0, .01);
            glVertex3d(gridStep * 3, 0, .01);
            color(colors.palette[Colors::AxisY]);
            glVertex3d(0, 0, .01);
            glVertex3d(0, gridStep * 3, .01);
            color(colors.palette[Colors::AxisZ]);
            glVertex3d(0, 0, 0);
            glVertex3d(0, 0, gridStep * 3);
            glEnd();
            if (visible && !model.moves.empty()) {
                bindBuffer(GL_ARRAY_BUFFER, buffer);
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_COLOR_ARRAY);
                glVertexPointer(3, GL_FLOAT, sizeof(gc::Vertex), nullptr);
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(gc::Vertex), reinterpret_cast<void *>(12));
                lineShader.begin(float(vh), float(camera.distance), float(camera.span), travel, colors.palette);
                glDrawArrays(GL_LINES, 0, model.moves[visible - 1].vertexEnd);
                lineShader.end();
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_VERTEX_ARRAY);
                bindBuffer(GL_ARRAY_BUFFER, 0);
                Vec3 p = model.moves[visible - 1].end;
                glDisable(GL_DEPTH_TEST);
                glPointSize(8);
                color(colors.palette[Colors::Nozzle]);
                glBegin(GL_POINTS);
                glVertex3d(p.x, p.y, p.z);
                glEnd();
                glEnable(GL_DEPTH_TEST);
            }

            glDisable(GL_DEPTH_TEST);
            glViewport(0, 0, pw, ph);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, w, h, 0, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            uiPixelX = float(pw) / w;
            uiPixelY = float(ph) / h;
            color(.065f, .085f, .12f);
            rect(0, 0, float(w), 62);
            rect(0, float(h - 100), float(w), 100);
            button(16, "OPEN");
            button(128, "FIT");
            button(240, "TRAVEL", travel);
            button(352, playing ? "PAUSE" : "PLAY", playing);
            button(float(w - 228), "COLORS", colors.open);
            button(float(w - 116), "ABOUT", about);
            if (w > 1200) {
                color(.43f, .54f, .65f);
                text(480, 24, "FERGO / FAST GCODE VIEWER", 2);
            }

            float textCell = 6 * std::max(1.f, std::round(2 * uiPixelX)) / uiPixelX;
            color(.72f, .82f, .88f);
            std::string clipped = status.substr(0, static_cast<size_t>(std::max(1, int((w - 40) / textCell))));
            text(20, float(h - 86), clipped, 2);
            if (model.moves.empty() && !loading) {
                color(.27f, .43f, .52f);
                text(float(std::max(20, w / 2 - 192)), float(h / 2 - 10), "DROP YOUR GCODE HERE", 3);
            }

            double fraction = model.moves.empty() ? 0. : double(visible) / model.moves.size();
            color(.14f, .21f, .28f);
            rect(36, float(h - 52), float(std::max(1, w - 72)), 6);
            color(.16f, .83f, .72f);
            rect(36, float(h - 52), float(std::max(1, w - 72) * fraction), 6);
            rect(float(36 + std::max(1, w - 72) * fraction - 5), float(h - 58), 10, 18);
            char info[200];
            auto line = visible ? model.moves[visible - 1].line : 0;
            std::snprintf(info, sizeof info, "%.1f%%  MOVE %zu/%zu  LINE %llu", fraction * 100, visible, model.moves.size(), (unsigned long long)line);
            color(.71f, .8f, .86f);
            text(36, float(h - 28), info, 2);
            if (w > 1000 && 36 + std::string(info).size() * textCell < w - 460) {
                color(.4f, .52f, .63f);
                text(float(w - 450), float(h - 28), "RMB ORBIT / LMB PAN / SCROLL ZOOM", 2);
            }

            if (loading) {
                color(.16f, .83f, .72f);
                rect(0, 60, float(w) * progress.load(), 2);
            }

            if (playing) {
                color(.065f, .085f, .12f);
                rect(20, float(h - 146), float(w - 40), 40);
                char speedLabel[40];
                std::snprintf(speedLabel, sizeof speedLabel, "SPEED %.2fX", playbackSpeed);
                color(.78f, .86f, .91f);
                text(36, float(h - 134), speedLabel, 2);
                float speedT = float((std::log10(playbackSpeed) + 1) / 2);
                float trackWidth = float(std::max(w - 270, 1));
                color(.14f, .21f, .28f);
                rect(210, float(h - 128), trackWidth, 6);
                color(.16f, .83f, .72f);
                rect(210, float(h - 128), trackWidth * speedT, 6);
                rect(210 + trackWidth * speedT - 5, float(h - 134), 10, 18);
            }

            if (about) {
                color(.02f, .03f, .05f);
                rect(0, 62, float(w), float(h - 162));
                float left = float(w - 580) / 2, top = float(h - 260) / 2;
                color(.10f, .14f, .19f);
                rect(left, top, 580, 260);
                color(.16f, .83f, .72f);
                text(left + 28, top + 28, FERGO_APP_NAME, 2);
                color(.85f, .90f, .94f);
                text(left + 28, top + 66, "VERSION " FERGO_APP_VERSION, 2);
                text(left + 28, top + 100, "DEVELOPED BY FERGO - 2026", 2);
                char tech[100];
                int version = SDL_GetVersion();
                std::snprintf(tech, sizeof tech, "C++17 / SDL %d.%d.%d / OPENGL 3.3", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                              SDL_VERSIONNUM_MICRO(version));
                text(left + 28, top + 140, tech, 2);
                color(.55f, .65f, .73f);
                text(left + 28, top + 204, "CLICK OR PRESS ESC TO CLOSE", 2);
            }

            if (colors.open)
                colors.draw(w, h);
            if (smoke && !loading && !pending && !future.valid() && running) {
                if (selftest && model.moves.empty())
                    throw std::runtime_error("Selftest requires a nonempty file");
                if (selftest && testStage == 0) {
                    testDistance = camera.distance;
                    testYaw = camera.yaw;
                    testTarget = camera.target;
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.y = 1;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.state = SDL_BUTTON_RMASK;
                    e.motion.x = 120;
                    e.motion.y = 140;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.state = SDL_BUTTON_LMASK;
                    e.motion.x = 150;
                    e.motion.y = 160;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = float(w) / 2;
                    e.button.y = float(h) - 50;
                    SDL_PushEvent(&e);
                    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_T;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 1) {
                    if (std::string(SDL_GetWindowTitle(window)) != std::string(FERGO_APP_NAME) + " | " + model.name)
                        throw std::runtime_error("Window title selftest failed");
                    std::printf("INPUT zoom=%d orbit=%d pan=%d visible=%zu travel=%d\n", camera.distance < testDistance, camera.yaw != testYaw,
                                dot(camera.target - testTarget, camera.target - testTarget) != 0, visible, travel);
                    if (!(camera.distance < testDistance) || camera.yaw == testYaw || dot(camera.target - testTarget, camera.target - testTarget) == 0 ||
                        visible == 0 || visible == model.moves.size() || travel)
                        throw std::runtime_error("Mouse / slider / travel selftest failed");
                    screenshot(pw, ph, "dist/selftest-half.bmp");
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = float(w - 66);
                    e.button.y = 30;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 2) {
                    if (!about)
                        throw std::runtime_error("About button selftest failed");
                    screenshot(pw, ph, "dist/selftest-about.bmp");
                    SDL_Event e{};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_ESCAPE;
                    SDL_PushEvent(&e);
                    testStepExpected = visible > 2 ? visible - 2 : 0;
                    e = {};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_LEFT;
                    e.key.repeat = true;
                    SDL_PushEvent(&e);
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 3) {
                    if (visible != testStepExpected || playing)
                        throw std::runtime_error("Repeated arrow selftest failed");
                    SDL_Event e{};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_SPACE;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = 210;
                    e.button.y = float(h - 126);
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 4) {
                    if (std::abs(playbackSpeed - .1) > 1e-6 || !speedDragging || !playing)
                        throw std::runtime_error("Playback speed slider selftest failed");
                    screenshot(pw, ph, "dist/selftest-playback.bmp");
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.state = SDL_BUTTON_LMASK;
                    e.motion.x = float(w - 60);
                    e.motion.y = float(h - 126);
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button = SDL_BUTTON_LEFT;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 5) {
                    if (std::abs(playbackSpeed - 10) > 1e-6 || speedDragging)
                        throw std::runtime_error("Playback speed drag selftest failed");
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = float(w - 178);
                    e.button.y = 30;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 6) {
                    if (!colors.open || playing)
                        throw std::runtime_error("Colors menu selftest failed");
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = ColorsPanel::left(w) + 100;
                    e.button.y = ColorsPanel::top(h) + 63 + Colors::Line * 24;
                    SDL_PushEvent(&e);
                    e.button.x = ColorsPanel::left(w) + 510;
                    e.button.y = ColorsPanel::top(h) + 126;
                    SDL_PushEvent(&e);
                    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 7) {
                    Colors saved;
                    if (colors.palette[Colors::Line].r != 255 || !saved.load(colors.path) || saved[Colors::Line].r != 255)
                        throw std::runtime_error("Color preview/save selftest failed");
                    screenshot(pw, ph, "dist/selftest-colors.bmp");
                    colors.palette[Colors::Line].r = 0;
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = ColorsPanel::left(w) + 200;
                    e.button.y = ColorsPanel::top(h) + 292;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 8) {
                    if (colors.palette[Colors::Line].r != 255)
                        throw std::runtime_error("Color reload selftest failed");
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.x = ColorsPanel::left(w) + 350;
                    e.button.y = ColorsPanel::top(h) + 292;
                    SDL_PushEvent(&e);
                    ++testStage;
                } else if (selftest && testStage == 9) {
                    if (colors.palette[Colors::Line].hex() != Colors{}[Colors::Line].hex())
                        throw std::runtime_error("Color defaults selftest failed");
                    SDL_Event e{};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_ESCAPE;
                    SDL_PushEvent(&e);
                    e = {};
                    e.type = SDL_EVENT_DROP_FILE;
                    e.drop.data = testPath.c_str();
                    SDL_PushEvent(&e);
                    ++testStage;
                } else {
                    if (selftest && colors.open)
                        throw std::runtime_error("Color menu close selftest failed");
                    if (selftest && about)
                        throw std::runtime_error("About close selftest failed");
                    if (selftest && visible != model.moves.size())
                        throw std::runtime_error("Drop reload selftest failed");
                    screenshot(pw, ph, "dist/smoke.bmp");
                    running = false;
                    std::printf("%s moves=%zu parse_seconds=%.6f GL=%s\n", selftest ? "SELFTEST OK" : "SMOKE", model.moves.size(), model.seconds,
                                reinterpret_cast<const char *>(glGetString(GL_VERSION)));
                }
            }

            SDL_GL_SwapWindow(window);
            dirty = false;
        }

        if (running)
            hasWaited = SDL_WaitEventTimeout(&waited, (playing || loading) ? 8 : 50);
    }

    cancel = true;
    if (colors.changed)
        colors.save();
    if (future.valid())
        future.wait();
    lineShader.destroy();
    deleteBuffers(1, &buffer);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        bool automation = false;
        for (int i = 1; i < argc; ++i)
            automation |= std::string(argv[i]) == "--smoke" || std::string(argv[i]) == "--selftest";
        if (!automation)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, FERGO_APP_NAME, e.what(), nullptr);
        SDL_Quit();
        return 1;
    }
}
