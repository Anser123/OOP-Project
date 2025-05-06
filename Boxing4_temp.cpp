#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Audio.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <random>
#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>
#include <string>

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
    sf::Sound& collectSoundRef; // Reference to the collect sound

    // NEW: Store the platform body the player is standing on
    b2Body* currentPlatformBody = nullptr;

    PlayerContactListener(std::vector<b2Body*>& bodiesToRemove, int& score, sf::Sound& collectSound)
        : collectiblesToRemove(bodiesToRemove), scoreRef(score), collectSoundRef(collectSound) {}

    void BeginContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // Platform contact logic
        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) {
            footContacts++;
            currentPlatformBody = fixtureB->GetBody(); // Track the platform body
        }
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE) {
            footContacts++;
            currentPlatformBody = fixtureA->GetBody(); // Track the platform body
        }

        // Ground logic
        if ((userDataA != FOOT_SENSOR_ID && userDataB == GROUND_ID) ||
            (userDataB != FOOT_SENSOR_ID && userDataA == GROUND_ID)) {
            touchedGround = true;
        }

        // Collectible logic
        if (userDataA == COLLECTIBLE_ID) {
            if (userDataB != FOOT_SENSOR_ID && userDataB != GROUND_ID && userDataB != CEILING_ID && userDataB < PLATFORM_ID_BASE) {
                collectiblesToRemove.push_back(fixtureA->GetBody());
                scoreRef++;
                collectSoundRef.play(); // Play sound on collect
            }
        } else if (userDataB == COLLECTIBLE_ID) {
            if (userDataA != FOOT_SENSOR_ID && userDataA != GROUND_ID && userDataA != CEILING_ID && userDataA < PLATFORM_ID_BASE) {
                collectiblesToRemove.push_back(fixtureB->GetBody());
                scoreRef++;
                collectSoundRef.play(); // Play sound on collect
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

        // If the player leaves the platform, stop tracking the platform
        if (footContacts == 0) {
            currentPlatformBody = nullptr;
        }
    }

    // Override PreSolve to adjust friction for player-platform contacts.
    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // If one fixture is a platform and the other is the player's main fixture (player id is 0)
        if ((userDataA >= PLATFORM_ID_BASE && userDataB == 0) ||
            (userDataB >= PLATFORM_ID_BASE && userDataA == 0)) {
            contact->SetFriction(0.0f);
        }
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
        return -1.0f; // Ignore other fixtures
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

    // --- Load Assets ---
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

    sf::Texture staticPlayerTexture;
    if (!staticPlayerTexture.loadFromFile("Idle.png")) {
        std::cerr << "Error loading texture 'Idle.png'" << std::endl;
        return 1;
    }
    sf::Texture jumpPlayerTexture;
    if (!jumpPlayerTexture.loadFromFile("Jump.png")) {
        std::cerr << "Error loading texture 'Jump.png'" << std::endl;
        return 1;
    }

    sf::SoundBuffer collectBuffer;
    if (!collectBuffer.loadFromFile("collectible.wav")) {
        std::cerr << "Error loading sound 'collectible.wav'" << std::endl;
        return 1;
    }
    sf::Sound collectSound;
    collectSound.setBuffer(collectBuffer);

    sf::Music backgroundMusic;
    if (!backgroundMusic.openFromFile("background.ogg")) {
        std::cerr << "Error loading music 'background.ogg'" << std::endl;
    } else {
        backgroundMusic.setLoop(true);
        backgroundMusic.setVolume(50);
        backgroundMusic.play();
    }

    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) {
        std::cerr << "Error loading font arial.ttf" << std::endl;
        return 1;
    }

    // --- Box2D Setup ---
    b2Vec2 gravity(0.0f, 5.0f);
    b2World world(gravity);
    std::vector<b2Body*> collectiblesToRemoveFromWorld; // List to hold bodies pending removal
    int score = 0;
    PlayerContactListener contactListener(collectiblesToRemoveFromWorld, score, collectSound);
    world.SetContactListener(&contactListener);

    // --- Player Setup ---
    sf::Sprite playerSprite;
    playerSprite.setTexture(staticPlayerTexture);
    playerSprite.setScale(playerWidth / staticPlayerTexture.getSize().x,
                          playerHeight / staticPlayerTexture.getSize().y);
    playerSprite.setOrigin(staticPlayerTexture.getSize().x / 2.f, staticPlayerTexture.getSize().y / 2.f);

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
    playerFixtureDef.friction = 0.5f; // This friction will be overridden during player-platform contacts
    playerFixtureDef.restitution = 0.0f;
    // Use 0 to denote the player's main fixture
    playerFixtureDef.userData.pointer = 0;
    playerBody->CreateFixture(&playerFixtureDef);

    // Player foot sensor
    b2PolygonShape footSensorBox;
    b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
    footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth * 0.9f / 2.f, 5.f)).x,
                           toMeters(sf::Vector2f(playerWidth * 0.9f / 2.f, 5.f)).y,
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
    ceilingBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).x,
                                toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).y);
    b2Body* ceilingBody = world.CreateBody(&ceilingBodyDef);
    b2PolygonShape ceilingBox;
    ceilingBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x,
                        toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* ceilingFixture = ceilingBody->CreateFixture(&ceilingBox, 0.0f);
    ceilingFixture->GetUserData().pointer = CEILING_ID;

    // --- Platform and Collectible Spawning ---
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lengthDist(minLength, maxLength);
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> collectibleChanceDist(0.0f, 1.0f);

    std::vector<Block> blocks;
    std::vector<Collectible> collectibles;
    uintptr_t nextPlatformId = PLATFORM_ID_BASE;

    sf::Clock spawnClock;
    float nextSpawnTime = initialSpawnTimeDist(gen);
    sf::Clock deltaClock;
    float gameTime = 0.f;

    enum GameState { Playing, GameOver };
    GameState currentState = Playing;

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
                        snapKeyPressed = true;
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == sf::Keyboard::W) {
                        jumpKeyPressed = false;
                    }
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();
        if (currentState == GameOver) {
            dt = 0;
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

            // Reset jumps if grounded and update player texture
            if (contactListener.isGrounded()) {
                jumpsRemaining = maxJumps;
                playerSprite.setTexture(staticPlayerTexture);
            } else {
                playerSprite.setTexture(jumpPlayerTexture);
            }

            // --- Snap Logic ('S' Key) ---
            if (snapKeyPressed && !contactListener.isGrounded()) {
                SnapRayCastCallback callback;
                b2Vec2 playerPos = playerBody->GetPosition();
                b2Vec2 p1 = playerPos + b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f + 1.f)).y);
                b2Vec2 p2 = p1 + b2Vec2(0, toMeters(sf::Vector2f(0, windowHeight)).y);

                world.RayCast(&callback, p1, p2);

                if (callback.closestFixture) {
                    b2Vec2 targetPos = callback.hitPoint - b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                    playerBody->SetTransform(b2Vec2(playerPos.x, targetPos.y), 0);
                    playerBody->SetLinearVelocity(b2Vec2(0, 0));
                    jumpsRemaining = maxJumps;
                }
                snapKeyPressed = false;
            }

            // --- Spawn new blocks (platforms) and collectibles ---
            if (spawnClock.getElapsedTime().asSeconds() >= nextSpawnTime) {
                Block newBlock;
                float blockLength = lengthDist(gen);
                float spawnY = yPosDist(gen);
                sf::Vector2f spawnPos(windowWidth + blockLength / 2.f, spawnY);

                newBlock.shape.setSize(sf::Vector2f(blockLength, fixedHeight));
                newBlock.shape.setFillColor(sf::Color(255, 200, 0));
                newBlock.shape.setOutlineColor(sf::Color::Black);
                newBlock.shape.setOutlineThickness(2.5f);
                newBlock.shape.setOrigin(blockLength / 2.f, fixedHeight / 2.f);
                newBlock.shape.setPosition(spawnPos);

                newBlock.line.setSize(sf::Vector2f(15.f, 500));
                newBlock.line.setFillColor(sf::Color(150,150,150));
                newBlock.line.setOutlineColor(sf::Color::Black);
                newBlock.line.setOutlineThickness(2.5f);
                newBlock.line.setOrigin(7.5f, 0.f);
                newBlock.line.setPosition(spawnPos.x, spawnPos.y + fixedHeight / 2.f);

                b2BodyDef blockBodyDef;
                blockBodyDef.type = b2_kinematicBody;
                blockBodyDef.position = toMeters(spawnPos);
                newBlock.body = world.CreateBody(&blockBodyDef);
                newBlock.id = nextPlatformId++;

                b2PolygonShape blockBox;
                blockBox.SetAsBox(toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).x,
                                  toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).y);

                b2FixtureDef blockFixtureDef;
                blockFixtureDef.shape = &blockBox;
                blockFixtureDef.friction = 0.7f;
                blockFixtureDef.userData.pointer = newBlock.id;
                newBlock.body->CreateFixture(&blockFixtureDef);

                newBlock.body->SetLinearVelocity(b2Vec2(toMeters(sf::Vector2f(-blockSpeed, 0.f)).x, 0.0f));

                bool visualOverlap = false;
                sf::FloatRect candidateBounds = newBlock.shape.getGlobalBounds();
                candidateBounds.left -= 50; candidateBounds.width += 100;
                for (const auto& block : blocks) {
                    if (!block.markedForRemoval && candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        visualOverlap = true;
                        break;
                    }
                }

                if (!visualOverlap) {
                    blocks.push_back(newBlock);
                    // Optionally spawn a collectible alongside the platform
                    if (collectibleChanceDist(gen) < collectibleSpawnChance) {
                        Collectible newCollectible;
                        newCollectible.shape.setRadius(collectibleRadius);
                        newCollectible.shape.setFillColor(sf::Color::Green);
                        newCollectible.shape.setOrigin(collectibleRadius, collectibleRadius);
                        sf::Vector2f collPos = sf::Vector2f(spawnPos.x, spawnPos.y - fixedHeight/2.f - collectibleRadius - 5.f);
                        newCollectible.shape.setPosition(collPos);

                        b2BodyDef collBodyDef;
                        collBodyDef.type = b2_kinematicBody;
                        collBodyDef.position = toMeters(collPos);
                        newCollectible.body = world.CreateBody(&collBodyDef);

                        b2CircleShape collShape;
                        collShape.m_radius = toMeters(sf::Vector2f(collectibleRadius, collectibleRadius)).x;

                        b2FixtureDef collFixtureDef;
                        collFixtureDef.shape = &collShape;
                        collFixtureDef.isSensor = true;
                        collFixtureDef.userData.pointer = COLLECTIBLE_ID;
                        newCollectible.body->CreateFixture(&collFixtureDef);

                        collectibles.push_back(newCollectible);
                    }
                } else {
                    // Remove body if overlapping
                    world.DestroyBody(newBlock.body);
                }
                spawnClock.restart();
                nextSpawnTime = currentSpawnTimeDist(gen);
            }
        }

        // --- Physics Step ---
        world.Step(dt, 8, 3);

        // --- Remove collectibles flagged for removal ---
        for (auto body : collectiblesToRemoveFromWorld) {
            for (auto it = collectibles.begin(); it != collectibles.end(); ++it) {
                if (it->body == body) {
                    world.DestroyBody(it->body);
                    collectibles.erase(it);
                    break;
                }
            }
        }
        collectiblesToRemoveFromWorld.clear();

        // --- Render ---
        window.clear();
        window.draw(backgroundSprite);

        // Draw blocks
        for (auto& block : blocks) {
            // Update block position from Box2D body
            b2Vec2 pos = block.body->GetPosition();
            block.shape.setPosition(toPixels(pos));
            block.line.setPosition(toPixels(pos).x, toPixels(pos).y + block.shape.getSize().y/2.f);
            window.draw(block.shape);
            window.draw(block.line);
        }

        // Draw collectibles
        for (auto& coll : collectibles) {
            b2Vec2 pos = coll.body->GetPosition();
            coll.shape.setPosition(toPixels(pos));
            window.draw(coll.shape);
        }

        // Update player sprite position from Box2D body
        b2Vec2 playerPos = playerBody->GetPosition();
        playerSprite.setPosition(toPixels(playerPos));
        window.draw(playerSprite);

        // Update and draw score
        scoreText.setString("Score: " + std::to_string(score));
        window.draw(scoreText);

        if (currentState == GameOver) {
            window.draw(gameOverText);
        }

        window.display();
    }
    return 0;
}
