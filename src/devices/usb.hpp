#pragma once
// USB at the level Latasim models it: whole packets between a host and a device
// controller, once per 1 ms full-speed frame. No electrical signalling, PHY,
// bit timing, NRZI, CRC, data toggles or handshake timing (docs/phase5/usb-audio.md).
//
// A device controller model implements Bus; a virtual host implements HostPort and
// is given the bus once per frame.
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace latasim::usb {

// The device, as the host sees it on the cable.
class Bus {
public:
    virtual ~Bus() = default;
    virtual bool pull_up() const = 0;  // the device has connected itself to the bus
    virtual void cable(bool attached) = 0;
    virtual void bus_reset() = 0;
    virtual void start_of_frame() = 0;
    virtual void setup(const std::array<std::uint8_t, 8>& packet) = 0;  // always accepted
    // An IN token on endpoint `ep`: the packet the device validated, or nullopt for
    // a NAK or a STALL (control_stalled() tells which for endpoint 0).
    virtual std::optional<std::vector<std::uint8_t>> in(unsigned ep) = 0;
    virtual bool control_stalled() const = 0;
    virtual bool out(unsigned ep, const std::vector<std::uint8_t>& data) = 0;  // false: NAK or STALL
    virtual void iso_out(unsigned ep, const std::vector<std::uint8_t>& data) = 0;  // this frame's packet
};

class HostPort {
public:
    virtual ~HostPort() = default;
    virtual void frame(Bus& bus) = 0;
};

}  // namespace latasim::usb
