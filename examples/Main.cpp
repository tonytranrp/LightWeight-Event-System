#include <EventCore/EventDispatcher.hpp>
#include <EventCore/Event.hpp>
#include <EventCore/EventId.hpp>

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <memory>

// Example event types
struct PlayerDiedEvent : public EventCore::Event {
    int playerId;
    float damage;
    std::string cause;
    
    PlayerDiedEvent(int id, float dmg, std::string c) 
        : playerId(id), damage(dmg), cause(std::move(c)) {}
};

struct PlayerLevelUpEvent : public EventCore::Event {
    int playerId;
    int newLevel;
    int experienceGained;
    
    PlayerLevelUpEvent(int id, int level, int exp) 
        : playerId(id), newLevel(level), experienceGained(exp) {}
};

struct GameStateChangeEvent : public EventCore::Event {
    enum class State { Menu, Playing, Paused, GameOver } state;
    
    explicit GameStateChangeEvent(State s) : state(s) {}
};

// Example listener classes
class Player {
public:
    explicit Player(int id) : playerId_(id) {
        std::cout << "Player " << playerId_ << " created\n";
    }
    
    ~Player() {
        std::cout << "Player " << playerId_ << " destroyed\n";
    }
    
    void on_player_died(const PlayerDiedEvent& event) {
        if (event.playerId == playerId_) {
            std::cout << "Player " << playerId_ << " received own death event: " 
                      << event.damage << " damage from " << event.cause << "\n";
        } else {
            std::cout << "Player " << playerId_ << " heard that Player " 
                      << event.playerId << " died\n";
        }
    }
    
    void on_level_up(const PlayerLevelUpEvent& event) {
        std::cout << "Player " << playerId_ << " sees level up: Player " 
                  << event.playerId << " reached level " << event.newLevel << "\n";
    }
    
    int get_id() const { return playerId_; }

private:
    int playerId_;
};

class GameManager {
public:
    GameManager() {
        std::cout << "GameManager created\n";
    }
    
    ~GameManager() {
        std::cout << "GameManager destroyed\n";
    }
    
    void on_player_died(const PlayerDiedEvent& event) {
        std::cout << "GameManager: Processing player death - Player " 
                  << event.playerId << " eliminated\n";
        deadPlayers_++;
    }
    
    void on_level_up(const PlayerLevelUpEvent& event) {
        std::cout << "GameManager: Player " << event.playerId 
                  << " leveled up to " << event.newLevel << "\n";
    }
    
    void on_game_state_change(const GameStateChangeEvent& event) {
        std::cout << "GameManager: Game state changed to ";
        switch (event.state) {
            case GameStateChangeEvent::State::Menu: std::cout << "Menu"; break;
            case GameStateChangeEvent::State::Playing: std::cout << "Playing"; break;
            case GameStateChangeEvent::State::Paused: std::cout << "Paused"; break;
            case GameStateChangeEvent::State::GameOver: std::cout << "GameOver"; break;
        }
        std::cout << "\n";
    }
    
    int get_dead_players() const { return deadPlayers_; }

private:
    int deadPlayers_ = 0;
};

class AudioSystem {
public:
    AudioSystem() {
        std::cout << "AudioSystem created\n";
    }
    
    ~AudioSystem() {
        std::cout << "AudioSystem destroyed\n";
    }
    
    void on_player_died(const PlayerDiedEvent& event) {
        std::cout << "AudioSystem: Playing death sound for Player " 
                  << event.playerId << "\n";
    }
    
    void on_level_up(const PlayerLevelUpEvent& event) {
        std::cout << "AudioSystem: Playing level up sound for Player " 
                  << event.playerId << "\n";
    }
};

// Helper function to print statistics
void print_stats(const EventCore::EventDispatcher& dispatcher) {
    std::cout << "\n=== EventDispatcher Statistics ===\n";
    std::cout << "Total listeners: " << dispatcher.get_total_listener_count() << "\n";
    std::cout << "Total dispatches: " << dispatcher.get_total_dispatch_count() << "\n";
    std::cout << "Queued events: " << dispatcher.get_queued_event_count() << "\n";
    std::cout << "Event types: " << dispatcher.get_event_type_count() << "\n";
    std::cout << "PlayerDiedEvent listeners: " 
              << dispatcher.get_listener_count<PlayerDiedEvent>() << "\n";
    std::cout << "PlayerLevelUpEvent listeners: " 
              << dispatcher.get_listener_count<PlayerLevelUpEvent>() << "\n";
    std::cout << "===================================\n\n";
}

// Worker thread function for stress testing
void worker_thread(EventCore::EventDispatcher& dispatcher, int threadId, int eventCount) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> playerDist(1, 10);
    std::uniform_int_distribution<> damageDist(10, 100);
    std::uniform_int_distribution<> levelDist(1, 50);
    std::uniform_int_distribution<> typeDist(0, 1);
    
    std::vector<std::string> causes = {"Dragon", "Fall", "Lava", "Monster", "PvP"};
    std::uniform_int_distribution<> causeDist(0, static_cast<int>(causes.size() - 1));
    
    std::cout << "Worker thread " << threadId << " starting to enqueue " 
              << eventCount << " events\n";
    
    for (int i = 0; i < eventCount; ++i) {
        if (typeDist(gen) == 0) {
            // Enqueue PlayerDiedEvent
            PlayerDiedEvent event(
                playerDist(gen),
                static_cast<float>(damageDist(gen)),
                causes[causeDist(gen)]
            );
            dispatcher.enqueue(event);
        } else {
            // Enqueue PlayerLevelUpEvent
            PlayerLevelUpEvent event(
                playerDist(gen),
                levelDist(gen),
                levelDist(gen) * 100
            );
            dispatcher.enqueue(event);
        }
        
        // Small delay to simulate real work
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "Worker thread " << threadId << " finished\n";
}

int main() {
    std::cout << "=== EventCore High-Performance Event System Demo ===\n\n";
    
    // Create the event dispatcher
    EventCore::EventDispatcher dispatcher;
    
    std::cout << "1. Creating listeners and subscribing to events...\n";
    
    // Create shared listener objects
    auto gameManager = std::make_shared<GameManager>();
    auto audioSystem = std::make_shared<AudioSystem>();
    auto player1 = std::make_shared<Player>(1);
    auto player2 = std::make_shared<Player>(2);
    
    // Subscribe listeners to events
    dispatcher.subscribe<PlayerDiedEvent>(gameManager, &GameManager::on_player_died);
    dispatcher.subscribe<PlayerLevelUpEvent>(gameManager, &GameManager::on_level_up);
    dispatcher.subscribe<GameStateChangeEvent>(gameManager, &GameManager::on_game_state_change);
    
    dispatcher.subscribe<PlayerDiedEvent>(audioSystem, &AudioSystem::on_player_died);
    dispatcher.subscribe<PlayerLevelUpEvent>(audioSystem, &AudioSystem::on_level_up);
    
    dispatcher.subscribe<PlayerDiedEvent>(player1, &Player::on_player_died);
    dispatcher.subscribe<PlayerLevelUpEvent>(player1, &Player::on_level_up);
    
    dispatcher.subscribe<PlayerDiedEvent>(player2, &Player::on_player_died);
    dispatcher.subscribe<PlayerLevelUpEvent>(player2, &Player::on_level_up);
    
    print_stats(dispatcher);
    
    std::cout << "2. Testing immediate dispatch...\n";
    
    // Test immediate dispatch
    PlayerDiedEvent deathEvent(1, 85.5f, "Dragon");
    dispatcher.dispatch(deathEvent);
    
    PlayerLevelUpEvent levelEvent(2, 15, 1500);
    dispatcher.dispatch(levelEvent);
    
    GameStateChangeEvent stateEvent(GameStateChangeEvent::State::Playing);
    dispatcher.dispatch(stateEvent);
    
    print_stats(dispatcher);
    
    std::cout << "3. Testing deferred dispatch with multiple worker threads...\n";
    
    // Start multiple worker threads to enqueue events
    std::vector<std::thread> workers;
    const int numThreads = 3;
    const int eventsPerThread = 5;
    
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back(worker_thread, std::ref(dispatcher), i, eventsPerThread);
    }
    
    // Wait for all workers to finish
    for (auto& worker : workers) {
        worker.join();
    }
    
    print_stats(dispatcher);
    
    std::cout << "4. Processing queued events...\n";
    
    // Process all queued events
    auto processedCount = dispatcher.process_queued_events();
    std::cout << "Processed " << processedCount << " queued events\n";
    
    print_stats(dispatcher);
    
    std::cout << "5. Testing listener lifetime management...\n";
    
    // Create a temporary listener to demonstrate cleanup
    {
        auto tempPlayer = std::make_shared<Player>(99);
        dispatcher.subscribe<PlayerDiedEvent>(tempPlayer, &Player::on_player_died);
        
        std::cout << "Added temporary player (Player 99)\n";
        print_stats(dispatcher);
        
        // Dispatch an event while temporary player exists
        PlayerDiedEvent tempEvent(99, 50.0f, "Test");
        dispatcher.dispatch(tempEvent);
        
        // tempPlayer will be destroyed when scope ends
    }
    
    std::cout << "Temporary player destroyed (should trigger automatic cleanup)\n";
    
    // Dispatch another event to trigger cleanup
    PlayerDiedEvent cleanupEvent(1, 25.0f, "Cleanup Test");
    dispatcher.dispatch(cleanupEvent);
    
    print_stats(dispatcher);
    
    std::cout << "6. Manual cleanup of expired listeners...\n";
    
    auto cleanedCount = dispatcher.cleanup_expired_listeners();
    std::cout << "Manually cleaned " << cleanedCount << " expired listeners\n";
    
    print_stats(dispatcher);
    
    std::cout << "7. Performance test with rapid dispatch...\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    const int rapidEvents = 10000;
    
    for (int i = 0; i < rapidEvents; ++i) {
        PlayerLevelUpEvent rapidEvent(i % 3 + 1, i % 50 + 1, (i % 50 + 1) * 100);
        dispatcher.dispatch(rapidEvent);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Dispatched " << rapidEvents << " events in " 
              << duration.count() << " microseconds\n";
    std::cout << "Average: " << (duration.count() / rapidEvents) 
              << " microseconds per event\n";
    
    print_stats(dispatcher);
    
    std::cout << "8. Testing unsubscription...\n";
    
    // Unsubscribe player1 from death events
    dispatcher.unsubscribe<PlayerDiedEvent>(player1.get(), &Player::on_player_died);
    std::cout << "Unsubscribed Player 1 from PlayerDiedEvent\n";
    
    // Test that player1 no longer receives death events
    PlayerDiedEvent unsubTestEvent(1, 30.0f, "Unsubscribe Test");
    dispatcher.dispatch(unsubTestEvent);
    
    print_stats(dispatcher);
    
    std::cout << "9. Final statistics and cleanup...\n";
    
    // Show compile-time event type IDs
    std::cout << "Event Type IDs (compile-time generated):\n";
    std::cout << "PlayerDiedEvent: " << EventCore::get_event_type_id<PlayerDiedEvent>() << "\n";
    std::cout << "PlayerLevelUpEvent: " << EventCore::get_event_type_id<PlayerLevelUpEvent>() << "\n";
    std::cout << "GameStateChangeEvent: " << EventCore::get_event_type_id<GameStateChangeEvent>() << "\n";
    
    print_stats(dispatcher);
    
    std::cout << "Demo completed successfully!\n";
    std::cout << "All listeners will be automatically cleaned up when objects are destroyed.\n";
    
    return 0;
} 