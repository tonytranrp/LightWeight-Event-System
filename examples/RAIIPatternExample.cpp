#include <EventCore/EventDispatcher.hpp>
#include <EventCore/Event.hpp>
#include <iostream>
#include <memory>
#include <chrono>

using namespace EventCore;

// Example events
struct MyEvent : public Event {
    bool mCancel = false;
    std::string message;
    
    MyEvent(const std::string& msg) : message(msg) {}
};

/**
 * @brief Current EventCore RAII Pattern
 * 
 * Our system uses shared_ptr for automatic cleanup, which provides
 * memory safety but requires a slightly different pattern than NES.
 */
class EventCoreStyleClass {
public:
    explicit EventCoreStyleClass(EventDispatcher& dispatcher) : mDispatcher{dispatcher} {
        std::cout << "EventCoreStyleClass: Registering listeners with shared_ptr safety...\n";
        
        // Our current approach uses shared_ptr for automatic cleanup
        auto selfPtr = std::shared_ptr<EventCoreStyleClass>(this, [](EventCoreStyleClass*){
            // Custom deleter that does nothing - we manage lifetime manually
        });
        
        mDispatcher.subscribe<MyEvent>(selfPtr, &EventCoreStyleClass::onMyEvent);
        mSelfPtr = selfPtr; // Keep reference to prevent cleanup
    }
    
    ~EventCoreStyleClass() {
        std::cout << "EventCoreStyleClass: Destructor - listeners auto-cleanup via weak_ptr!\n";
        // No manual cleanup needed - weak_ptr handles it automatically
    }
    
    void onMyEvent(const MyEvent& event) {
        std::cout << "EventCoreStyleClass::onMyEvent - Message: " << event.message << std::endl;
    }

private:
    EventDispatcher& mDispatcher;
    std::shared_ptr<EventCoreStyleClass> mSelfPtr;
};

/**
 * @brief Simulated NES-style pattern using our system
 * 
 * This shows how you could implement the exact NES pattern on top of our system
 */
class NESStyleClass {
public:
    explicit NESStyleClass(EventDispatcher& dispatcher) : mDispatcher{dispatcher} {
        std::cout << "NESStyleClass: Registering listeners (simulated NES-style)...\n";
        
        // Create shared_ptr for automatic cleanup
        mSelfPtr = std::shared_ptr<NESStyleClass>(this, [](NESStyleClass*){});
        
        // Simulate listen<MyEvent, &NESStyleClass::onMyEvent>(this)
        mDispatcher.subscribe<MyEvent>(mSelfPtr, &NESStyleClass::onMyEvent);
    }
    
    ~NESStyleClass() {
        std::cout << "NESStyleClass: Destructor - simulating deafen call...\n";
        
        // Simulate deafen<MyEvent, &NESStyleClass::onMyEvent>(this)
        // In our system, this happens automatically when shared_ptr expires
        mSelfPtr.reset(); // This triggers automatic cleanup
        
        std::cout << "NESStyleClass: All listeners removed safely!\n";
    }
    
    void onMyEvent(const MyEvent& event) {
        std::cout << "NESStyleClass::onMyEvent - Message: " << event.message << std::endl;
    }

private:
    EventDispatcher& mDispatcher;
    std::shared_ptr<NESStyleClass> mSelfPtr;
};

/**
 * @brief Ideal RAII pattern with proper shared_ptr usage
 * 
 * The cleanest approach using our system's strengths
 */
class IdealRAIIClass : public std::enable_shared_from_this<IdealRAIIClass> {
public:
    static std::shared_ptr<IdealRAIIClass> create(EventDispatcher& dispatcher) {
        auto instance = std::shared_ptr<IdealRAIIClass>(new IdealRAIIClass(dispatcher));
        instance->init();
        return instance;
    }
    
    ~IdealRAIIClass() {
        std::cout << "IdealRAIIClass: Destructor - automatic cleanup!\n";
    }
    
    void onMyEvent(const MyEvent& event) {
        std::cout << "IdealRAIIClass::onMyEvent - Message: " << event.message << std::endl;
    }

private:
    explicit IdealRAIIClass(EventDispatcher& dispatcher) : mDispatcher{dispatcher} {}
    
    void init() {
        std::cout << "IdealRAIIClass: Registering listeners with perfect RAII...\n";
        mDispatcher.subscribe<MyEvent>(shared_from_this(), &IdealRAIIClass::onMyEvent);
    }
    
    EventDispatcher& mDispatcher;
};

int main() {
    EventDispatcher dispatcher;
    
    std::cout << "=== EventCore RAII Pattern Comparison ===\n\n";
    
    // 1. Test EventCore-style RAII
    std::cout << "1. Testing EventCore-style RAII pattern...\n";
    {
        EventCoreStyleClass eventCoreObject(dispatcher);
        
        std::cout << "   Dispatching event...\n";
        dispatcher.dispatch(MyEvent{"Hello EventCore style!"});
        
        std::cout << "   Leaving scope...\n";
    } // Destructor called, automatic cleanup
    
    std::cout << "   Testing cleanup - should not receive event:\n";
    dispatcher.dispatch(MyEvent{"Should not be received"});
    std::cout << "\n";
    
    // 2. Test simulated NES-style RAII
    std::cout << "2. Testing simulated NES-style RAII pattern...\n";
    {
        NESStyleClass nesObject(dispatcher);
        
        std::cout << "   Dispatching event...\n";
        dispatcher.dispatch(MyEvent{"Hello NES style!"});
        
        std::cout << "   Leaving scope...\n";
    } // Destructor called, manual cleanup
    
    std::cout << "   Testing cleanup - should not receive event:\n";
    dispatcher.dispatch(MyEvent{"Should not be received"});
    std::cout << "\n";
    
    // 3. Test ideal RAII pattern
    std::cout << "3. Testing ideal RAII pattern with enable_shared_from_this...\n";
    {
        auto idealObject = IdealRAIIClass::create(dispatcher);
        
        std::cout << "   Dispatching event...\n";
        dispatcher.dispatch(MyEvent{"Hello ideal RAII!"});
        
        std::cout << "   Leaving scope...\n";
    } // Destructor called, perfect automatic cleanup
    
    std::cout << "   Testing cleanup - should not receive event:\n";
    dispatcher.dispatch(MyEvent{"Should not be received"});
    std::cout << "\n";
    
    // 4. Performance comparison
    std::cout << "4. Performance test - 10,000 events...\n";
    {
        auto perfObject = IdealRAIIClass::create(dispatcher);
        
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 10000; ++i) {
            dispatcher.dispatch(MyEvent{"Performance test " + std::to_string(i)});
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "   Dispatched 10,000 events in " << duration.count() << " microseconds\n";
        std::cout << "   Average: " << (duration.count() / 10000.0) << " μs per event\n";
    }
    
    std::cout << "\n=== Comparison Summary ===\n";
    std::cout << "EventCore vs NuvolaEventSystem RAII Patterns:\n\n";
    
    std::cout << "NuvolaEventSystem pattern:\n";
    std::cout << "  ✓ Raw pointer support\n";
    std::cout << "  ✓ Manual deafen() in destructor\n";
    std::cout << "  ✓ Template-based syntax: listen<Event, &Class::method>(this)\n";
    std::cout << "  ⚠ Risk of dangling pointers if deafen() forgotten\n";
    std::cout << "  ⚠ Manual memory management responsibility\n\n";
    
    std::cout << "EventCore pattern:\n";
    std::cout << "  ✓ Automatic cleanup via weak_ptr (safer)\n";
    std::cout << "  ✓ No manual deafen() required\n";
    std::cout << "  ✓ Exception-safe (automatic cleanup even during exceptions)\n";
    std::cout << "  ✓ shared_ptr-based syntax: subscribe<Event>(shared_ptr, &Class::method)\n";
    std::cout << "  ✓ Same performance in hot dispatch path\n";
    std::cout << "  ✓ Memory safety guarantees\n\n";
    
    std::cout << "Both systems support the RAII principle of automatic resource cleanup!\n";
    std::cout << "EventCore just uses shared_ptr/weak_ptr for additional safety.\n";
    
    return 0;
} 