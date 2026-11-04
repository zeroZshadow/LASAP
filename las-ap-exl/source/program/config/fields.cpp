#include "config/fields.hpp"
#include <algorithm>

namespace {
    constexpr std::string_view kSeparators(" \0", 2);
}

std::string_view Fields::Str() {
    size_t start = m_Rest.find_first_not_of(kSeparators);
    if (start == std::string_view::npos) {
        m_Rest = {};
        return {};
    }
    m_Rest.remove_prefix(start);
    size_t end = std::min(m_Rest.find_first_of(kSeparators), m_Rest.size());
    std::string_view field = m_Rest.substr(0, end);
    // The line's end is a 0 already
    if (m_Terminate && end < m_Rest.size()) const_cast<char&>(m_Rest[end]) = 0;
    m_Rest.remove_prefix(end);
    return field;
}

std::string_view Fields::Rest() {
    if (!m_Rest.empty() && kSeparators.find(m_Rest.front()) != std::string_view::npos) m_Rest.remove_prefix(1);
    std::string_view rest = m_Rest;
    m_Rest = {};
    return rest;
}
