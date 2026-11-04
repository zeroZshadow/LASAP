#pragma once
// A text line's fields, separated by spaces: the seed file's lines (config/seed_file.hpp) and the client's protocol
// lines (DESIGN.md, Game ↔ client protocol). Each field read is a view into the line.
#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>

class Fields {
public:
    // With terminate, each field read is ended with a 0 in place of its separator, for names the game reads to their
    // terminator: the line must be writable and end with a 0.
    explicit Fields(std::string_view line, bool terminate = false) : m_Rest(line), m_Terminate(terminate) {}
    // The next field; "" once none is left.
    std::string_view Str();
    // The next field as a number (an enum by its underlying type); none when it is missing or is not one.
    template<typename T>
    std::optional<T> Num() {
        if constexpr (std::is_enum_v<T>) {
            std::optional<std::underlying_type_t<T>> value = Num<std::underlying_type_t<T>>();
            return value ? std::optional<T>(T{*value}) : std::nullopt;
        } else {
            std::string_view field = Str();
            T value{};
            auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value);
            if (field.empty() || error != std::errc() || end != field.data() + field.size()) return std::nullopt;
            return value;
        }
    }
    // The rest of the line after the separator that ends the last field read: a name, which may hold spaces.
    std::string_view Rest();

private:
    std::string_view m_Rest;
    bool m_Terminate;
};
