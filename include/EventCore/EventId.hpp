#pragma once

#include <cstdint>
#include <string_view>

namespace EventCore {

using EventTypeId = std::uint64_t;

namespace detail {

/**
 * @brief Compile-time FNV-1a 64-bit hash implementation
 * 
 * FNV-1a is a fast, simple hash function with good distribution properties.
 * This implementation is constexpr, allowing for compile-time hash computation.
 */
constexpr std::uint64_t FNV_OFFSET_BASIS_64 = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME_64 = 1099511628211ULL;

constexpr std::uint64_t fnv1a_hash(std::string_view str) noexcept {
    std::uint64_t hash = FNV_OFFSET_BASIS_64;
    for (char c : str) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= FNV_PRIME_64;
    }
    return hash;
}

/**
 * @brief Get a compile-time type name representation
 * 
 * Uses __PRETTY_FUNCTION__ or __FUNCSIG__ to get a unique string representation.
 * This is implementation-specific but works at compile time.
 */
template<typename T>
constexpr std::string_view get_type_name() noexcept {
#ifdef _MSC_VER
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

} // namespace detail

/**
 * @brief Generate a compile-time unique EventTypeId for a given event type
 * 
 * This function uses the FNV-1a hash algorithm to generate a 64-bit hash
 * from the type's mangled name. The hash is computed at compile time.
 * 
 * @tparam EventT The event type (must inherit from Event)
 * @return constexpr EventTypeId Unique identifier for the event type
 * 
 * Example usage:
 * constexpr auto id = get_event_type_id<PlayerDiedEvent>();
 */
template<typename EventT>
constexpr EventTypeId get_event_type_id() noexcept {
    static_assert(std::is_base_of_v<Event, EventT>, 
                  "EventT must inherit from EventCore::Event");
    
    constexpr auto type_name = detail::get_type_name<EventT>();
    return detail::fnv1a_hash(type_name);
}

/**
 * @brief Macro for convenient EventTypeId generation
 * 
 * This macro provides a shorter syntax for getting event type IDs.
 * 
 * Example usage:
 * constexpr auto id = EVENT_TYPE_ID(PlayerDiedEvent);
 */
#define EVENT_TYPE_ID(EventType) ::EventCore::get_event_type_id<EventType>()

} // namespace EventCore 