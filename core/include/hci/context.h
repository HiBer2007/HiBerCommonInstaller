#pragma once

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>

#include "hci/vars.h"

namespace hci {

// Per-step failure policy for the flow controller.
enum class StepFailure {
    Abort,     // stop the whole run (default)
    Ignore,    // log error, continue with the next step
    Continue,  // log error, continue (alias of Ignore in v1)
};

// InstallContext: everything a run carries (variables, cancellation,
// failure policy, free-form run state).
class InstallContext {
public:
    InstallContext() = default;

    Vars& vars() { return vars_; }
    const Vars& vars() const { return vars_; }

    void cancel() { cancelled_.store(true); }
    bool cancelled() const { return cancelled_.load(); }
    void resetCancelled() { cancelled_.store(false); }

    StepFailure failurePolicy() const { return failurePolicy_; }
    void setFailurePolicy(StepFailure p) { failurePolicy_ = p; }

    // Free-form state shared between extensions/steps (e.g. last result).
    nlohmann::json& state() { return state_; }
    const nlohmann::json& state() const { return state_; }

private:
    Vars vars_;
    std::atomic<bool> cancelled_{false};
    StepFailure failurePolicy_ = StepFailure::Abort;
    nlohmann::json state_;
};

} // namespace hci