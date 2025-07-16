#pragma once

namespace EventCore {

/**
 * @brief Base event class
 * 
 * All custom event types must inherit from this base class.
 * This ensures compile-time type safety and provides a common interface.
 * 
 * Example usage:
 * struct PlayerDiedEvent : public Event {
 *     int playerId;
 *     float damage;
 * };
 */
struct Event {
    virtual ~Event() = default;
};

} // namespace EventCore 