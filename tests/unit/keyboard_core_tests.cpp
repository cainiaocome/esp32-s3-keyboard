#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "clock.hpp"
#include "hid_backend.hpp"
#include "key_map.hpp"
#include "keyboard_engine.hpp"

using namespace remote_hid;

namespace {

class FakeClock final : public Clock {
  public:
    uint64_t now_ms() const override { return now_; }
    void advance_ms(uint64_t amount) { now_ += amount; }

  private:
    uint64_t now_ = 0;
};

class FakeHidBackend final : public HidBackend {
  public:
    bool send_report(const HidReport& report) override {
        reports.push_back(report);
        return !fail;
    }

    bool fail = false;
    std::vector<HidReport> reports;
};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

HidReport expected(uint8_t modifiers, std::initializer_list<uint8_t> keys) {
    HidReport report;
    report.modifiers = modifiers;
    std::size_t index = 0;
    for (uint8_t key : keys) {
        report.keys[index++] = key;
    }
    return report;
}

void test_key_map() {
    KeyCode key{};
    expect(key_from_name("a", key) && key == KeyCode::A, "maps letters case-insensitively");
    expect(key_from_name("7", key) && key == KeyCode::DIGIT_7, "maps digits");
    expect(key_from_name("page_down", key) && key == KeyCode::PAGE_DOWN, "maps navigation keys");
    expect(key_from_name("f12", key) && key == KeyCode::F12, "maps function keys");
    expect(key_from_name("ctrl", key) && key == KeyCode::LEFT_CTRL, "maps modifier aliases");
    expect(key_name(KeyCode::RIGHT_GUI) != nullptr, "names right GUI");
    expect(!key_from_name("not-a-key", key), "rejects unknown key");
}

void test_down_up_and_idempotence() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngine engine(backend, clock);

    expect(engine.key_down(KeyCode::A) == EngineResult::kOk, "down succeeds");
    expect(backend.reports.back() == expected(0, {key_usage(KeyCode::A)}), "down emits A");
    const std::size_t report_count = backend.reports.size();
    expect(engine.key_down(KeyCode::A) == EngineResult::kOk, "repeated down succeeds");
    expect(backend.reports.size() == report_count, "repeated down is a no-op");
    expect(engine.key_up(KeyCode::A) == EngineResult::kOk, "up succeeds");
    expect(backend.reports.back() == HidReport{}, "up emits empty report");
    const std::size_t after_up = backend.reports.size();
    expect(engine.key_up(KeyCode::A) == EngineResult::kOk, "repeated up succeeds");
    expect(backend.reports.size() == after_up, "repeated up is a no-op");
}

void test_press_is_non_blocking_and_completes() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngineConfig config;
    config.max_hold_time_ms = 10000;
    config.default_press_duration_ms = 50;
    config.max_press_duration_ms = 1000;
    KeyboardEngine engine(backend, clock, config);

    expect(engine.key_press(KeyCode::ENTER) == EngineResult::kOk, "press succeeds");
    expect(backend.reports.size() == 1, "press sends down immediately");
    expect(backend.reports[0] == expected(0, {key_usage(KeyCode::ENTER)}), "press down report");
    clock.advance_ms(49);
    engine.tick();
    expect(backend.reports.size() == 1, "press remains held before duration");
    clock.advance_ms(1);
    engine.tick();
    expect(backend.reports.back() == HidReport{}, "press sends release at due time");

    expect(engine.key_press(KeyCode::A, 50) == EngineResult::kOk, "second press succeeds");
    expect(engine.key_down(KeyCode::A) == EngineResult::kOk,
           "explicit hold can take over a scheduled press");
    clock.advance_ms(50);
    engine.tick();
    expect(engine.is_pressed(KeyCode::A), "explicit hold is not released by old press timer");
    expect(engine.key_up(KeyCode::A) == EngineResult::kOk, "explicit hold releases normally");
}

void test_modifiers_and_ctrl_c() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngine engine(backend, clock);

    expect(engine.key_down(KeyCode::LEFT_CTRL) == EngineResult::kOk, "ctrl down");
    expect(engine.key_down(KeyCode::LEFT_SHIFT) == EngineResult::kOk, "shift down");
    expect(backend.reports.back().modifiers ==
               (modifier_bit(KeyCode::LEFT_CTRL) | modifier_bit(KeyCode::LEFT_SHIFT)),
           "multiple modifiers are combined");
    expect(engine.key_down(KeyCode::C) == EngineResult::kOk, "C down");
    expect(backend.reports.back() ==
               expected(modifier_bit(KeyCode::LEFT_CTRL) | modifier_bit(KeyCode::LEFT_SHIFT),
                        {key_usage(KeyCode::C)}),
           "Ctrl+Shift+C report");
    expect(engine.key_up(KeyCode::C) == EngineResult::kOk, "C up");
    expect(engine.key_up(KeyCode::LEFT_SHIFT) == EngineResult::kOk, "shift up");
    expect(engine.key_up(KeyCode::LEFT_CTRL) == EngineResult::kOk, "ctrl up");
    expect(backend.reports.back() == HidReport{}, "Ctrl+C sequence ends empty");
}

void test_combo_and_release_all() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngine engine(backend, clock);
    const std::array<KeyCode, 2> combo = {KeyCode::LEFT_CTRL, KeyCode::C};

    expect(engine.combo(combo.data(), combo.size()) == EngineResult::kOk, "combo succeeds");
    expect(backend.reports.back() ==
               expected(modifier_bit(KeyCode::LEFT_CTRL), {key_usage(KeyCode::C)}),
           "combo presses keys in order");
    clock.advance_ms(50);
    engine.tick();
    expect(backend.reports.back() == HidReport{}, "combo releases all members");
    expect(engine.release_all() == EngineResult::kOk, "release-all succeeds when empty");
    expect(backend.reports.back() == HidReport{}, "release-all emits empty barrier report");
}

void test_timeout_and_invalid_input() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngineConfig config;
    config.max_hold_time_ms = 100;
    config.default_press_duration_ms = 50;
    config.max_press_duration_ms = 1000;
    KeyboardEngine engine(backend, clock, config);
    expect(engine.key_down(KeyCode::A) == EngineResult::kOk, "hold key");
    const HidReport before_invalid = engine.report();
    expect(engine.key_down(static_cast<KeyCode>(0xFF)) == EngineResult::kInvalidKey,
           "invalid key rejected");
    expect(engine.report() == before_invalid, "invalid key does not change state");
    clock.advance_ms(99);
    engine.tick();
    expect(engine.is_pressed(KeyCode::A), "key remains before timeout");
    clock.advance_ms(1);
    engine.tick();
    expect(!engine.is_pressed(KeyCode::A), "timeout releases key");
    expect(backend.reports.back() == HidReport{}, "timeout emits empty report");
}

void test_rollover_and_backend_failure() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngine engine(backend, clock);
    const std::array<KeyCode, 7> keys = {KeyCode::A, KeyCode::B, KeyCode::C, KeyCode::D,
                                         KeyCode::E, KeyCode::F, KeyCode::G};
    for (std::size_t i = 0; i < 6; ++i) {
        expect(engine.key_down(keys[i]) == EngineResult::kOk, "six-key rollover accepts six keys");
    }
    const HidReport before_seventh = engine.report();
    expect(engine.key_down(keys[6]) == EngineResult::kRollover, "seventh key rejected");
    expect(engine.report() == before_seventh, "rollover rejection preserves report");

    expect(engine.release_all() == EngineResult::kOk, "clear before backend test");
    backend.fail = true;
    expect(engine.key_down(KeyCode::Z) == EngineResult::kBackendFailure,
           "backend failure is returned");
    expect(engine.is_pressed(KeyCode::Z), "logical state remains authoritative after send failure");
    backend.fail = false;
    expect(engine.key_up(KeyCode::Z) == EngineResult::kOk,
           "recovery can release failed report state");
}

void test_status_order_and_duration_validation() {
    FakeClock clock;
    FakeHidBackend backend;
    KeyboardEngine engine(backend, clock);
    expect(engine.key_down(KeyCode::A) == EngineResult::kOk, "status normal key");
    expect(engine.key_down(KeyCode::LEFT_ALT) == EngineResult::kOk, "status modifier");
    std::array<KeyCode, 8> pressed{};
    const std::size_t count = engine.pressed_keys(pressed.data(), pressed.size());
    expect(count == 2, "status count");
    expect(pressed[0] == KeyCode::LEFT_ALT && pressed[1] == KeyCode::A,
           "status exposes deterministic key order");
    expect(engine.key_press(KeyCode::B, 1001) == EngineResult::kInvalidArgument,
           "long press rejected");
}

} // namespace

int main() {
    test_key_map();
    test_down_up_and_idempotence();
    test_press_is_non_blocking_and_completes();
    test_modifiers_and_ctrl_c();
    test_combo_and_release_all();
    test_timeout_and_invalid_input();
    test_rollover_and_backend_failure();
    test_status_order_and_duration_validation();
    std::cout << "keyboard_core_tests: all tests passed\n";
    return 0;
}
