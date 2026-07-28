#include "hello/windows_hello_unlock.h"

namespace passvault::hello {

bool WindowsHelloUnlock::ProbeAvailability() {
    if (forced_availability_.has_value()) return *forced_availability_;
    return false;
}

bool WindowsHelloUnlock::RequestVerification() {
    if (forced_prompt_.has_value()) return *forced_prompt_;
    return false;
}

}  // namespace passvault::hello
