#pragma once

#include <ghc/filesystem.hpp>
#include <memory>
#include <string>
#include <vector>

namespace IR { struct Machine; }
namespace fs = ghc::filesystem;

class ScreenRecorder {
public:
    ScreenRecorder(const std::vector<std::shared_ptr<IR::Machine>>& machines,
                   const fs::path& destination,
                   int fps = 4);
    ~ScreenRecorder();

    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    void capture_frame();
    void finish();
    bool active() const { return started; }
    const fs::path& destination() const { return output_path; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    fs::path output_path;
    bool started = false;
};
