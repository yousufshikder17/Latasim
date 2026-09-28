#pragma once
// A virtual PC that plays audio to a USB Audio Class 1.0 speaker (docs/phase5/usb-audio.md).
//
// Plugged into a device controller's port (usb::HostPort), it runs one transaction
// per 1 ms frame: when the device connects itself it resets the bus, enumerates it
// with standard requests (device descriptor, address, configuration descriptor,
// configuration), finds the audio streaming interface with an isochronous OUT
// endpoint and its format, selects that alternate setting, then sends one packet
// of PCM from its source every frame. A device that disconnects itself is noticed
// on the next frame and enumerated again when it reconnects.
//
// Everything is deterministic: the source is data, not a sound card, and packets go
// out on frame boundaries of virtual time. There is no operating-system USB stack.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "devices/usb.hpp"

namespace latasim::usb {

class PcmSource {
public:
    virtual ~PcmSource() = default;
    virtual std::int16_t next() = 0;
};

// A triangle wave: integer arithmetic only, so every run gives the same samples.
class TonePcm : public PcmSource {
public:
    TonePcm(unsigned sample_rate, unsigned frequency, std::int16_t amplitude);
    std::int16_t next() override;

private:
    unsigned period_;
    unsigned phase_ = 0;
    std::int32_t amplitude_;
};

// Given samples, then silence (or the samples again, looped).
class BufferPcm : public PcmSource {
public:
    explicit BufferPcm(std::vector<std::int16_t> samples, bool loop = false);
    std::int16_t next() override;
    std::size_t position() const { return pos_; }

private:
    std::vector<std::int16_t> samples_;
    bool loop_;
    std::size_t pos_ = 0;
};

struct AudioStream {
    unsigned interface = 0;
    unsigned alternate = 0;
    unsigned endpoint = 0;  // endpoint number (OUT)
    unsigned max_packet = 0;
    unsigned channels = 0;
    unsigned subframe = 0;  // bytes per sample
    unsigned sample_rate = 0;
};

class AudioHost : public HostPort {
public:
    enum class State { Detached, Enumerating, Streaming, Failed };

    void frame(Bus& bus) override;

    // nullptr (the default) plays silence.
    void set_source(PcmSource* source) { source_ = source; }
    // Paused: no packets are sent, as when the PC stops playing.
    void set_playing(bool playing) { playing_ = playing; }
    bool playing() const { return playing_; }

    State state() const { return state_; }
    std::string state_name() const;
    const AudioStream& stream() const { return stream_; }
    std::uint64_t frames() const { return frames_; }
    std::uint64_t packets_sent() const { return packets_; }
    std::uint64_t samples_sent() const { return samples_; }
    unsigned enumerations() const { return enumerations_; }
    const std::string& error() const { return error_; }

private:
    struct Request {
        std::array<std::uint8_t, 8> setup{};
        std::function<void(const std::vector<std::uint8_t>&)> done;
    };
    enum class Stage { Setup, DataIn, StatusIn, StatusOut };

    void start_enumeration();
    void control(std::uint8_t type, std::uint8_t request, std::uint16_t value, std::uint16_t index,
                 std::uint16_t length, std::function<void(const std::vector<std::uint8_t>&)> done);
    void step_control(Bus& bus);
    void fail(const std::string& why);
    bool parse_configuration(const std::vector<std::uint8_t>& config);

    State state_ = State::Detached;
    PcmSource* source_ = nullptr;
    bool playing_ = true;
    bool reset_done_ = false;
    std::vector<Request> queue_;
    std::size_t next_ = 0;
    Stage stage_ = Stage::Setup;
    std::vector<std::uint8_t> received_;
    unsigned waited_ = 0;
    unsigned max_packet0_ = 8;
    std::uint8_t configuration_ = 1;
    AudioStream stream_;
    std::uint64_t frames_ = 0, packets_ = 0, samples_ = 0;
    unsigned enumerations_ = 0;
    std::string error_;
};

}  // namespace latasim::usb
