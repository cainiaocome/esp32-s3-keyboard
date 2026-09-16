#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "clock.hpp"
#include "hid_backend.hpp"
#include "key_codes.hpp"

namespace remote_hid {

enum class EngineResult {
    kOk,
    kInvalidKey,
    kRollover,
    kInvalidArgument,
    kQueueFull,
    kBackendFailure,
};

struct KeyboardEngineConfig {
    uint64_t max_hold_time_ms = 10000;
    uint64_t default_press_duration_ms = 50;
    uint64_t max_press_duration_ms = 1000;
};

class KeyboardEngine {
  public:
    KeyboardEngine(HidBackend& backend, Clock& clock, KeyboardEngineConfig config = {});

    EngineResult key_down(KeyCode key);
    EngineResult key_up(KeyCode key);
    EngineResult key_press(KeyCode key, uint64_t duration_ms = 0);
    EngineResult combo(const KeyCode* keys, std::size_t count, uint64_t duration_ms = 0);
    EngineResult release_all();

    // Called periodically by the firmware task. It releases scheduled press
    // completions and held keys that exceed the inactivity safety timeout.
    void tick();

    HidReport report() const;
    std::size_t pressed_keys(KeyCode* output, std::size_t capacity) const;
    bool is_pressed(KeyCode key) const;
    uint64_t last_activity_ms() const;

  private:
    static constexpr std::size_t kMaxPendingReleases = 16;

    struct PendingRelease {
        KeyCode key{};
        uint64_t due_ms = 0;
        bool active = false;
    };

    EngineResult send_current_report_locked();
    EngineResult set_key_locked(KeyCode key, bool pressed);
    bool has_key_locked(KeyCode key) const;
    bool add_key_locked(KeyCode key);
    void remove_key_locked(KeyCode key);
    bool schedule_release_locked(KeyCode key, uint64_t due_ms);
    void cancel_pending_release_locked(KeyCode key);
    void clear_pending_releases_locked();
    void refresh_activity_locked(uint64_t now_ms);
    bool duration_valid(uint64_t duration_ms) const;

    HidBackend& backend_;
    Clock& clock_;
    KeyboardEngineConfig config_;
    HidReport report_{};
    bool report_dirty_ = false;
    uint64_t last_activity_ms_ = 0;
    std::array<PendingRelease, kMaxPendingReleases> pending_releases_{};
    mutable std::mutex mutex_;
};

} // namespace remote_hid
