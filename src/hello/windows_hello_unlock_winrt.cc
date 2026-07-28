#include "hello/windows_hello_unlock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.UI.h>

namespace passvault::hello {

namespace {

using winrt::Windows::Security::Credentials::UI::UserConsentVerifier;
using winrt::Windows::Security::Credentials::UI::UserConsentVerifierAvailability;
using winrt::Windows::Security::Credentials::UI::UserConsentVerificationResult;

void TryInitApartment() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (winrt::hresult_error const&) {
        // RPC_E_CHANGED_MODE if the thread already picked a different apartment.
        // Safe to ignore: subsequent WinRT calls still work in the existing one.
    }
}

}  // namespace

bool WindowsHelloUnlock::ProbeAvailability() {
    if (forced_availability_.has_value()) return *forced_availability_;
    TryInitApartment();
    try {
        auto op = UserConsentVerifier::CheckAvailabilityAsync();
        return op.get() == UserConsentVerifierAvailability::Available;
    } catch (winrt::hresult_error const&) {
        return false;
    } catch (...) {
        return false;
    }
}

bool WindowsHelloUnlock::RequestVerification() {
    if (forced_prompt_.has_value()) return *forced_prompt_;
    TryInitApartment();
    try {
        auto op = UserConsentVerifier::RequestVerificationAsync(
            winrt::hstring(L"PassVault 需要确认您的身份"));
        return op.get() == UserConsentVerificationResult::Verified;
    } catch (winrt::hresult_error const&) {
        return false;
    } catch (...) {
        return false;
    }
}

}  // namespace passvault::hello
