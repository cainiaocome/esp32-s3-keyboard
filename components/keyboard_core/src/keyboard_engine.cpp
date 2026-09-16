#include "keyboard_engine.hpp"

#include <algorithm>

#include "key_map.hpp"

namespace remote_hid {

KeyboardEngine::KeyboardEngine(HidBackend& backend, Clock& clock, KeyboardEngineConfig config)
    : backend_(backend), clock_(clock), config_(config), last_activity_ms_(clock.now_ms()) {
    if (config_.max_press_duration_ms == 0 || config_.max_press_duration_ms > 1000) {
        config_.max_press_duration_ms = 1000;
    }
    if (config_.default_press_duration_ms == 0 ||
        config_.default_press_duration_ms > config_.max_press_duration_ms) {
        config_.default_press_duration_ms = std::min<uint64_t>(50, config_.max_press_duration_ms);
    }
    if (config_.max_hold_time_ms == 0) {
        config_.max_hold_time_ms = 10000;
    }
}

bool KeyboardEngine::has_key_locked(KeyCode key) const {
    return std::any_of(report_.keys.begin(), report_.keys.end(),
                       [key](uint8_t usage) { return usage == key_usage(key); }) ||
           (is_modifier(key) && (report_.modifiers & modifier_bit(key)) != 0);
}

bool KeyboardEngine::add_key_locked(KeyCode key) {
    if (is_modifier(key)) {
        report_.modifiers = static_cast<uint8_t>(report_.modifiers | modifier_bit(key));
        return true;
    }
    for (uint8_t& usage : report_.keys) {
        if (usage == 0) {
            usage = key_usage(key);
            return true;
        }
    }
    return false;
}

void KeyboardEngine::remove_key_locked(KeyCode key) {
    if (is_modifier(key)) {
        report_.modifiers = static_cast<uint8_t>(report_.modifiers & ~modifier_bit(key));
        return;
    }
    for (uint8_t& usage : report_.keys) {
        if (usage == key_usage(key)) {
            usage = 0;
            break;
        }
    }
    // Keep reports compact and deterministic after a release.
    std::stable_partition(report_.keys.begin(), report_.keys.end(),
                          [](uint8_t usage) { return usage != 0; });
}

EngineResult KeyboardEngine::send_current_report_locked() {
    if (backend_.send_report(report_)) {
        report_dirty_ = false;
        return EngineResult::kOk;
    }
    report_dirty_ = true;
    return EngineResult::kBackendFailure;
}

void KeyboardEngine::refresh_activity_locked(uint64_t now_ms) { last_activity_ms_ = now_ms; }

bool KeyboardEngine::duration_valid(uint64_t duration_ms) const {
    return duration_ms > 0 && duration_ms <= config_.max_press_duration_ms;
}

EngineResult KeyboardEngine::set_key_locked(KeyCode key, bool pressed) {
    if (!is_valid_key(key)) {
        return EngineResult::kInvalidKey;
    }

    const bool was_pressed = has_key_locked(key);
    if (was_pressed == pressed) {
        refresh_activity_locked(clock_.now_ms());
        return EngineResult::kOk;
    }

    if (pressed) {
        if (!add_key_locked(key)) {
            return EngineResult::kRollover;
        }
    }
    if (!pressed) {
        remove_key_locked(key);
    }
    refresh_activity_locked(clock_.now_ms());
    return send_current_report_locked();
}

EngineResult KeyboardEngine::key_down(KeyCode key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_pending_release_locked(key);
    return set_key_locked(key, true);
}

EngineResult KeyboardEngine::key_up(KeyCode key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_pending_release_locked(key);
    return set_key_locked(key, false);
}

bool KeyboardEngine::schedule_release_locked(KeyCode key, uint64_t due_ms) {
    for (PendingRelease& pending : pending_releases_) {
        if (!pending.active) {
            pending = PendingRelease{key, due_ms, true};
            return true;
        }
    }
    return false;
}

void KeyboardEngine::cancel_pending_release_locked(KeyCode key) {
    for (PendingRelease& pending : pending_releases_) {
        if (pending.active && pending.key == key) {
            pending.active = false;
        }
    }
}

void KeyboardEngine::clear_pending_releases_locked() {
    for (PendingRelease& pending : pending_releases_) {
        pending.active = false;
    }
}

EngineResult KeyboardEngine::key_press(KeyCode key, uint64_t duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_valid_key(key)) {
        return EngineResult::kInvalidKey;
    }
    if (duration_ms == 0) {
        duration_ms = config_.default_press_duration_ms;
    }
    if (!duration_valid(duration_ms)) {
        return EngineResult::kInvalidArgument;
    }

    // Never let a press command release a key that another client is holding.
    if (has_key_locked(key)) {
        refresh_activity_locked(clock_.now_ms());
        return EngineResult::kOk;
    }

    const EngineResult down_result = set_key_locked(key, true);
    if (down_result != EngineResult::kOk && down_result != EngineResult::kBackendFailure) {
        return down_result;
    }
    if (!schedule_release_locked(key, clock_.now_ms() + duration_ms)) {
        remove_key_locked(key);
        send_current_report_locked();
        return EngineResult::kQueueFull;
    }
    return down_result;
}

EngineResult KeyboardEngine::combo(const KeyCode* keys, std::size_t count, uint64_t duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keys == nullptr || count == 0 || count > kHidKeySlots) {
        return EngineResult::kInvalidArgument;
    }
    if (duration_ms == 0) {
        duration_ms = config_.default_press_duration_ms;
    }
    if (!duration_valid(duration_ms)) {
        return EngineResult::kInvalidArgument;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!is_valid_key(keys[i])) {
            return EngineResult::kInvalidKey;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (keys[i] == keys[j]) {
                return EngineResult::kInvalidArgument;
            }
        }
    }

    std::array<KeyCode, kHidKeySlots> newly_pressed{};
    std::size_t newly_pressed_count = 0;
    bool backend_failure = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (has_key_locked(keys[i])) {
            continue;
        }
        const EngineResult result = set_key_locked(keys[i], true);
        if (result != EngineResult::kOk && result != EngineResult::kBackendFailure) {
            for (std::size_t j = 0; j < newly_pressed_count; ++j) {
                cancel_pending_release_locked(newly_pressed[j]);
                remove_key_locked(newly_pressed[j]);
            }
            send_current_report_locked();
            return result;
        }
        backend_failure = backend_failure || result == EngineResult::kBackendFailure;
        newly_pressed[newly_pressed_count++] = keys[i];
    }

    if (newly_pressed_count == 0) {
        refresh_activity_locked(clock_.now_ms());
        return EngineResult::kOk;
    }

    const uint64_t due_ms = clock_.now_ms() + duration_ms;
    for (std::size_t i = 0; i < newly_pressed_count; ++i) {
        if (!schedule_release_locked(newly_pressed[i], due_ms)) {
            for (std::size_t j = 0; j < newly_pressed_count; ++j) {
                cancel_pending_release_locked(newly_pressed[j]);
                remove_key_locked(newly_pressed[j]);
            }
            send_current_report_locked();
            return EngineResult::kQueueFull;
        }
    }
    refresh_activity_locked(clock_.now_ms());
    return backend_failure ? EngineResult::kBackendFailure : EngineResult::kOk;
}

EngineResult KeyboardEngine::release_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    report_ = HidReport{};
    clear_pending_releases_locked();
    refresh_activity_locked(clock_.now_ms());
    // Deliberately emit even for an already-empty state: callers use this as
    // a recovery barrier after disconnects and HID reinitialization.
    return send_current_report_locked();
}

void KeyboardEngine::tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t now_ms = clock_.now_ms();
    bool changed = false;
    bool report_sent = false;
    for (PendingRelease& pending : pending_releases_) {
        if (pending.active && now_ms >= pending.due_ms) {
            if (has_key_locked(pending.key)) {
                remove_key_locked(pending.key);
                changed = true;
            }
            pending.active = false;
        }
    }
    if (changed) {
        refresh_activity_locked(now_ms);
        send_current_report_locked();
        report_sent = true;
    }
    const bool any_key_pressed =
        report_.modifiers != 0 || std::any_of(report_.keys.begin(), report_.keys.end(),
                                              [](uint8_t usage) { return usage != 0; });
    if (any_key_pressed) {
        if (now_ms - last_activity_ms_ >= config_.max_hold_time_ms) {
            report_ = HidReport{};
            clear_pending_releases_locked();
            refresh_activity_locked(now_ms);
            send_current_report_locked();
            report_sent = true;
        }
    }
    if (report_dirty_ && !report_sent) {
        send_current_report_locked();
    }
}

HidReport KeyboardEngine::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return report_;
}

std::size_t KeyboardEngine::pressed_keys(KeyCode* output, std::size_t capacity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        if ((report_.modifiers & (1U << bit)) != 0) {
            if (count < capacity && output != nullptr) {
                output[count] = static_cast<KeyCode>(key_usage(KeyCode::LEFT_CTRL) + bit);
            }
            ++count;
        }
    }
    for (uint8_t usage : report_.keys) {
        if (usage != 0 && count < capacity && output != nullptr) {
            output[count] = static_cast<KeyCode>(usage);
        }
        if (usage != 0) {
            ++count;
        }
    }
    return count;
}

bool KeyboardEngine::is_pressed(KeyCode key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_key_locked(key);
}

uint64_t KeyboardEngine::last_activity_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_activity_ms_;
}

} // namespace remote_hid
