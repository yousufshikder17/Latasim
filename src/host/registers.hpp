#pragma once
// Register proxies for host-compiled firmware (see device/LPC17xx.h).
//
// A proxy stores no register value. Reading it reads the bound board's MCU at the
// register's LPC address; assigning to it writes there, with the access width of
// its type (8, 16 or 32 bits). Compound assignments are a read then a write, as
// the compiled ARM code would do.
#include <cstdint>

namespace latasim::host {

// Access the bound board's MCU. Faults (unmapped address, misaligned access,
// no board bound) abort with a message, as a register access from C would.
std::uint32_t mmio_read(std::uint32_t address, unsigned size);
void mmio_write(std::uint32_t address, unsigned size, std::uint32_t value);

// The LPC address of a proxy inside latasim_gpio_ports or latasim_sc.
std::uint32_t lpc_address(const void* proxy);

// A register at a known LPC address: LATASIM_REG32(address).
template <typename T>
class RegisterAt {
public:
    explicit RegisterAt(std::uint32_t address) : address_(address) {}
    RegisterAt(const RegisterAt&) = default;

    operator T() const { return static_cast<T>(mmio_read(address_, sizeof(T))); }
    RegisterAt& operator=(T value) {
        mmio_write(address_, sizeof(T), value);
        return *this;
    }
    // Register-to-register assignment copies the value, not the address.
    RegisterAt& operator=(const RegisterAt& other) { return *this = static_cast<T>(other); }
    RegisterAt& operator|=(T bits) { return *this = static_cast<T>(*this | bits); }
    RegisterAt& operator&=(T bits) { return *this = static_cast<T>(*this & bits); }
    RegisterAt& operator^=(T bits) { return *this = static_cast<T>(*this ^ bits); }

private:
    std::uint32_t address_;
};

// A register member of a peripheral structure: its address is where it sits.
template <typename T>
class Register {
public:
    Register() = default;
    Register(const Register&) = delete;

    operator T() const { return at(); }
    Register& operator=(T value) {
        at() = value;
        return *this;
    }
    Register& operator=(const Register& other) { return *this = static_cast<T>(other); }
    Register& operator|=(T bits) { return *this = static_cast<T>(*this | bits); }
    Register& operator&=(T bits) { return *this = static_cast<T>(*this & bits); }
    Register& operator^=(T bits) { return *this = static_cast<T>(*this ^ bits); }

private:
    RegisterAt<T> at() const { return RegisterAt<T>(lpc_address(this)); }

    T size_;  // gives the proxy the register's size; never read or written
};

}  // namespace latasim::host
