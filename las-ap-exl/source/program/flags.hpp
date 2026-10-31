#pragma once
// Flags<E>: a set of the bit flags an enum class of masks names, as the type of a flags field of the game's or the
// module's. One flag (E) and a set (Flags<E>) stay apart: Has takes one flag, Any and All a set, and a set never
// compares with one flag. Each enum allows `A | B` with FLAGS_ENUM(E) in its own namespace. It compiles to the same
// code as masks on the integer.
#include <bit>
#include <type_traits>

template<typename E>
class Flags {
    static_assert(std::is_enum_v<E>);
    using U = std::underlying_type_t<E>;
    U bits_;

public:
    Flags() = default;  // trivial, as the integer it stands for; Flags{} is empty
    constexpr Flags(E flag) : bits_(static_cast<U>(flag)) {}
    static constexpr Flags FromRaw(U raw) {
        Flags set;
        set.bits_ = raw;
        return set;
    }
    constexpr U Raw() const { return bits_; }

    constexpr bool Has(E flag) const { return (bits_ & static_cast<U>(flag)) != 0; }
    constexpr bool Any(Flags set) const { return (bits_ & set.bits_) != 0; }
    constexpr bool All(Flags set) const { return (bits_ & set.bits_) == set.bits_; }
    // The bits of mask are those of value.
    constexpr bool Match(Flags mask, Flags value) const { return (bits_ & mask.bits_) == value.bits_; }

    constexpr void Set(Flags set) { bits_ |= set.bits_; }
    constexpr void Clear(Flags set) { bits_ &= ~set.bits_; }
    constexpr void Set(E flag, bool on) { on ? Set(flag) : Clear(flag); }

    friend constexpr Flags operator|(Flags a, Flags b) { return FromRaw(a.bits_ | b.bits_); }
    friend constexpr bool operator==(Flags, Flags) = default;
    // `flags == Flag` is the bug of meaning flags.Has(Flag)
    friend bool operator==(Flags, E) = delete;
};

#define FLAGS_ENUM(E) \
    constexpr Flags<E> operator|(E a, E b) { return Flags<E>(a) | b; }
