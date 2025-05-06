#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <random>
#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Box2D works best with meters, SFML uses pixels. Define a conversion factor.
const float PIXELS_PER_METER = 30.0f;
const float METERS_PER_PIXEL = 1.0f / PIXELS_PER_METER;

// Convert Box2D meters to SFML pixels
sf::Vector2f toPixels(const b2Vec2& vec) {
    return sf::Vector2f(vec.x * PIXELS_PER_METER, vec.y * PIXELS_PER_METER);
}

// Convert SFML pixels to Box2D meters
b2Vec2 toMeters(const sf::Vector2f& vec) {
    return b2Vec2(vec.x * METERS_PER_PIXEL, vec.y * METERS_PER_PIXEL);
}

// Convert Box2D angle (radians) to SFML angle (degrees)
float toDegrees(float radians) {
    return radians * 180.0f / b2_pi;
}

struct Block {
    sf::RectangleShape shape;
    sf::RectangleShape line; // vertical line (visual only)
    b2Body* body = nullptr;
    bool markedForRemoval = false;
};

// Identifiers for user data
static constexpr uintptr_t FOOT_SENSOR_ID = 1;
static constexpr uintptr_t GROUND_ID = 2;

// Contact listener to detect when the player touches a platform or the dangerous ground
class PlayerContactListener : public b2ContactListener {
public:
    int footContacts = 0;
    bool touchedGround = false;
    
    void BeginContact(b2Contact* contact) override {
        uintptr_t userDataA = contact->GetFixtureA()->GetUserData().pointer;
        uintptr_t userDataB = contact->GetFixtureB()->GetUserData().pointer;
        
        // Count foot sensor contacts (platform contacts)
        if (userDataA == FOOT_SENSOR_ID) footContacts++;
        if (userDataB == FOOT_SENSOR_ID) footContacts++;
        
        // If the player touches the ground (the invisible floor), mark game over.
        if (userDataA == GROUND_ID || userDataB == GROUND_ID) {
            touchedGround = true;
        }
    }

    void EndContact(b2Contact* contact) override {
        uintptr_t userDataA = contact->GetFixtureA()->GetUserData().pointer;
        uintptr_t userDataB = contact->GetFixtureB()->GetUserData().pointer;

        if (userDataA == FOOT_SENSOR_ID && footContacts > 0) footContacts--;
        if (userDataB == FOOT_SENSOR_ID && footContacts > 0) footContacts--;
    }

    bool isGrounded() const {
        return footContacts > 0;
    }
};

int main() {
    // Window settings
    const unsigned int windowWidth = 1200;
    const unsigned int windowHeight = 700;
    const float fixedHeight = 20.f;  // Block height

    const float minLength = 100.f;
    const float maxLength = 300.f;
    float blockSpeed = 200.f; // Initial speed, will increase
    const float blockSpeedIncreaseFactor = 5.0f; // Speed increase per second
    const float maxBlockSpeed = 600.f;

    float minSpawnTime = 2.5f;
    float maxSpawnTime = 3.5f;
    const float minSpawnTimeBase = 0.8f; // Minimum possible spawn time at max speed
    const float maxSpawnTimeBase = 1.5f; // Minimum possible max spawn time

    const float playerWidth = 40.f;
    const float playerHeight = 60.f;
    const float playerJumpForce = 450.0f;
    const int maxJumps = 2;

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rat Rider - Box2D");
    window.setFramerateLimit(60);

    // --- Box2D Setup ---
    // Reduced gravity for a floatier jump feeling
    b2Vec2 gravity(0.0f, 9.8f);
    b2World world(gravity);
    PlayerContactListener contactListener;
    world.SetContactListener(&contactListener);

    // --- Background ---
    sf::Texture backgroundTexture;
    if (!backgroundTexture.loadFromFile("silhouette.jpg")) {
        std::cerr << "Error loading background image 'silhouette.jpg'" << std::endl;
        return 1;
    }
    sf::Sprite backgroundSprite(backgroundTexture);
    backgroundSprite.setScale(
         static_cast<float>(windowWidth) / backgroundTexture.getSize().x,
         static_cast<float>(windowHeight) / backgroundTexture.getSize().y
    );

    // --- Player Setup ---
    sf::RectangleShape playerShape(sf::Vector2f(playerWidth, playerHeight));
    playerShape.setFillColor(sf::Color::Magenta);
    playerShape.setOrigin(playerWidth / 2.f, playerHeight / 2.f);

    b2BodyDef playerBodyDef;
    playerBodyDef.type = b2_dynamicBody;
    // Start position (center screen horizontally, slightly above bottom)
    playerBodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f, windowHeight - 200.f));
    playerBodyDef.fixedRotation = true; // Prevent rotation
    playerBodyDef.allowSleep = false;
    b2Body* playerBody = world.CreateBody(&playerBodyDef);

    b2PolygonShape playerBox;
    playerBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x,
                       toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);

    b2FixtureDef playerFixtureDef;
    playerFixtureDef.shape = &playerBox;
    playerFixtureDef.density = 1.0f;
    playerFixtureDef.friction = 0.5f; // Friction when on platforms
    playerFixtureDef.restitution = 0.0f; // No bounce
    playerBody->CreateFixture(&playerFixtureDef);

    // Player foot sensor for ground check (for platforms only)
    b2PolygonShape footSensorBox;
    b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
    footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x, // Slightly narrower, small height
                           toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y,
                           footSensorCenter, // Offset to bottom
                           0); // No rotation

    b2FixtureDef footSensorFixtureDef;
    footSensorFixtureDef.shape = &footSensorBox;
    footSensorFixtureDef.isSensor = true; // Sensor only, detects collision without physical response
    footSensorFixtureDef.userData.pointer = FOOT_SENSOR_ID; // Identify the foot sensor
    playerBody->CreateFixture(&footSensorFixtureDef);

    int jumpsRemaining = maxJumps;
    bool jumpKeyPressed = false;

    // --- Ground (Invisible floor to catch player if they miss a platform) ---
    b2BodyDef groundBodyDef;
    // Position slightly below the screen
    groundBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).x,
                               toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).y);
    b2Body* groundBody = world.CreateBody(&groundBodyDef);

    b2PolygonShape groundBox;
    groundBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x,
                       toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* groundFixture = groundBody->CreateFixture(&groundBox, 0.0f); // Static body
    groundFixture->GetUserData().pointer = GROUND_ID;  // Mark as ground

    // --- Platform Spawning ---
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lengthDist(minLength, maxLength);
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f); // Spawn first few faster

    std::vector<Block> blocks;
    sf::Clock spawnClock;
    float nextSpawnTime = initialSpawnTimeDist(gen); // Start spawning quickly
    sf::Clock deltaClock;
    float gameTime = 0.f;

    enum GameState { Playing, GameOver };
    GameState currentState = Playing;
    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) { // Ensure arial.ttf or another font file is available.
        std::cerr << "Error loading font arial.ttf" << std::endl;
        return 1;
    }
    sf::Text gameOverText("Game Over!", font, 50);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setStyle(sf::Text::Bold);
    sf::FloatRect textRect = gameOverText.getLocalBounds();
    gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
    gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));

    // --- Main Loop ---
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (currentState == Playing && event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::W && !jumpKeyPressed) {
                    if (jumpsRemaining > 0) {
                        // Apply impulse for jump
                        float impulse = playerBody->GetMass() * sqrt(2.0f * std::abs(gravity.y) * (playerJumpForce * METERS_PER_PIXEL));
                        // Reset vertical velocity for consistent jump height
                        playerBody->SetLinearVelocity(b2Vec2(playerBody->GetLinearVelocity().x, 0));
                        playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulse), true);
                        jumpsRemaining--;
                        jumpKeyPressed = true; // Prevent holding W for continuous jumps
                    }
                }
            }
            if (event.type == sf::Event::KeyReleased) {
                if (event.key.code == sf::Keyboard::W) {
                    jumpKeyPressed = false;
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();
        if (currentState == GameOver) {
            dt = 0; // Freeze physics on game over
        } else {
            gameTime += dt;
        }

        // --- Game Logic Update ---
        if (currentState == Playing) {
            // Increase speed and adjust spawn times over time
            if (blockSpeed < maxBlockSpeed) {
                blockSpeed += blockSpeedIncreaseFactor * dt;
                blockSpeed = std::min(blockSpeed, maxBlockSpeed);

                // Adjust spawn times based on speed (faster speed -> shorter spawn intervals)
                float speedRatio = (blockSpeed - 200.f) / (maxBlockSpeed - 200.f); // Normalize speed progress
                minSpawnTime = lerp(2.5f, minSpawnTimeBase, speedRatio);
                maxSpawnTime = lerp(3.5f, maxSpawnTimeBase, speedRatio);
            }
            std::uniform_real_distribution<float> currentSpawnTimeDist(minSpawnTime, maxSpawnTime);

            // Reset jumps if player is grounded on a platform (not the ground floor)
            if (contactListener.isGrounded()) {
                jumpsRemaining = maxJumps;
            }

            // Spawn new blocks (platforms)
            if (spawnClock.getElapsedTime().asSeconds() >= nextSpawnTime) {
                Block newBlock;
                float blockLength = lengthDist(gen);
                float spawnY = yPosDist(gen);
                sf::Vector2f spawnPos(windowWidth + blockLength / 2.f, spawnY);

                // Create SFML shape
                newBlock.shape.setSize(sf::Vector2f(blockLength, fixedHeight));
                newBlock.shape.setFillColor(sf::Color(255, 200, 0));
                newBlock.shape.setOutlineColor(sf::Color::Black);
                newBlock.shape.setOutlineThickness(2.5f);
                newBlock.shape.setOrigin(blockLength / 2.f, fixedHeight / 2.f);
                newBlock.shape.setPosition(spawnPos);

                // Create visual line
                newBlock.line.setSize(sf::Vector2f(15.f, 500));
                newBlock.line.setFillColor(sf::Color(150,150,150));
                newBlock.line.setOutlineColor(sf::Color::Black);
                newBlock.line.setOutlineThickness(2.5f);
                newBlock.line.setOrigin(7.5f, 0.f);
                newBlock.line.setPosition(spawnPos.x, spawnPos.y + fixedHeight / 2.f);

                // Create Box2D body for the platform
                b2BodyDef blockBodyDef;
                blockBodyDef.type = b2_kinematicBody;
                blockBodyDef.position = toMeters(spawnPos);
                newBlock.body = world.CreateBody(&blockBodyDef);

                b2PolygonShape blockBox;
                blockBox.SetAsBox(toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).x,
                                  toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).y);

                b2FixtureDef blockFixtureDef;
                blockFixtureDef.shape = &blockBox;
                blockFixtureDef.friction = 0.7f;
                newBlock.body->CreateFixture(&blockFixtureDef);

                // Set velocity for moving platform
                newBlock.body->SetLinearVelocity(b2Vec2(toMeters(sf::Vector2f(-blockSpeed, 0.f)).x, 0.0f));

                // Check for visual overlap with existing blocks (with a buffer)
                bool visualOverlap = false;
                sf::FloatRect candidateBounds = newBlock.shape.getGlobalBounds();
                candidateBounds.left -= 50;
                candidateBounds.width += 100;
                for (const auto& block : blocks) {
                    if (!block.markedForRemoval && candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        visualOverlap = true;
                        break;
                    }
                }

                if (!visualOverlap) {
                    blocks.push_back(newBlock);
                } else {
                    world.DestroyBody(newBlock.body);
                }

                spawnClock.restart();
                nextSpawnTime = currentSpawnTimeDist(gen);
            }

            // --- Physics Step ---
            int32 velocityIterations = 6;
            int32 positionIterations = 2;
            world.Step(dt, velocityIterations, positionIterations);

            // --- Update Game Objects from Physics ---
            // Update Player
            playerShape.setPosition(toPixels(playerBody->GetPosition()));
            // Fix player's horizontal velocity to 0
            playerBody->SetLinearVelocity(b2Vec2(0.0f, playerBody->GetLinearVelocity().y));

            // Update Platforms (Blocks)
            for (auto& block : blocks) {
                if (block.body) {
                    block.shape.setPosition(toPixels(block.body->GetPosition()));
                    block.line.setPosition(
                        block.shape.getPosition().x,
                        block.shape.getPosition().y + (fixedHeight / 2.f)
                    );

                    // Mark platforms off-screen for removal
                    if (block.shape.getPosition().x + block.shape.getSize().x / 2.f < -50.f) {
                        block.markedForRemoval = true;
                    }
                }
            }

            // --- Remove Marked Platforms ---
            for (auto& block : blocks) {
                if (block.markedForRemoval && block.body) {
                    world.DestroyBody(block.body);
                    block.body = nullptr;
                }
            }
            blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                        [](const Block& b){ return b.markedForRemoval; }),
                         blocks.end());

            // --- Check Game Over Conditions ---
            // If player falls off the bottom or lands on the ground floor (i.e., misses platforms)
            if (playerShape.getPosition().y > windowHeight + playerHeight || contactListener.touchedGround) {
                currentState = GameOver;
            }
        }

        // --- Rendering ---
        window.clear();
        window.draw(backgroundSprite);

        // Draw platforms and their vertical lines
        for (const auto& block : blocks) {
             window.draw(block.line);
             window.draw(block.shape);
        }

        // Draw player
        window.draw(playerShape);

        // Draw Game Over text if applicable
        if (currentState == GameOver) {
            window.draw(gameOverText);
        }

        window.display();
    }

    return 0;
}
