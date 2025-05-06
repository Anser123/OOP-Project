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
#include <string> // Needed for std::to_string

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
    uintptr_t id = 0; // To identify platform bodies during raycast
};

struct Collectible {
    sf::CircleShape shape;
    b2Body* body = nullptr;
    bool markedForRemoval = false;
};

// Identifiers for user data
static constexpr uintptr_t FOOT_SENSOR_ID = 1;
static constexpr uintptr_t GROUND_ID = 2;
static constexpr uintptr_t CEILING_ID = 3;
static constexpr uintptr_t COLLECTIBLE_ID = 4;
static constexpr uintptr_t PLATFORM_ID_BASE = 1000; // Base ID for platforms

// Contact listener to detect when the player touches a platform, the ground, or a collectible
class PlayerContactListener : public b2ContactListener {
public:
    int footContacts = 0;
    bool touchedGround = false;
    std::vector<b2Body*>& collectiblesToRemove; // Reference to bodies to remove
    int& scoreRef; // Reference to the score

    PlayerContactListener(std::vector<b2Body*>& bodiesToRemove, int& score)
        : collectiblesToRemove(bodiesToRemove), scoreRef(score) {}

    void BeginContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // Count foot sensor contacts (platform contacts)
        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) footContacts++;
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE) footContacts++;

        // If the player touches the ground (the invisible floor), mark game over.
        if ((userDataA != FOOT_SENSOR_ID && userDataB == GROUND_ID) ||
            (userDataB != FOOT_SENSOR_ID && userDataA == GROUND_ID)) {
            touchedGround = true;
        }

        // Check for collectible contact
        if (userDataA == COLLECTIBLE_ID) {
            if (userDataB != FOOT_SENSOR_ID && userDataB != GROUND_ID && userDataB != CEILING_ID && userDataB < PLATFORM_ID_BASE) { // Ensure it's the player body
                 collectiblesToRemove.push_back(fixtureA->GetBody());
                 scoreRef++;
            }
        } else if (userDataB == COLLECTIBLE_ID) {
             if (userDataA != FOOT_SENSOR_ID && userDataA != GROUND_ID && userDataA != CEILING_ID && userDataA < PLATFORM_ID_BASE) { // Ensure it's the player body
                 collectiblesToRemove.push_back(fixtureB->GetBody());
                 scoreRef++;
             }
        }
    }

    void EndContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE && footContacts > 0) footContacts--;
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE && footContacts > 0) footContacts--;
    }

    bool isGrounded() const {
        return footContacts > 0;
    }
};

// RayCast callback to find the closest platform below the player for the snap mechanic
class SnapRayCastCallback : public b2RayCastCallback {
public:
    b2Fixture* closestFixture = nullptr;
    b2Vec2 hitPoint;
    float closestFraction = 1.0f; // Start with max fraction

    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        uintptr_t userData = fixture->GetUserData().pointer;
        // Check if it's a platform fixture (using the base ID)
        if (userData >= PLATFORM_ID_BASE) {
            if (fraction < closestFraction) {
                closestFraction = fraction;
                closestFixture = fixture;
                hitPoint = point;
            }
            return fraction; // Continue searching for closer fixtures
        }
        return -1.0f; // Ignore this fixture (player, ground, ceiling, collectibles, etc.)
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
    const float collectibleRadius = 15.f;
    const float collectibleSpawnChance = 0.3f; // 30% chance to spawn with a platform

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rat Rider - Box2D");
    window.setFramerateLimit(60);

    // --- Box2D Setup ---
    b2Vec2 gravity(0.0f, 5.0f);
    b2World world(gravity);
    std::vector<b2Body*> collectiblesToRemoveFromWorld; // List to hold bodies pending removal
    int score = 0;
    PlayerContactListener contactListener(collectiblesToRemoveFromWorld, score);
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
    playerShape.setFillColor(sf::Color(137,1,56));
    playerShape.setOrigin(playerWidth / 2.f, playerHeight / 2.f);

    b2BodyDef playerBodyDef;
    playerBodyDef.type = b2_dynamicBody;
    playerBodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f, windowHeight - 200.f));
    playerBodyDef.fixedRotation = true;
    playerBodyDef.allowSleep = false;
    b2Body* playerBody = world.CreateBody(&playerBodyDef);

    b2PolygonShape playerBox;
    playerBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x,
                       toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);

    b2FixtureDef playerFixtureDef;
    playerFixtureDef.shape = &playerBox;
    playerFixtureDef.density = 1.0f;
    playerFixtureDef.friction = 0.5f;
    playerFixtureDef.restitution = 0.0f;
    // Assign a low ID to distinguish player body from platforms during raycast
    playerFixtureDef.userData.pointer = 0;
    playerBody->CreateFixture(&playerFixtureDef);

    // Player foot sensor
    b2PolygonShape footSensorBox;
    b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
    footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x,
                           toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y,
                           footSensorCenter, 0);

    b2FixtureDef footSensorFixtureDef;
    footSensorFixtureDef.shape = &footSensorBox;
    footSensorFixtureDef.isSensor = true;
    footSensorFixtureDef.userData.pointer = FOOT_SENSOR_ID;
    playerBody->CreateFixture(&footSensorFixtureDef);

    int jumpsRemaining = maxJumps;
    bool jumpKeyPressed = false;
    bool snapKeyPressed = false; // For the 'S' key snap

    // --- Ground ---
    b2BodyDef groundBodyDef;
    groundBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).x,
                               toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).y);
    b2Body* groundBody = world.CreateBody(&groundBodyDef);
    b2PolygonShape groundBox;
    groundBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x,
                       toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* groundFixture = groundBody->CreateFixture(&groundBox, 0.0f);
    groundFixture->GetUserData().pointer = GROUND_ID;

    // --- Ceiling (Upper Boundary) ---
    b2BodyDef ceilingBodyDef;
    // Position slightly above the screen
    ceilingBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).x,
                                toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).y);
    b2Body* ceilingBody = world.CreateBody(&ceilingBodyDef);
    b2PolygonShape ceilingBox;
    ceilingBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x,
                        toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* ceilingFixture = ceilingBody->CreateFixture(&ceilingBox, 0.0f); // Static body
    ceilingFixture->GetUserData().pointer = CEILING_ID; // Mark as ceiling

    // --- Platform and Collectible Spawning ---
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lengthDist(minLength, maxLength);
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> collectibleChanceDist(0.0f, 1.0f); // For collectible spawn chance

    std::vector<Block> blocks;
    std::vector<Collectible> collectibles;
    uintptr_t nextPlatformId = PLATFORM_ID_BASE; // Counter for unique platform IDs

    sf::Clock spawnClock;
    float nextSpawnTime = initialSpawnTimeDist(gen);
    sf::Clock deltaClock;
    float gameTime = 0.f;

    enum GameState { Playing, GameOver };
    GameState currentState = Playing;
    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) {
        std::cerr << "Error loading font arial.ttf" << std::endl;
        return 1;
    }
    sf::Text gameOverText("Game Over!", font, 50);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setStyle(sf::Text::Bold);
    sf::FloatRect textRect = gameOverText.getLocalBounds();
    gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
    gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));

    sf::Text scoreText("Score: 0", font, 30);
    scoreText.setFillColor(sf::Color::White);
    scoreText.setPosition(10.f, 10.f);


    // --- Main Loop ---
    while (window.isOpen()) {
        // --- Event Handling ---
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (currentState == Playing) {
                 if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::W && !jumpKeyPressed) {
                        if (jumpsRemaining > 0) {
                            float impulse = playerBody->GetMass() * sqrt(2.0f * std::abs(gravity.y) * (playerJumpForce * METERS_PER_PIXEL));
                            playerBody->SetLinearVelocity(b2Vec2(playerBody->GetLinearVelocity().x, 0));
                            playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulse), true);
                            jumpsRemaining--;
                            jumpKeyPressed = true;
                        }
                    } else if (event.key.code == sf::Keyboard::S && !snapKeyPressed) {
                        snapKeyPressed = true; // Set flag to attempt snap in the update phase
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == sf::Keyboard::W) {
                        jumpKeyPressed = false;
                    }
                    // Note: snapKeyPressed is reset after the attempt in the update logic
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();
        if (currentState == GameOver) {
            dt = 0; // Freeze physics
        } else {
            gameTime += dt;
        }

        // --- Game Logic Update ---
        if (currentState == Playing) {
            // Increase speed and adjust spawn times
            if (blockSpeed < maxBlockSpeed) {
                blockSpeed += blockSpeedIncreaseFactor * dt;
                blockSpeed = std::min(blockSpeed, maxBlockSpeed);
                float speedRatio = (blockSpeed - 200.f) / (maxBlockSpeed - 200.f);
                minSpawnTime = lerp(2.5f, minSpawnTimeBase, speedRatio);
                maxSpawnTime = lerp(3.5f, maxSpawnTimeBase, speedRatio);
            }
            std::uniform_real_distribution<float> currentSpawnTimeDist(minSpawnTime, maxSpawnTime);

            // Reset jumps if grounded
            if (contactListener.isGrounded()) {
                jumpsRemaining = maxJumps;
            }

            // --- Snap Logic ('S' Key) ---
            if (snapKeyPressed && !contactListener.isGrounded()) {
                SnapRayCastCallback callback;
                b2Vec2 playerPos = playerBody->GetPosition();
                // Raycast slightly below the player's feet down a significant distance
                b2Vec2 p1 = playerPos + b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f + 1.f)).y); // Start just below feet
                b2Vec2 p2 = p1 + b2Vec2(0, toMeters(sf::Vector2f(0, windowHeight)).y); // Raycast down screen height

                world.RayCast(&callback, p1, p2);

                if (callback.closestFixture) {
                    // Snap player to the hit point (adjusting for player height)
                    b2Vec2 targetPos = callback.hitPoint - b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                    playerBody->SetTransform(b2Vec2(playerPos.x, targetPos.y), 0); // Keep current X
                    playerBody->SetLinearVelocity(b2Vec2(0, 0)); // Stop vertical movement
                    jumpsRemaining = maxJumps; // Reset jumps after snapping
                }
                snapKeyPressed = false; // Reset flag after attempting snap
            }


            // --- Spawn new blocks (platforms) and collectibles ---
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
                newBlock.id = nextPlatformId++; // Assign unique ID

                b2PolygonShape blockBox;
                blockBox.SetAsBox(toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).x,
                                  toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).y);

                b2FixtureDef blockFixtureDef;
                blockFixtureDef.shape = &blockBox;
                blockFixtureDef.friction = 0.7f;
                blockFixtureDef.userData.pointer = newBlock.id; // Use unique ID for fixture data
                newBlock.body->CreateFixture(&blockFixtureDef);

                newBlock.body->SetLinearVelocity(b2Vec2(toMeters(sf::Vector2f(-blockSpeed, 0.f)).x, 0.0f));

                // Check for visual overlap
                bool visualOverlap = false;
                sf::FloatRect candidateBounds = newBlock.shape.getGlobalBounds();
                candidateBounds.left -= 50; candidateBounds.width += 100; // Buffer
                for (const auto& block : blocks) {
                    if (!block.markedForRemoval && candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        visualOverlap = true;
                        break;
                    }
                }

                if (!visualOverlap) {
                    blocks.push_back(newBlock);

                    // --- Spawn Collectible ---
                    if (collectibleChanceDist(gen) < collectibleSpawnChance) {
                        Collectible newCollectible;
                        // Position collectible above the center of the new platform
                        sf::Vector2f collectiblePos = spawnPos;
                        collectiblePos.y -= (fixedHeight / 2.f + collectibleRadius + 5.f); // 5px gap

                        newCollectible.shape.setRadius(collectibleRadius);
                        newCollectible.shape.setFillColor(sf::Color::Magenta);
                        newCollectible.shape.setOutlineColor(sf::Color::Black);
                        newCollectible.shape.setOutlineThickness(1.5f);
                        newCollectible.shape.setOrigin(collectibleRadius, collectibleRadius);
                        newCollectible.shape.setPosition(collectiblePos);

                        b2BodyDef collectibleBodyDef;
                        collectibleBodyDef.type = b2_kinematicBody; // Move with platform
                        collectibleBodyDef.position = toMeters(collectiblePos);
                        newCollectible.body = world.CreateBody(&collectibleBodyDef);

                        b2CircleShape collectibleCircle;
                        collectibleCircle.m_radius = toMeters(sf::Vector2f(collectibleRadius, 0)).x;

                        b2FixtureDef collectibleFixtureDef;
                        collectibleFixtureDef.shape = &collectibleCircle;
                        collectibleFixtureDef.isSensor = true; // Detect collision without physical reaction
                        collectibleFixtureDef.userData.pointer = COLLECTIBLE_ID;
                        newCollectible.body->CreateFixture(&collectibleFixtureDef);

                        // Match collectible velocity to the platform it's on
                        newCollectible.body->SetLinearVelocity(newBlock.body->GetLinearVelocity());

                        collectibles.push_back(newCollectible);
                    }

                } else {
                    world.DestroyBody(newBlock.body); // Clean up body if not added
                }

                spawnClock.restart();
                nextSpawnTime = currentSpawnTimeDist(gen);
            }

            // --- Physics Step ---
            int32 velocityIterations = 6;
            int32 positionIterations = 2;
            world.Step(dt, velocityIterations, positionIterations);

            // --- Remove Collectibles Marked by Contact Listener ---
            for (b2Body* bodyToDestroy : collectiblesToRemoveFromWorld) {
                 // Find the collectible associated with this body and mark it
                 for (auto& collectible : collectibles) {
                     if (collectible.body == bodyToDestroy) {
                         collectible.markedForRemoval = true;
                         break; // Found it
                     }
                 }
                 world.DestroyBody(bodyToDestroy); // Destroy the Box2D body
            }
            collectiblesToRemoveFromWorld.clear(); // Clear the list for the next frame

            // --- Update Game Objects from Physics ---
            // Update Player
            playerShape.setPosition(toPixels(playerBody->GetPosition()));
            playerBody->SetLinearVelocity(b2Vec2(0.0f, playerBody->GetLinearVelocity().y)); // Prevent horizontal drift

            // Update Platforms (Blocks)
            for (auto& block : blocks) {
                if (block.body) {
                    block.shape.setPosition(toPixels(block.body->GetPosition()));
                    block.line.setPosition(
                        block.shape.getPosition().x,
                        block.shape.getPosition().y + (fixedHeight / 2.f)
                    );
                    if (block.shape.getPosition().x + block.shape.getSize().x / 2.f < -50.f) {
                        block.markedForRemoval = true;
                    }
                }
            }

            // Update Collectibles
            for (auto& collectible : collectibles) {
                if (collectible.body && !collectible.markedForRemoval) {
                    collectible.shape.setPosition(toPixels(collectible.body->GetPosition()));
                    // Also mark for removal if they go off-screen
                    if (collectible.shape.getPosition().x + collectibleRadius < -50.f) {
                         collectible.markedForRemoval = true;
                         // Ensure body is destroyed if it wasn't collected
                         if (collectible.body) {
                             world.DestroyBody(collectible.body);
                             collectible.body = nullptr;
                         }
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

            // --- Remove Marked Collectibles (Visuals) ---
             collectibles.erase(std::remove_if(collectibles.begin(), collectibles.end(),
                                             [](const Collectible& c){ return c.markedForRemoval; }),
                              collectibles.end());


            // --- Check Game Over Conditions ---
            if (playerShape.getPosition().y > windowHeight + playerHeight || contactListener.touchedGround) {
                currentState = GameOver;
            }

            // Update Score Text
            scoreText.setString("Score: " + std::to_string(score));
        }

        // --- Rendering ---
        window.clear();
        window.draw(backgroundSprite);

        // Draw platforms and lines
        for (const auto& block : blocks) {
             window.draw(block.line);
             window.draw(block.shape);
        }

        // Draw collectibles
        for (const auto& collectible : collectibles) {
             if (!collectible.markedForRemoval) { // Only draw if not marked
                 window.draw(collectible.shape);
             }
        }

        // Draw player
        window.draw(playerShape);

        // Draw Score
        window.draw(scoreText);

        // Draw Game Over text
        if (currentState == GameOver) {
            window.draw(gameOverText);
        }

        window.display();
    }

    // Clean up remaining Box2D bodies (optional but good practice)
    // Note: world destructor should handle this, but explicit deletion can be clearer
    // for (auto& block : blocks) if (block.body) world.DestroyBody(block.body);
    // for (auto& collectible : collectibles) if (collectible.body) world.DestroyBody(collectible.body);
    // if (playerBody) world.DestroyBody(playerBody);
    // if (groundBody) world.DestroyBody(groundBody);
    // if (ceilingBody) world.DestroyBody(ceilingBody);


    return 0;
}
