#include "ScreenRecorder.hpp"
#include "IR/Machine.hpp"
#include "backends/VM.hpp"

#include <stb/Image.hpp>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <chrono>

#ifdef __linux__
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
constexpr int tile_width = 800;
constexpr int tile_height = 500;

stb::Image<stb::RGB> fit_to_tile(const stb::Image<stb::RGB>& source) {
    if (source.w <= 0 || source.h <= 0) throw std::runtime_error("Invalid screenshot size");
    double scale = std::min(double(tile_width) / source.w, double(tile_height) / source.h);
    int w = std::max(1, int(source.w * scale));
    int h = std::max(1, int(source.h * scale));
    return source.resize(w, h);
}
}

struct ScreenRecorder::Impl {
    std::vector<std::shared_ptr<IR::Machine>> machines;
    std::map<std::string, stb::Image<stb::RGB>> last_frames;
    int fps = 4;
    int width = tile_width;
    int height = tile_height;
    std::chrono::steady_clock::time_point started_at;
    uint64_t frames_written = 0;
#ifdef __linux__
    int pipe_fd = -1;
    pid_t ffmpeg_pid = -1;
    struct sigaction old_sigpipe {};
    bool sigpipe_changed = false;
#endif
};

#ifdef __linux__
static fs::path find_ffmpeg() {
    const char* path_env = std::getenv("PATH");
    if (!path_env) throw std::runtime_error("PATH is not set while looking for ffmpeg");
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path candidate = fs::path(dir.empty() ? "." : dir) / "ffmpeg";
        if (::access(candidate.generic_string().c_str(), X_OK) == 0) return candidate;
    }
    throw std::runtime_error("--record-tests requires the ffmpeg executable in PATH");
}

static void write_all(int fd, const uint8_t* data, size_t size) {
    while (size) {
        ssize_t written = ::write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("Writing video frame to ffmpeg failed: ") + std::strerror(errno));
        }
        data += written;
        size -= size_t(written);
    }
}
#endif

ScreenRecorder::ScreenRecorder(const std::vector<std::shared_ptr<IR::Machine>>& machines,
                               const fs::path& destination, int fps_)
    : impl(new Impl), output_path(destination)
{
    impl->machines = machines;
    std::sort(impl->machines.begin(), impl->machines.end(), [](const auto& a, const auto& b) {
        return a->name() < b->name();
    });
    impl->machines.erase(std::unique(impl->machines.begin(), impl->machines.end()), impl->machines.end());
    impl->fps = std::max(1, fps_);
    if (impl->machines.empty() || output_path.empty()) return;
    impl->started_at = std::chrono::steady_clock::now();

    const int columns = std::min<int>(2, impl->machines.size());
    const int rows = (int(impl->machines.size()) + columns - 1) / columns;
    impl->width = columns * tile_width;
    impl->height = rows * tile_height;
    fs::create_directories(output_path.parent_path());

#ifndef __linux__
    throw std::runtime_error("--record-tests is currently implemented only on Linux");
#else
    auto ffmpeg = find_ffmpeg();
    int fds[2];
    if (::pipe(fds) != 0) throw std::runtime_error("Can't create ffmpeg pipe");

    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, &ignore, &impl->old_sigpipe) == 0) impl->sigpipe_changed = true;

    impl->ffmpeg_pid = ::fork();
    if (impl->ffmpeg_pid < 0) {
        ::close(fds[0]); ::close(fds[1]);
        if (impl->sigpipe_changed) {
            sigaction(SIGPIPE, &impl->old_sigpipe, nullptr);
            impl->sigpipe_changed = false;
        }
        throw std::runtime_error("Can't fork ffmpeg process");
    }
    if (impl->ffmpeg_pid == 0) {
        ::dup2(fds[0], STDIN_FILENO);
        ::close(fds[0]); ::close(fds[1]);
        std::string size = std::to_string(impl->width) + "x" + std::to_string(impl->height);
        std::string rate = std::to_string(impl->fps);
        execl(ffmpeg.generic_string().c_str(), "ffmpeg",
              "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
              "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", size.c_str(),
              "-framerate", rate.c_str(), "-i", "pipe:0", "-an",
              "-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "6",
              "-b:v", "0", "-crf", "35", "-pix_fmt", "yuv420p",
              output_path.generic_string().c_str(), (char*)nullptr);
        _exit(127);
    }
    ::close(fds[0]);
    impl->pipe_fd = fds[1];
    started = true;
#endif
}

void ScreenRecorder::capture_frame() {
    if (!started) return;
    stb::Image<stb::RGB> mosaic(impl->width, impl->height, stb::RGB::black());
    const int columns = std::min<int>(2, impl->machines.size());

    for (size_t index = 0; index < impl->machines.size(); ++index) {
        auto& machine = impl->machines[index];
        const auto name = machine->name();
        try {
            if (machine->is_defined() && machine->vm()->state() == VmState::Running) {
                impl->last_frames[name] = machine->vm()->screenshot();
            }
        } catch (...) {
            // A VM may be transitioning between states. Reuse its last good frame.
        }
        auto found = impl->last_frames.find(name);
        if (found == impl->last_frames.end()) continue;

        auto tile = fit_to_tile(found->second);
        int cell_x = int(index % columns) * tile_width;
        int cell_y = int(index / columns) * tile_height;
        int off_x = cell_x + (tile_width - tile.w) / 2;
        int off_y = cell_y + (tile_height - tile.h) / 2;
        for (int y = 0; y < tile.h; ++y)
            for (int x = 0; x < tile.w; ++x)
                mosaic.at(off_x + x, off_y + y) = tile.at(x, y);
    }

#ifdef __linux__
    auto elapsed = std::chrono::steady_clock::now() - impl->started_at;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    uint64_t target_frames = std::max<uint64_t>(1, (uint64_t(elapsed_ms) * impl->fps) / 1000 + 1);
    while (impl->frames_written < target_frames) {
        write_all(impl->pipe_fd, mosaic.data, mosaic.data_len());
        ++impl->frames_written;
    }
#endif
}

void ScreenRecorder::finish() {
    if (!started) return;
#ifdef __linux__
    std::exception_ptr capture_error;
    try {
        // Preserve wall-clock duration even when libvirt operations temporarily block
        // the cooperative capture coroutine. The last good VM images are reused.
        capture_frame();
    } catch (...) {
        capture_error = std::current_exception();
    }

    if (impl->pipe_fd >= 0) {
        ::close(impl->pipe_fd);
        impl->pipe_fd = -1;
    }

    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(impl->ffmpeg_pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    impl->ffmpeg_pid = -1;

    if (impl->sigpipe_changed) {
        sigaction(SIGPIPE, &impl->old_sigpipe, nullptr);
        impl->sigpipe_changed = false;
    }
    started = false;

    if (capture_error) std::rethrow_exception(capture_error);
    if (waited < 0) {
        throw std::runtime_error(std::string("Waiting for ffmpeg failed: ") + std::strerror(errno));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("ffmpeg failed while producing " + output_path.generic_string());
    }
    if (!fs::is_regular_file(output_path) || fs::file_size(output_path) == 0) {
        throw std::runtime_error("ffmpeg did not produce a valid recording: " + output_path.generic_string());
    }
#endif
}

ScreenRecorder::~ScreenRecorder() {
    try {
        finish();
    } catch (...) {
    }
}
