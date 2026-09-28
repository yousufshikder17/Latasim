// The workbench's built-in scenarios: Keil's Blinky_ULp, the representative RTOS
// workloads (firmware/rtos/workloads.h), the USB speaker and the media center.
#include <cstdio>
#include <cstring>
#include <string>

#include "firmware/blinky_ulp_host.hpp"
#include "lpc17xx/nvic.hpp"
#include "media_center.h"
#include "usb_speaker.h"
#include "workbench/session.hpp"
#include "workloads.h"

namespace latasim::workbench {
namespace {

constexpr std::uint64_t kMs = lpc17xx::kCyclesPerMillisecond;
constexpr std::uint32_t at_ms(std::uint32_t ms) { return ms * 100'000u; }  // osKernelSysTick counts cycles

Check expect(bool ok, const std::string& detail) { return {ok, detail}; }

std::string list(const volatile std::uint32_t* v, unsigned n, std::uint32_t scale = 1) {
    std::string s;
    for (unsigned i = 0; i < n; ++i) s += (i ? ", " : "") + std::to_string(v[i] / scale);
    return s;
}

Scenario rtos_scenario(std::string name, std::string description, void (*main)(), std::function<void()> reset,
                       std::uint64_t check_after, std::function<Check(const Session&)> check,
                       rtos::Config config = {}) {
    Scenario s;
    s.name = std::move(name);
    s.description = std::move(description);
    s.rtos = true;
    s.config = config;
    s.rtos_main = main;
    s.reset_statics = std::move(reset);
    s.check_after = check_after;
    s.check = std::move(check);
    return s;
}

Scenario inversion(int mode, const char* name, const char* description, std::uint32_t expected_ms) {
    return rtos_scenario(
        name, description, inv_main, [mode] { inv_reset(mode); }, 300 * kMs, [expected_ms](const Session&) {
            return expect(inv_high_got_resource_at == at_ms(expected_ms),
                          "High got the resource at " + std::to_string(inv_high_got_resource_at / 100'000) +
                              " ms (expected " + std::to_string(expected_ms) + ")");
        });
}

std::string speaker_status(const Session&) {
    return "Speaker firmware: " + std::string(usb_speaker.streaming ? "streaming" : usb_speaker.configured ? "configured" : usb_speaker.connected ? "connected" : "off") +
           ", " + (usb_speaker.playing ? "playing" : "not playing") + ", buffer " +
           std::to_string(usb_speaker.buffer_level) + "/" + std::to_string(USB_SPEAKER_BUFFER) + ", volume " +
           std::to_string(usb_speaker.volume) + "/256, underruns " + std::to_string(usb_speaker.underruns) +
           ", overruns " + std::to_string(usb_speaker.overruns) + ", received " +
           std::to_string(usb_speaker.samples_received) + ", played " + std::to_string(usb_speaker.samples_played);
}

void bind_usb_speaker(Session& s) {
    auto& mcu = s.board().mcu();
    mcu.bind_handler(lpc17xx::kUsbIrq, USB_IRQHandler);
    mcu.bind_handler(lpc17xx::kTimer0Irq, usb_speaker_timer_irq);
}

std::vector<Scenario> make() {
    std::vector<Scenario> all;

    Scenario blinky;
    blinky.name = "Blinky_ULp";
    blinky.description = "Keil's Blinky_ULp, bare metal: a SysTick LED chase whose speed follows the potentiometer.";
    blinky.reset_statics = [] { firmware::reset_blinky_statics(); };
    blinky.start = [](Session& s) {
        blinky_ulp_start();
        firmware::bind_blinky(s.board().mcu());
    };
    blinky.check_after = 100 * kMs;
    blinky.check = [](const Session& s) {
        unsigned on = 0;
        for (unsigned i = 0; i < mcb1700::kLedCount; ++i) on += s.board().led(i) == mcb1700::LedState::On;
        return expect(on == 1, std::to_string(on) + " LED(s) lit; the chase lights exactly one");
    };
    all.push_back(blinky);

    rtos::Config rr;
    rr.tick_period = 500'000;
    rr.tick_us = 5'000;
    rr.round_robin_ticks = 3;
    all.push_back(rtos_scenario(
        "RTOS: round-robin", "Three equal threads with 40, 30 and 20 ms of work in 15 ms time slices.", rr_main, rr_reset,
        100 * kMs,
        [](const Session&) {
            return expect(rr_steps[0] == 40 && rr_steps[1] == 30 && rr_steps[2] == 20 && rr_done_at[2] == at_ms(80) &&
                              rr_done_at[0] == at_ms(90),
                          "done at " + list(rr_done_at, RR_THREADS, 100'000) + " ms (expected 90, 90, 80)");
        },
        rr));
    all.push_back(rtos_scenario(
        "RTOS: preemption", "Five finite computations at three priorities; higher ones finish first.", pre_main,
        pre_reset, 200 * kMs, [](const Session&) {
            const bool ok = pre_finished == 5 && pre_finish_order[0] == 2 && pre_finish_order[1] == 0 &&
                            pre_finish_order[2] == 3 && pre_finish_order[3] == 1 && pre_finish_order[4] == 4;
            return expect(ok, "finish order " + list(pre_finish_order, pre_finished) + " (expected 2, 0, 3, 1, 4)");
        }));
    rtos::Config yield;
    yield.round_robin = false;
    yield.call_cycles = 250;
    all.push_back(rtos_scenario(
        "RTOS: yield", "Two equal threads counting and handing over with osThreadYield (250-cycle kernel calls).",
        yield_main, yield_reset, 1 * kMs,
        [](const Session&) {
            const std::string order(const_cast<const char*>(yield_order));
            return expect(order.rfind("ABABABAB", 0) == 0, "order " + order.substr(0, 16));
        },
        yield));
    all.push_back(rtos_scenario(
        "RTOS: delays", "osDelay(10) and osDelay(20) set two threads' rates.", delay_main, delay_reset, 1000 * kMs,
        [](const Session& s) {
            const std::uint64_t ms = s.now() / kMs;
            return expect(delay_count[0] == ms / 10 + 1 && delay_count[1] == ms / 20 + 1,
                          "counts " + list(delay_count, 2) + " after " + std::to_string(ms) + " ms");
        }));
    all.push_back(rtos_scenario(
        "RTOS: signals and mutex", "Five cooperating threads: signals, a mutex-protected log, finite lifetimes.",
        svc_main, svc_reset, 100 * kMs, [](const Session&) {
            const std::string order(const_cast<const char*>(svc_order));
            return expect(order == "CMADU" && std::string(svc_log) == "app: start of message, end",
                          "finish order " + order + ", log \"" + svc_log + "\"");
        }));
    all.push_back(rtos_scenario(
        "RTOS: virtual timers", "Timers of 50, 80 and 120 ms signal a thread that toggles LEDs 0-2.", vt_main, vt_reset,
        1000 * kMs, [](const Session& s) {
            const std::uint64_t ms = s.now() / kMs;
            return expect(vt_fired[0] == ms / 50 && vt_fired[1] == ms / 80 && vt_fired[2] == ms / 120,
                          "fired " + list(vt_fired, 3) + " times after " + std::to_string(ms) + " ms");
        }));
    all.push_back(rtos_scenario(
        "RTOS: rate-monotonic", "C (200, 50), B (400, 100), A (400, 150) ms, priorities by rate.", rms_main, rms_reset,
        800 * kMs, [](const Session&) {
            const bool ok = rms_complete[2][0] == at_ms(50) && rms_complete[1][0] == at_ms(150) &&
                            rms_complete[0][0] == at_ms(350) && rms_complete[0][1] == at_ms(750);
            return expect(ok, "first completions C " + std::to_string(rms_complete[2][0] / 100'000) + ", B " +
                                  std::to_string(rms_complete[1][0] / 100'000) + ", A " +
                                  std::to_string(rms_complete[0][0] / 100'000) + " ms (expected 50, 150, 350)");
        }));
    all.push_back(inversion(0, "RTOS: priority inversion",
                            "Low holds a flag-protected resource; Medium runs while High waits for it.", 190));
    all.push_back(inversion(1, "RTOS: inversion, priority elevation",
                            "The same, with High raising Low's priority while it waits.", 90));
    all.push_back(inversion(2, "RTOS: inversion, mutex inheritance",
                            "The resource is an RTX mutex: Low inherits High's priority.", 90));

    Scenario speaker;
    speaker.name = "USB speaker";
    speaker.description = "Bare-metal USB audio speaker on Keil's USB and DAC drivers; the PC plays a 440 Hz tone.";
    speaker.usb_pc = true;
    speaker.reset_statics = [] { usb_speaker_reset(); };
    speaker.start = [](Session& s) {
        bind_usb_speaker(s);
        usb_speaker_start();
    };
    speaker.teardown = [](Session&) { usb_speaker_stop(); };
    speaker.status = speaker_status;
    speaker.check_after = 200 * kMs;
    speaker.check = [](const Session&) {
        return expect(usb_speaker.streaming && usb_speaker.playing && usb_speaker.underruns == 0,
                      std::string(usb_speaker.playing ? "playing" : "not playing") + ", " +
                          std::to_string(usb_speaker.samples_played) + " samples, " +
                          std::to_string(usb_speaker.underruns) + " underruns");
    };
    all.push_back(speaker);

    Scenario mediacenter = rtos_scenario(
        "Media center", "RTOS application: GLCD menu, joystick, photos, paddle game, USB audio with potentiometer volume.",
        media_main,
        [] {
            media_reset();
            usb_speaker_reset();
        },
        50 * kMs, [](const Session&) {
            return expect(media.redraws > 0, "screen " + std::to_string(media.screen) + ", " +
                                                 std::to_string(media.redraws) + " screens drawn");
        });
    mediacenter.usb_pc = true;
    mediacenter.start = [](Session& s) {
        s.board().mcu().bind_handler(lpc17xx::kAdcIrq, ADC_IRQHandler);
        bind_usb_speaker(s);
    };
    mediacenter.teardown = [](Session&) {
        if (usb_speaker.connected) usb_speaker_stop();
    };
    mediacenter.status = [](const Session& s) {
        static const char* const screens[] = {"menu", "photos", "audio", "game"};
        return std::string("Media center: ") + screens[media.screen % 4] + ", selection " +
               std::to_string(media.selection) + ", score " + std::to_string(media.score) + "\n" + speaker_status(s);
    };
    all.push_back(mediacenter);
    return all;
}

}  // namespace

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> all = make();
    return all;
}

}  // namespace latasim::workbench
