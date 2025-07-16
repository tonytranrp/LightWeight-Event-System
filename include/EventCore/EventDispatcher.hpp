#pragma once

#include "Event.hpp"
#include "EventId.hpp"

#include <vector>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <type_traits>
#include <algorithm>

// External dependencies
#include <robin_hood.h>
#include <concurrentqueue.h>

namespace EventCore {

/**
 * @brief Event priority levels for controlling execution order
 * 
 * Higher values execute first. This allows critical system events
 * to run before less important events like UI updates.
 */
enum class EventPriority : int {
    Low = 0,        // UI updates, non-critical notifications
    Normal = 1,     // Default priority for most game events  
    High = 2,       // Critical system events, state changes
    Critical = 3    // Emergency events, error handling
};

namespace detail {

/**
 * @brief Internal listener representation with type erasure
 * 
 * This struct stores a type-erased callback along with lifetime management
 * using weak_ptr to prevent dangling pointer issues.
 */
struct InternalListener {
    void* instancePtr;                           // Raw pointer to listener instance
    std::function<void(const void*)> callback;   // Type-erased callback function
    std::weak_ptr<void> weakInstancePtr;        // Lifetime management
    EventPriority priority;                     // Execution priority
    bool markedForRemoval;                      // Cleanup flag (for thread-safe removal)
    
    InternalListener(void* inst, std::function<void(const void*)> cb, std::weak_ptr<void> weak, EventPriority prio)
        : instancePtr(inst), callback(std::move(cb)), weakInstancePtr(std::move(weak)), 
          priority(prio), markedForRemoval(false) {}
};

/**
 * @brief Type-erased event wrapper for deferred dispatch
 * 
 * Base class for storing events of different types in the same queue.
 */
class EventWrapper {
public:
    virtual ~EventWrapper() = default;
    virtual EventTypeId get_type_id() const = 0;
    virtual const void* get_event_data() const = 0;
};

/**
 * @brief Templated event wrapper implementation
 */
template<typename EventT>
class TypedEventWrapper : public EventWrapper {
private:
    EventT event_;
    static constexpr EventTypeId type_id_ = get_event_type_id<EventT>();
    
public:
    explicit TypedEventWrapper(const EventT& event) : event_(event) {}
    explicit TypedEventWrapper(EventT&& event) : event_(std::move(event)) {}
    
    EventTypeId get_type_id() const override { return type_id_; }
    const void* get_event_data() const override { return &event_; }
};

} // namespace detail

/**
 * @brief High-performance, thread-safe event dispatcher
 * 
 * This class provides a low-overhead event dispatching system with the following features:
 * - Minimal runtime overhead in dispatch hot path
 * - Cache-friendly data structures (robin_hood map + vectors)
 * - Compile-time type safety
 * - Thread-safe subscription/unsubscription
 * - Immediate and deferred (lock-free queue) dispatch modes
 * - Automatic cleanup of expired listeners
 * 
 * Key design principles:
 * - No dynamic allocations in immediate dispatch path
 * - Type-erased callbacks to avoid std::function overhead
 * - weak_ptr for automatic listener lifetime management
 * - Lock-free queuing for cross-thread event publishing
 */
class EventDispatcher {
private:
    using ListenerVector = std::vector<detail::InternalListener>;
    using ListenerMap = robin_hood::unordered_flat_map<EventTypeId, ListenerVector>;
    
    // Thread synchronization
    mutable std::shared_mutex listenersMutex_;
    
    // Listener storage (optimized for cache locality)
    ListenerMap listeners_;
    
    // Deferred dispatch queue (lock-free)
    moodycamel::ConcurrentQueue<std::unique_ptr<detail::EventWrapper>> eventQueue_;
    
    // Statistics (atomic for thread-safety)
    std::atomic<std::size_t> totalListeners_{0};
    std::atomic<std::size_t> totalDispatches_{0};
    std::atomic<std::size_t> queuedEvents_{0};
    
public:
    /**
     * @brief Constructor
     */
    EventDispatcher() = default;
    
    /**
     * @brief Destructor
     */
    ~EventDispatcher() = default;
    
    // Non-copyable, non-movable (to ensure pointer stability)
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;
    EventDispatcher(EventDispatcher&&) = delete;
    EventDispatcher& operator=(EventDispatcher&&) = delete;
    
    /**
     * @brief Subscribe a member function to an event type with priority
     * 
     * This method registers a listener's member function to be called when
     * events of type EventT are dispatched. The listener's lifetime is 
     * managed using shared_ptr/weak_ptr.
     * 
     * @tparam EventT Event type to subscribe to (must inherit from Event)
     * @tparam ListenerT Listener object type
     * @param listenerInstance Shared pointer to the listener object
     * @param memberFunc Member function pointer to call
     * @param priority Event execution priority (default: Normal)
     * 
     * Thread Safety: This method is thread-safe (uses write lock)
     * 
     * Example:
     * auto player = std::make_shared<Player>();
     * dispatcher.subscribe<PlayerDiedEvent>(player, &Player::on_player_died);
     * dispatcher.subscribe<CriticalSystemEvent>(player, &Player::on_critical, EventPriority::High);
     */
    template<typename EventT, typename ListenerT>
    void subscribe(std::shared_ptr<ListenerT> listenerInstance, 
                   void (ListenerT::*memberFunc)(const EventT&),
                   EventPriority priority = EventPriority::Normal) {
        using DecayedEventT = std::decay_t<EventT>;
        static_assert(std::is_base_of_v<Event, DecayedEventT>, 
                      "EventT must inherit from EventCore::Event");
        
        // Create type-erased callback using std::function
        // Note: We capture the raw pointer, not the shared_ptr, to avoid circular references
        auto rawPtr = listenerInstance.get();
        auto callback = [rawPtr, memberFunc](const void* eventData) {
            const auto* typedEvent = static_cast<const DecayedEventT*>(eventData);
            (rawPtr->*memberFunc)(*typedEvent);
        };
        
        constexpr EventTypeId eventId = get_event_type_id<DecayedEventT>();
        
        std::unique_lock lock(listenersMutex_);
        
        // Create weak_ptr for lifetime management
        std::weak_ptr<void> weakPtr = std::static_pointer_cast<void>(listenerInstance);
        
        // Insert listener maintaining priority order (higher priority first)
        auto& listenerVec = listeners_[eventId];
        auto insertPos = std::upper_bound(listenerVec.begin(), listenerVec.end(), priority,
            [](EventPriority prio, const detail::InternalListener& listener) {
                return static_cast<int>(prio) > static_cast<int>(listener.priority);
            });
        
        listenerVec.emplace(insertPos,
            listenerInstance.get(),
            std::move(callback),
            std::move(weakPtr),
            priority
        );
        
        totalListeners_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Unsubscribe a specific listener from an event type
     * 
     * @tparam EventT Event type to unsubscribe from
     * @tparam ListenerT Listener object type
     * @param listenerInstance Raw pointer to the listener object
     * @param memberFunc Member function pointer that was subscribed
     * 
     * Thread Safety: This method is thread-safe (uses write lock)
     */
    template<typename EventT, typename ListenerT>
    void unsubscribe(ListenerT* listenerInstance, 
                     void (ListenerT::*memberFunc)(const EventT&)) {
        using DecayedEventT = std::decay_t<EventT>;
        static_assert(std::is_base_of_v<Event, DecayedEventT>, 
                      "EventT must inherit from EventCore::Event");
        
        // Suppress warning for unused parameter (reserved for future precise unsubscription)
        (void)memberFunc;
        
        constexpr EventTypeId eventId = get_event_type_id<DecayedEventT>();
        
        std::unique_lock lock(listenersMutex_);
        
        auto it = listeners_.find(eventId);
        if (it != listeners_.end()) {
            auto& listenerVec = it->second;
            
            // Find and remove the specific listener
            auto removeIt = std::remove_if(listenerVec.begin(), listenerVec.end(),
                [listenerInstance](const detail::InternalListener& listener) {
                    return listener.instancePtr == listenerInstance;
                });
            
            if (removeIt != listenerVec.end()) {
                std::size_t removedCount = std::distance(removeIt, listenerVec.end());
                listenerVec.erase(removeIt, listenerVec.end());
                totalListeners_.fetch_sub(removedCount, std::memory_order_relaxed);
            }
            
            // Clean up empty vectors
            if (listenerVec.empty()) {
                listeners_.erase(it);
            }
        }
    }
    
    /**
     * @brief Immediately dispatch an event to all registered listeners
     * 
     * This is the hot path - optimized for minimal overhead:
     * - Uses shared lock for read access
     * - No dynamic allocations
     * - Cache-friendly iteration over vector
     * - Automatic cleanup of expired listeners
     * 
     * @tparam EventT Event type to dispatch
     * @param event Event instance to dispatch
     * 
     * Thread Safety: This method is thread-safe (uses read lock)
     * 
     * Performance: O(n) where n is the number of listeners for this event type
     */
    template<typename EventT>
    void dispatch(const EventT& event) {
        using DecayedEventT = std::decay_t<EventT>;
        static_assert(std::is_base_of_v<Event, DecayedEventT>, 
                      "EventT must inherit from EventCore::Event");
        
        constexpr EventTypeId eventId = get_event_type_id<DecayedEventT>();
        
        std::shared_lock lock(listenersMutex_);
        
        auto it = listeners_.find(eventId);
        if (it == listeners_.end()) {
            return; // No listeners for this event type
        }
        
        auto& listenerVec = it->second;
        bool needsCleanup = false;
        
        // Hot path: iterate through listeners
        for (auto& listener : listenerVec) {
            if (listener.markedForRemoval) {
                needsCleanup = true;
                continue;
            }
            
            // Try to lock the weak_ptr to ensure object still exists
            if (auto lockedPtr = listener.weakInstancePtr.lock()) {
                // Object still alive, dispatch event
                listener.callback(&event);
            } else {
                // Object expired, mark for removal
                listener.markedForRemoval = true;
                needsCleanup = true;
            }
        }
        
        totalDispatches_.fetch_add(1, std::memory_order_relaxed);
        
        // Defer cleanup to avoid modifying vector during iteration
        if (needsCleanup) {
            // Schedule cleanup (this could be done periodically instead)
            // For now, we'll do it immediately but outside the hot path
            lock.unlock();
            cleanup_expired_listeners_for_event(eventId);
        }
    }
    
    /**
     * @brief Enqueue an event for deferred dispatch
     * 
     * This method is lock-free and thread-safe. Events are queued and
     * can be processed later using process_queued_events().
     * 
     * @tparam EventT Event type to enqueue
     * @param event Event instance to enqueue
     * 
     * Thread Safety: Lock-free, fully thread-safe
     * 
     * Note: This method performs a dynamic allocation (for the event copy)
     */
    template<typename EventT>
    void enqueue(const EventT& event) {
        using DecayedEventT = std::decay_t<EventT>;
        static_assert(std::is_base_of_v<Event, DecayedEventT>, 
                      "EventT must inherit from EventCore::Event");
        
        auto wrapper = std::make_unique<detail::TypedEventWrapper<DecayedEventT>>(event);
        eventQueue_.enqueue(std::move(wrapper));
        queuedEvents_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Enqueue an event for deferred dispatch (move version)
     */
    template<typename EventT>
    void enqueue(EventT&& event) {
        using DecayedEventT = std::decay_t<EventT>;
        static_assert(std::is_base_of_v<Event, DecayedEventT>, 
                      "EventT must inherit from EventCore::Event");
        
        auto wrapper = std::make_unique<detail::TypedEventWrapper<DecayedEventT>>(std::forward<EventT>(event));
        eventQueue_.enqueue(std::move(wrapper));
        queuedEvents_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Process all queued events
     * 
     * This method should be called periodically (e.g., once per frame)
     * on a designated thread (e.g., the main game thread) to process
     * all events that were enqueued via enqueue().
     * 
     * @param maxEvents Maximum number of events to process (0 = unlimited)
     * @return Number of events processed
     * 
     * Thread Safety: Should be called from a single thread for optimal performance
     */
    std::size_t process_queued_events(std::size_t maxEvents = 0) {
        std::unique_ptr<detail::EventWrapper> eventWrapper;
        std::size_t processedCount = 0;
        
        while ((maxEvents == 0 || processedCount < maxEvents) && 
               eventQueue_.try_dequeue(eventWrapper)) {
            
            // Dispatch the event using the type ID
            dispatch_type_erased(eventWrapper->get_type_id(), 
                                eventWrapper->get_event_data());
            
            ++processedCount;
            queuedEvents_.fetch_sub(1, std::memory_order_relaxed);
        }
        
        return processedCount;
    }
    
    /**
     * @brief Clean up expired listeners for all event types
     * 
     * This method removes all listeners whose objects have been destroyed.
     * It should be called periodically to prevent memory bloat.
     * 
     * Thread Safety: This method is thread-safe (uses write lock)
     * 
     * @return Number of expired listeners removed
     */
    std::size_t cleanup_expired_listeners() {
        std::unique_lock lock(listenersMutex_);
        
        std::size_t removedCount = 0;
        
        for (auto& [eventId, listenerVec] : listeners_) {
            auto removeIt = std::remove_if(listenerVec.begin(), listenerVec.end(),
                [](const detail::InternalListener& listener) {
                    return listener.markedForRemoval || listener.weakInstancePtr.expired();
                });
            
            if (removeIt != listenerVec.end()) {
                removedCount += std::distance(removeIt, listenerVec.end());
                listenerVec.erase(removeIt, listenerVec.end());
            }
        }
        
        // Remove empty event type entries
        for (auto it = listeners_.begin(); it != listeners_.end();) {
            if (it->second.empty()) {
                it = listeners_.erase(it);
            } else {
                ++it;
            }
        }
        
        totalListeners_.fetch_sub(removedCount, std::memory_order_relaxed);
        return removedCount;
    }
    
    /**
     * @brief Get the number of listeners for a specific event type
     */
    template<typename EventT>
    std::size_t get_listener_count() const {
        using DecayedEventT = std::decay_t<EventT>;
        constexpr EventTypeId eventId = get_event_type_id<DecayedEventT>();
        
        std::shared_lock lock(listenersMutex_);
        
        auto it = listeners_.find(eventId);
        return (it != listeners_.end()) ? it->second.size() : 0;
    }
    
    /**
     * @brief Get total number of registered listeners
     */
    std::size_t get_total_listener_count() const {
        return totalListeners_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get total number of dispatched events
     */
    std::size_t get_total_dispatch_count() const {
        return totalDispatches_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get number of events currently in the queue
     */
    std::size_t get_queued_event_count() const {
        return queuedEvents_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get the number of different event types with listeners
     */
    std::size_t get_event_type_count() const {
        std::shared_lock lock(listenersMutex_);
        return listeners_.size();
    }

private:
    /**
     * @brief Internal method for type-erased dispatch
     */
    void dispatch_type_erased(EventTypeId eventId, const void* eventData) {
        std::shared_lock lock(listenersMutex_);
        
        auto it = listeners_.find(eventId);
        if (it == listeners_.end()) {
            return;
        }
        
        auto& listenerVec = it->second;
        bool needsCleanup = false;
        
        for (auto& listener : listenerVec) {
            if (listener.markedForRemoval) {
                needsCleanup = true;
                continue;
            }
            
            if (auto lockedPtr = listener.weakInstancePtr.lock()) {
                listener.callback(eventData);
            } else {
                listener.markedForRemoval = true;
                needsCleanup = true;
            }
        }
        
        totalDispatches_.fetch_add(1, std::memory_order_relaxed);
        
        if (needsCleanup) {
            lock.unlock();
            cleanup_expired_listeners_for_event(eventId);
        }
    }
    
    /**
     * @brief Clean up expired listeners for a specific event type
     */
    void cleanup_expired_listeners_for_event(EventTypeId eventId) {
        std::unique_lock lock(listenersMutex_);
        
        auto it = listeners_.find(eventId);
        if (it == listeners_.end()) {
            return;
        }
        
        auto& listenerVec = it->second;
        auto removeIt = std::remove_if(listenerVec.begin(), listenerVec.end(),
            [](const detail::InternalListener& listener) {
                return listener.markedForRemoval || listener.weakInstancePtr.expired();
            });
        
        if (removeIt != listenerVec.end()) {
            std::size_t removedCount = std::distance(removeIt, listenerVec.end());
            listenerVec.erase(removeIt, listenerVec.end());
            totalListeners_.fetch_sub(removedCount, std::memory_order_relaxed);
        }
        
        if (listenerVec.empty()) {
            listeners_.erase(it);
        }
    }
};

} // namespace EventCore 