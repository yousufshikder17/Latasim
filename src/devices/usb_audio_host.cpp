#include "devices/usb_audio_host.hpp"

#include <algorithm>

namespace latasim::usb {
namespace {

constexpr unsigned kControlTimeoutFrames = 500;

std::uint16_t le16(const std::vector<std::uint8_t>& d, std::size_t i) {
    return static_cast<std::uint16_t>(d.at(i) | (d.at(i + 1) << 8));
}

}  // namespace

TonePcm::TonePcm(unsigned sample_rate, unsigned frequency, std::int16_t amplitude)
    : period_(std::max(2u, sample_rate / frequency)), amplitude_(amplitude) {}

std::int16_t TonePcm::next() {
    // Up from -A to +A over the first half period, back down over the second.
    const auto half = static_cast<std::int32_t>(period_ / 2);
    const auto p = static_cast<std::int32_t>(phase_ % period_);
    const std::int32_t ramp = p < half ? p : static_cast<std::int32_t>(period_) - p;
    phase_ = (phase_ + 1) % period_;
    return static_cast<std::int16_t>(-amplitude_ + 2 * amplitude_ * ramp / std::max(half, 1));
}

BufferPcm::BufferPcm(std::vector<std::int16_t> samples, bool loop) : samples_(std::move(samples)), loop_(loop) {}

std::int16_t BufferPcm::next() {
    if (samples_.empty()) return 0;
    if (pos_ >= samples_.size()) {
        if (!loop_) return 0;
        pos_ = 0;
    }
    return samples_[pos_++];
}

std::string AudioHost::state_name() const {
    switch (state_) {
    case State::Detached: return "detached";
    case State::Enumerating: return "enumerating";
    case State::Streaming: return "streaming";
    case State::Failed: return "failed: " + error_;
    }
    return "?";
}

void AudioHost::frame(Bus& bus) {
    ++frames_;
    if (!bus.pull_up()) {  // the device is not on the bus (or has left it)
        if (state_ != State::Detached) {
            state_ = State::Detached;
            queue_.clear();
            reset_done_ = false;
        }
        return;
    }
    if (state_ == State::Detached) {  // it has just connected: reset it, then enumerate
        bus.bus_reset();
        reset_done_ = true;
        start_enumeration();
        return;
    }
    if (!reset_done_) return;
    bus.start_of_frame();
    if (state_ == State::Enumerating) step_control(bus);
    else if (state_ == State::Streaming && playing_) {
        const unsigned samples = stream_.sample_rate / 1000 * stream_.channels;
        std::vector<std::uint8_t> packet;
        packet.reserve(samples * 2);
        for (unsigned i = 0; i < samples; ++i) {
            const std::int16_t s = source_ != nullptr ? source_->next() : std::int16_t{0};
            packet.push_back(static_cast<std::uint8_t>(s & 0xFF));
            packet.push_back(static_cast<std::uint8_t>((s >> 8) & 0xFF));
        }
        bus.iso_out(stream_.endpoint, packet);
        ++packets_;
        samples_ += samples;
    }
}

void AudioHost::fail(const std::string& why) {
    state_ = State::Failed;
    error_ = why;
    queue_.clear();
}

void AudioHost::control(std::uint8_t type, std::uint8_t request, std::uint16_t value, std::uint16_t index,
                        std::uint16_t length, std::function<void(const std::vector<std::uint8_t>&)> done) {
    Request r;
    r.setup = {type,
               request,
               static_cast<std::uint8_t>(value & 0xFF),
               static_cast<std::uint8_t>(value >> 8),
               static_cast<std::uint8_t>(index & 0xFF),
               static_cast<std::uint8_t>(index >> 8),
               static_cast<std::uint8_t>(length & 0xFF),
               static_cast<std::uint8_t>(length >> 8)};
    r.done = std::move(done);
    queue_.push_back(std::move(r));
}

// Standard requests (USB 2.0 chapter 9) in order; each one's result queues the next.
void AudioHost::start_enumeration() {
    state_ = State::Enumerating;
    ++enumerations_;
    queue_.clear();
    next_ = 0;
    stage_ = Stage::Setup;
    max_packet0_ = 8;
    stream_ = {};
    control(0x80, 6, 0x0100, 0, 18, [this](const std::vector<std::uint8_t>& d) {  // GET_DESCRIPTOR device
        if (d.size() < 18 || d[1] != 1) return fail("bad device descriptor");
        max_packet0_ = d[7];
        control(0x00, 5, 1, 0, 0, [this](const std::vector<std::uint8_t>&) {  // SET_ADDRESS 1
            control(0x80, 6, 0x0200, 0, 9, [this](const std::vector<std::uint8_t>& c) {  // configuration, header
                if (c.size() < 9 || c[1] != 2) return fail("bad configuration descriptor");
                configuration_ = c[5];
                control(0x80, 6, 0x0200, 0, le16(c, 2), [this](const std::vector<std::uint8_t>& all) {
                    if (!parse_configuration(all)) return fail("no audio streaming interface with an isochronous OUT endpoint");
                    control(0x00, 9, configuration_, 0, 0, [this](const std::vector<std::uint8_t>&) {  // SET_CONFIGURATION
                        control(0x01, 11, static_cast<std::uint16_t>(stream_.alternate),
                                static_cast<std::uint16_t>(stream_.interface), 0,
                                [this](const std::vector<std::uint8_t>&) { state_ = State::Streaming; });  // SET_INTERFACE
                    });
                });
            });
        });
    });
}

// Finds the first audio streaming interface (class 1, subclass 2) alternate setting
// with an isochronous OUT endpoint, and its Type I format.
bool AudioHost::parse_configuration(const std::vector<std::uint8_t>& d) {
    AudioStream candidate;
    bool in_streaming = false;
    for (std::size_t i = 0; i + 1 < d.size() && d[i] != 0; i += d[i]) {
        if (i + d[i] > d.size()) return false;
        const std::uint8_t type = d[i + 1];
        if (type == 4) {  // interface
            in_streaming = d[i + 5] == 1 && d[i + 6] == 2 && d[i + 3] != 0;
            candidate = {};
            candidate.interface = d[i + 2];
            candidate.alternate = d[i + 3];
        } else if (in_streaming && type == 0x24 && d[i + 2] == 2 && d[i] >= 11) {  // Type I format
            candidate.channels = d[i + 4];
            candidate.subframe = d[i + 5];
            candidate.sample_rate = d[i + 8] | (d[i + 9] << 8) | (d[i + 10] << 16);
        } else if (in_streaming && type == 5 && (d[i + 2] & 0x80) == 0 && (d[i + 3] & 3) == 1) {  // iso OUT
            candidate.endpoint = d[i + 2] & 0x0F;
            candidate.max_packet = le16(d, i + 4);
            if (candidate.sample_rate != 0 && candidate.channels != 0) {
                stream_ = candidate;
                return true;
            }
        }
    }
    return false;
}

// One control transaction per frame: SETUP, then IN data until a short packet or
// the requested length, then the status stage.
void AudioHost::step_control(Bus& bus) {
    if (next_ >= queue_.size()) return;
    Request& r = queue_[next_];
    const std::uint16_t length = static_cast<std::uint16_t>(r.setup[6] | (r.setup[7] << 8));
    const bool device_to_host = (r.setup[0] & 0x80) != 0;
    if (++waited_ > kControlTimeoutFrames) return fail("control request timed out");
    switch (stage_) {
    case Stage::Setup:
        bus.setup(r.setup);
        received_.clear();
        stage_ = length != 0 && device_to_host ? Stage::DataIn : Stage::StatusIn;
        return;
    case Stage::DataIn: {
        auto packet = bus.in(0);
        if (!packet) {
            if (bus.control_stalled()) fail("request stalled");
            return;
        }
        received_.insert(received_.end(), packet->begin(), packet->end());
        if (packet->size() < max_packet0_ || received_.size() >= length) stage_ = Stage::StatusOut;
        return;
    }
    case Stage::StatusOut:
        if (!bus.out(0, {})) return;
        break;
    case Stage::StatusIn: {
        auto packet = bus.in(0);
        if (!packet) {
            if (bus.control_stalled()) fail("request stalled");
            return;
        }
        break;
    }
    }
    // Completed: hand over the data and move to the next request.
    const auto done = r.done;
    const auto data = received_;
    ++next_;
    stage_ = Stage::Setup;
    waited_ = 0;
    if (done) done(data);
}

}  // namespace latasim::usb
