#pragma once

#include <memory>

namespace passvault::app {

class Application {
 public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace passvault::app
