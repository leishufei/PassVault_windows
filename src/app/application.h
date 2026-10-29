#pragma once

#include <memory>

#include "app/local_reset.h"

namespace passvault::sync {
class CloudStorageProvider;
}

namespace passvault::app {

class Application {
 public:
    Application();
    explicit Application(LocalResetPaths paths,
                         sync::CloudStorageProvider* provider_override = nullptr,
                         std::function<bool(const QString&)> reset_io_check = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace passvault::app
