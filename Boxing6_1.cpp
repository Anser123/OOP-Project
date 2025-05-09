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
#include <fstream> // For file I/O

inline float lerp(float a, float b, float t) { return a + t * (b - a); }

const float PIXELS_PER_METER = 30.0f;
const float METERS_PER_PIXEL = 1.0f / PIXELS_PER_METER;

sf::Vector2f toPixels(const b2Vec2& vec) {
    return sf::Vector2f(vec.x * PIXELS_PER_METER, vec.y * PIXELS_PER_METER);
}

b2Vec2 toMeters(const sf::Vector2f& vec) {
    return b2Vec2(vec.x * METERS_PER_PIXEL, vec.y * METERS_PER_PIXEL);
}

float toDegrees(float radians) {
    return radians * 180.0f / b2_pi;
}

struct Block {
    sf::RectangleShape shape;
    sf::RectangleShape line;
    b2Body* body = nullptr;
    bool markedForRemoval = false;
    uintptr_t id = 0;
};

enum CollectibleType { Magenta, Orange, Green, Red };

struct Collectible {
    sf::Sprite sprite; 
    CollectibleType type;
    b2Body* body = nullptr;
    bool markedForRemoval = false;
};

static constexpr uintptr_t FOOT_SENSOR_ID = 1;
static constexpr uintptr_t GROUND_ID = 2;
static constexpr uintptr_t CEILING_ID = 3;
static constexpr uintptr_t MAGENTA_COLLECTIBLE_ID = 4;
static constexpr uintptr_t ORANGE_COLLECTIBLE_ID = 5;
static constexpr uintptr_t GREEN_COLLECTIBLE_ID = 6;
static constexpr uintptr_t RED_COLLECTIBLE_ID = 7;
static constexpr uintptr_t PLATFORM_ID_BASE = 1000;

class PlayerContactListener : public b2ContactListener {
public:
    int footContacts = 0;
    bool touchedGround = false;
    std::vector<b2Body*>& collectiblesToRemove;

    PlayerContactListener(std::vector<b2Body*>& bodiesToRemove) : collectiblesToRemove(bodiesToRemove) {}

    void BeginContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // Player foot sensor on platform
        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) footContacts++;
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE) footContacts++;

        // Player body (ID 0) on ground (ID 2)
        if ((userDataA == 0 && userDataB == GROUND_ID) || (userDataB == 0 && userDataA == GROUND_ID)) {
            touchedGround = true;
        }

        // Player body (ID 0) on Collectible
        b2Body* collectibleBody = nullptr;
        if (userDataA == 0) { // Player body is A
            if (userDataB == MAGENTA_COLLECTIBLE_ID || userDataB == ORANGE_COLLECTIBLE_ID || userDataB == GREEN_COLLECTIBLE_ID || userDataB == RED_COLLECTIBLE_ID) {
                collectibleBody = fixtureB->GetBody();
            }
        } else if (userDataB == 0) { // Player body is B
            if (userDataA == MAGENTA_COLLECTIBLE_ID || userDataA == ORANGE_COLLECTIBLE_ID || userDataA == GREEN_COLLECTIBLE_ID || userDataA == RED_COLLECTIBLE_ID) {
                collectibleBody = fixtureA->GetBody();
            }
        }

        if (collectibleBody) {
            // Add body to removal list. Main loop will process effect.
            collectiblesToRemove.push_back(collectibleBody);
        }
    }

    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // Keep friction setting logic as is
        if ((userDataA >= PLATFORM_ID_BASE && userDataB == 0) || (userDataB >= PLATFORM_ID_BASE && userDataA == 0)) {
            contact->SetFriction(0.0f);
        }
    }

    void EndContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        // Player foot sensor leaving platform
        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE && footContacts > 0) footContacts--;
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE && footContacts > 0) footContacts--;
    }

    bool isGrounded() const {
        return footContacts > 0;
    }
};

class SnapRayCastCallback : public b2RayCastCallback {
public:
    b2Fixture* closestFixture = nullptr;
    b2Vec2 hitPoint;
    float closestFraction = 1.0f;

    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        uintptr_t userData = fixture->GetUserData().pointer;
        if (userData >= PLATFORM_ID_BASE) {
            if (fraction < closestFraction) {
                closestFraction = fraction;
                closestFixture = fixture;
                hitPoint = point;
            }
            return fraction;
        }
        return -1.0f;
    }
};

enum PlatformEffect { None, Lengthen, Shorten };

// Function to load high score from file
int loadHighScore(const std::string& filename) {
    std::ifstream file(filename);
    int highscore = 0;
    if (file.is_open()) {
        file >> highscore;
        file.close();
    }
    return highscore;
}

// Function to save high score to file
void saveHighScore(const std::string& filename, int highscore) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << highscore;
        file.close();
    }
}

int main() {
    const unsigned int windowWidth = 1200;
    const unsigned int windowHeight = 700;
    const float fixedHeight = 20.f; // Base lengths, will be modified by effects
    const float baseMinLength = 100.f;
    const float baseMaxLength = 300.f;
    float minLength = baseMinLength;
    float maxLength = baseMaxLength;
    float blockSpeed = 200.f;
    const float blockSpeedIncreaseFactor = 5.0f;
    const float maxBlockSpeed = 600.f;
    float minSpawnTime = 2.5f;
    float maxSpawnTime = 3.5f;
    const float minSpawnTimeBase = 0.8f;
    const float maxSpawnTimeBase = 1.5f;
    const float playerWidth = 40.f;
    const float playerHeight = 60.f;
    const float playerJumpForce = 300.0f;
    const int maxJumps = 2;
    float collectibleRadius = 25.f; // Increased radius for bigger size
    const float collectibleSpawnChance = 0.3f; // Total chance to spawn *any* collectible
    // New collectible spawn probabilities (relative to collectibleSpawnChance)
    const float magentaCollectibleProb = 0.4f; // 40% of 0.3 = 0.12 overall
    const float orangeCollectibleProb = 0.3f; // 30% of 0.3 = 0.09 overall
    const float greenCollectibleProb = 0.15f; // 15% of 0.3 = 0.045 overall
    const float redCollectibleProb = 0.15f; // 15% of 0.3 = 0.045 overall
    // New platform effect constants
    const float platformEffectDuration = 10.0f;
    const float lengthenFactor = 1.5f; // Platforms become 50% longer
    const float shortenFactor = 0.5f; // Platforms become 50% shorter

    sf::Color defaultBlockColor = sf::Color(255, 200, 0); // Original orange
    sf::Color greenBlockColor = sf::Color::Green;
    sf::Color redBlockColor = sf::Color::Red;

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rat Rider - Box2D");
    window.setFramerateLimit(60);

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

    // Load collectible textures
    sf::Texture collectibleTextures[4];
    if (!collectibleTextures[0].loadFromFile("collectible_1.png")) { // Magenta
        std::cerr << "Error loading texture 'collectible_1.png'" << std::endl; return 1;
    }
    if (!collectibleTextures[1].loadFromFile("collectible_2.png")) { // Orange
        std::cerr << "Error loading texture 'collectible_2.png'" << std::endl; return 1;
    }
    if (!collectibleTextures[2].loadFromFile("collectible_3.png")) { // Green
        std::cerr << "Error loading texture 'collectible_3.png'" << std::endl; return 1;
    }
    if (!collectibleTextures[3].loadFromFile("collectible_4.png")) { // Red
        std::cerr << "Error loading texture 'collectible_4.png'" << std::endl; return 1;
    }


    sf::SoundBuffer collectBuffer;
    if (!collectBuffer.loadFromFile("collectible.wav")) {
        std::cerr << "Error loading sound 'collectible.wav'" << std::endl;
        // Continue without sound if loading fails
    }
    sf::Sound collectSound;
    collectSound.setBuffer(collectBuffer);

    sf::Music backgroundMusic;
    if (!backgroundMusic.openFromFile("background.ogg")) {
        std::cerr << "Error loading music 'background.ogg'" << std::endl;
        // Continue without music if loading fails
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

    b2Vec2 gravity(0.0f, 7.0f);
    b2World world(gravity);

    std::vector<b2Body*> collectiblesToRemoveFromWorld;

    int score = 0;
    int highScore = loadHighScore("highscore.txt"); // Load high score

    // Pass only the removal list to the listener
    PlayerContactListener contactListener(collectiblesToRemoveFromWorld);
    world.SetContactListener(&contactListener);

    sf::Sprite playerSprite;
    playerSprite.setTexture(staticPlayerTexture);
    playerSprite.setScale(playerWidth / staticPlayerTexture.getSize().x, playerHeight / staticPlayerTexture.getSize().y);
    playerSprite.setOrigin(staticPlayerTexture.getSize().x / 2.f, staticPlayerTexture.getSize().y / 2.f); // Set origin to center

    b2BodyDef playerBodyDef;
    playerBodyDef.type = b2_dynamicBody;
    playerBodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f, windowHeight - 200.f));
    playerBodyDef.fixedRotation = true;
    playerBodyDef.allowSleep = false;
    b2Body* playerBody = world.CreateBody(&playerBodyDef);

    b2PolygonShape playerBox;
    playerBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);
    b2FixtureDef playerFixtureDef;
    playerFixtureDef.shape = &playerBox;
    playerFixtureDef.density = 1.0f;
    playerFixtureDef.friction = 0.5f;
    playerFixtureDef.restitution = 0.0f;
    playerFixtureDef.userData.pointer = 0; // Player body ID
    playerBody->CreateFixture(&playerFixtureDef);

    b2PolygonShape footSensorBox;
    b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
    footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y, footSensorCenter, 0);
    b2FixtureDef footSensorFixtureDef;
    footSensorFixtureDef.shape = &footSensorBox;
    footSensorFixtureDef.isSensor = true;
    footSensorFixtureDef.userData.pointer = FOOT_SENSOR_ID;
    playerBody->CreateFixture(&footSensorFixtureDef);

    int jumpsRemaining = maxJumps;
    bool jumpKeyPressed = false;
    bool snapKeyPressed = false;

    b2BodyDef groundBodyDef;
    groundBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).x, toMeters(sf::Vector2f(windowWidth / 2.f, windowHeight + 50.f)).y);
    b2Body* groundBody = world.CreateBody(&groundBodyDef);
    b2PolygonShape groundBox;
    groundBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x, toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* groundFixture = groundBody->CreateFixture(&groundBox, 0.0f);
    groundFixture->GetUserData().pointer = GROUND_ID;

    b2BodyDef ceilingBodyDef;
    ceilingBodyDef.position.Set(toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).x, toMeters(sf::Vector2f(windowWidth / 2.f, -10.f)).y);
    b2Body* ceilingBody = world.CreateBody(&ceilingBodyDef);
    b2PolygonShape ceilingBox;
    ceilingBox.SetAsBox(toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).x, toMeters(sf::Vector2f(windowWidth / 2.f, 10.f)).y);
    b2Fixture* ceilingFixture = ceilingBody->CreateFixture(&ceilingBox, 0.0f);
    ceilingFixture->GetUserData().pointer = CEILING_ID;

    std::random_device rd;
    std::mt19937 gen(rd());
    // Length distribution will be updated based on effect
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> collectibleTypeDist(0.0f, 1.0f); // For choosing collectible type

    std::vector<Block> blocks;
    std::vector<Collectible> collectibles; // Vector of Collectible structs
    uintptr_t nextPlatformId = PLATFORM_ID_BASE;

    sf::Clock spawnClock;
    float nextSpawnTime = initialSpawnTimeDist(gen);

    sf::Clock deltaClock;
    float gameTime = 0.f;

    // Platform effect state
    PlatformEffect currentPlatformEffect = PlatformEffect::None;
    sf::Clock platformEffectClock;

    enum GameState { Playing, GameOver };
    GameState currentState = Playing;

    sf::Text gameOverText("Game Over!", font, 50);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setStyle(sf::Text::Italic);
    sf::FloatRect textRect = gameOverText.getLocalBounds();
    gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
    gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));

    sf::Text scoreText("Score: 0", font, 30);
    scoreText.setFillColor(sf::Color::Red);
    scoreText.setPosition(10.f, 10.f);

    sf::Text highScoreText("High Score: 0", font, 30);
    highScoreText.setFillColor(sf::Color::Yellow);
    highScoreText.setPosition(10.f, 50.f); // Position below score

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();

            if (currentState == Playing) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::W && !jumpKeyPressed) {
                        if (jumpsRemaining > 0) {
                            float impulse = playerBody->GetMass() * sqrt(2.0f * std::abs(gravity.y) * (playerJumpForce * METERS_PER_PIXEL));
                            playerBody->SetLinearVelocity(b2Vec2(playerBody->GetLinearVelocity().x, 0)); // Cancel vertical velocity before jump
                            playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulse), true);
                            jumpsRemaining--;
                            jumpKeyPressed = true; // Prevent holding W for multiple jumps
                        }
                    } else if (event.key.code == sf::Keyboard::S && !snapKeyPressed) {
                        snapKeyPressed = true; // Mark snap key pressed
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == sf::Keyboard::W) {
                        jumpKeyPressed = false; // Allow jumping again after key release
                    }
                    if (event.key.code == sf::Keyboard::S) {
                        snapKeyPressed = false; // Allow snapping again after key release
                    }
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();

        if (currentState == GameOver) {
            dt = 0; // Pause game time in game over state
        } else {
            gameTime += dt;

            // Update platform effect timer
            if (currentPlatformEffect != PlatformEffect::None) {
                if (platformEffectClock.getElapsedTime().asSeconds() >= platformEffectDuration) {
                    currentPlatformEffect = PlatformEffect::None;
                    // std::cout << "Platform effect ended." << std::endl; // Debug
                }
            }
        }

        if (currentState == Playing) {
            // Update block speed and spawn times based on game time
            if (blockSpeed < maxBlockSpeed) {
                blockSpeed += blockSpeedIncreaseFactor * dt;
                blockSpeed = std::min(blockSpeed, maxBlockSpeed);
                float speedRatio = (blockSpeed - 200.f) / (maxBlockSpeed - 200.f);
                minSpawnTime = lerp(2.5f, minSpawnTimeBase, speedRatio);
                maxSpawnTime = lerp(3.5f, maxSpawnTimeBase, speedRatio);
            }
            std::uniform_real_distribution<float> currentSpawnTimeDist(minSpawnTime, maxSpawnTime);

            // Update player state (grounded, jumps, texture)
            if (contactListener.isGrounded()) {
                jumpsRemaining = maxJumps;
                playerSprite.setTexture(staticPlayerTexture);
            } else {
                playerSprite.setTexture(jumpPlayerTexture);
            }

            // Handle snap key
            if (snapKeyPressed && !contactListener.isGrounded()) {
                SnapRayCastCallback callback;
                b2Vec2 playerPos = playerBody->GetPosition();
                // Raycast downwards from slightly below player center
                b2Vec2 p1 = playerPos + b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f + 1.f)).y);
                b2Vec2 p2 = p1 + b2Vec2(0, toMeters(sf::Vector2f(0, windowHeight)).y); // Raycast far down
                world.RayCast(&callback, p1, p2);

                if (callback.closestFixture) {
                    // Calculate target position just above the hit point
                    b2Vec2 targetPos = callback.hitPoint - b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                    playerBody->SetTransform(b2Vec2(playerPos.x, targetPos.y), 0); // Teleport player vertically
                    playerBody->SetLinearVelocity(b2Vec2(playerBody->GetLinearVelocity().x, 0)); // Stop vertical movement
                    jumpsRemaining = maxJumps; // Reset jumps
                }
                snapKeyPressed = false; // Consume the snap action
            }

            // Update block length distribution and color based on effect
            float currentMinLength = baseMinLength;
            float currentMaxLength = baseMaxLength;
            sf::Color currentBlockColor = defaultBlockColor;
            if (currentPlatformEffect == PlatformEffect::Lengthen) {
                currentMinLength = baseMinLength * lengthenFactor;
                currentMaxLength = baseMaxLength * lengthenFactor;
                currentBlockColor = greenBlockColor;
            } else if (currentPlatformEffect == PlatformEffect::Shorten) {
                currentMinLength = baseMinLength * shortenFactor;
                currentMaxLength = baseMaxLength * shortenFactor;
                currentBlockColor = redBlockColor;
            }
            std::uniform_real_distribution<float> currentLengthDist(currentMinLength, currentMaxLength);

            // Spawn new blocks
            if (spawnClock.getElapsedTime().asSeconds() >= nextSpawnTime) {
                Block newBlock;
                float blockLength = currentLengthDist(gen); // Use current length distribution
                float spawnY = yPosDist(gen);
                sf::Vector2f spawnPos(windowWidth + blockLength / 2.f, spawnY);

                newBlock.shape.setSize(sf::Vector2f(blockLength, fixedHeight));
                newBlock.shape.setFillColor(currentBlockColor); // Use current block color
                newBlock.shape.setOutlineColor(sf::Color::Black);
                newBlock.shape.setOutlineThickness(2.5f);
                newBlock.shape.setOrigin(blockLength / 2.f, fixedHeight / 2.f);
                newBlock.shape.setPosition(spawnPos);

                newBlock.line.setSize(sf::Vector2f(15.f, 500));
                newBlock.line.setFillColor(sf::Color(150,150,150)); // Line color doesn't change
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
                blockBox.SetAsBox(toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).x, toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).y);
                b2FixtureDef blockFixtureDef;
                blockFixtureDef.shape = &blockBox;
                blockFixtureDef.friction = 0.7f;
                blockFixtureDef.userData.pointer = newBlock.id;
                newBlock.body->CreateFixture(&blockFixtureDef);
                newBlock.body->SetLinearVelocity(b2Vec2(toMeters(sf::Vector2f(-blockSpeed, 0.f)).x, 0.0f));

                // Check for visual overlap before adding
                bool visualOverlap = false;
                sf::FloatRect candidateBounds = newBlock.shape.getGlobalBounds();
                candidateBounds.left -= 50; // Add some buffer
                candidateBounds.width += 100; // Add some buffer
                for (const auto& block : blocks) {
                    if (!block.markedForRemoval && block.body && candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        visualOverlap = true;
                        break;
                    }
                }

                if (!visualOverlap) {
                    blocks.push_back(newBlock);

                    // Spawn collectible on this block?
                    float collectibleRoll = collectibleTypeDist(gen); // Roll between 0 and 1
                    if (collectibleRoll < collectibleSpawnChance) {
                        Collectible newCollectible;
                        sf::Vector2f collectiblePos = spawnPos;
                        collectiblePos.y -= (fixedHeight / 2.f + collectibleRadius + 5.f); // Position above block

                        // Determine collectible type and texture based on probabilities
                        float typeRoll = collectibleTypeDist(gen); // Use a new roll for type
                        uintptr_t collectibleUserData = 0;
                        sf::Texture* texturePtr = nullptr;

                        if (typeRoll < magentaCollectibleProb) {
                            newCollectible.type = CollectibleType::Magenta;
                            collectibleUserData = MAGENTA_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[0]; // collectible_1.png
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb) {
                            newCollectible.type = CollectibleType::Orange;
                            collectibleUserData = ORANGE_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[1]; // collectible_2.png
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb + greenCollectibleProb) {
                            newCollectible.type = CollectibleType::Green;
                            collectibleUserData = GREEN_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[2]; // collectible_3.png
                        } else { // Remaining chance for Red
                            newCollectible.type = CollectibleType::Red;
                            collectibleUserData = RED_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[3]; // collectible_4.png
                        }

                        // Setup collectible sprite
                        newCollectible.sprite.setTexture(*texturePtr);
                        // Scale sprite to match the desired visual size (based on collectibleRadius)
                        newCollectible.sprite.setScale(
                            (collectibleRadius * 2.f) / newCollectible.sprite.getTexture()->getSize().x,
                            (collectibleRadius * 2.f) / newCollectible.sprite.getTexture()->getSize().y
                        );
                        newCollectible.sprite.setOrigin(newCollectible.sprite.getTexture()->getSize().x / 2.f, newCollectible.sprite.getTexture()->getSize().y / 2.f);
                        newCollectible.sprite.setPosition(collectiblePos);

                        // Setup collectible Box2D body
                        b2BodyDef collectibleBodyDef;
                        collectibleBodyDef.type = b2_kinematicBody;
                        collectibleBodyDef.position = toMeters(collectiblePos);
                        newCollectible.body = world.CreateBody(&collectibleBodyDef);

                        b2CircleShape collectibleCircle;
                        collectibleCircle.m_radius = toMeters(sf::Vector2f(collectibleRadius, 0)).x; // Radius in meters

                        b2FixtureDef collectibleFixtureDef;
                        collectibleFixtureDef.shape = &collectibleCircle;
                        collectibleFixtureDef.isSensor = true;
                        collectibleFixtureDef.userData.pointer = collectibleUserData; // Set the specific ID
                        newCollectible.body->CreateFixture(&collectibleFixtureDef);

                        // Collectibles move with the block
                        newCollectible.body->SetLinearVelocity(newBlock.body->GetLinearVelocity());

                        collectibles.push_back(newCollectible);
                    }
                } else {
                    // If block overlaps, destroy its body immediately
                    world.DestroyBody(newBlock.body);
                }

                spawnClock.restart();
                nextSpawnTime = currentSpawnTimeDist(gen);
            }

            // Step the Box2D world
            world.Step(dt, 8, 3);

            // Update SFML positions from Box2D bodies
            playerSprite.setPosition(toPixels(playerBody->GetPosition()));

            for (auto& block : blocks) {
                if (block.body) {
                    block.shape.setPosition(toPixels(block.body->GetPosition()));
                    block.line.setPosition(block.shape.getPosition().x, block.shape.getPosition().y + fixedHeight / 2.f);

                    // Mark for removal if off-screen
                    if (block.shape.getPosition().x < -block.shape.getSize().x / 2.f) {
                        block.markedForRemoval = true;
                    }
                }
            }

            for (auto& collectible : collectibles) {
                if (collectible.body) {
                    collectible.sprite.setPosition(toPixels(collectible.body->GetPosition())); // Update sprite position from body

                    // Mark for removal if off-screen
                    if (collectible.sprite.getPosition().x < -collectible.sprite.getGlobalBounds().width / 2.f) {
                        collectible.markedForRemoval = true;
                    }
                }
            }

            // Process collectibles marked for removal by the contact listener
            for (b2Body* bodyToRemove : collectiblesToRemoveFromWorld) {
                for (auto it = collectibles.begin(); it != collectibles.end(); ++it) {
                    if (it->body == bodyToRemove) {
                        // Apply effect based on type BEFORE marking for removal
                        switch (it->type) {
                            case CollectibleType::Magenta:
                                score++;
                                collectSound.play(); // Play sound for score increase
                                break;
                            case CollectibleType::Orange:
                                score += 5;
                                collectSound.play(); // Play sound for score increase
                                break;
                            case CollectibleType::Green:
                                currentPlatformEffect = PlatformEffect::Lengthen;
                                platformEffectClock.restart();
                                // Optional: Play a different sound for effects
                                break;
                            case CollectibleType::Red:
                                currentPlatformEffect = PlatformEffect::Shorten;
                                platformEffectClock.restart();
                                // Optional: Play a different sound for effects
                                break;
                        }
                        it->markedForRemoval = true; // Mark the struct for removal from vector
                        break; // Found the collectible, exit inner loop
                    }
                }
            }
            collectiblesToRemoveFromWorld.clear(); // Clear the list for the next step

            // Remove marked blocks and collectibles from vectors and world
            blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](Block& block) {
                if (block.markedForRemoval && block.body) {
                    world.DestroyBody(block.body);
                    block.body = nullptr;
                    return true; // Remove from vector
                }
                return false;
            }), blocks.end());

            collectibles.erase(std::remove_if(collectibles.begin(), collectibles.end(), [&](Collectible& collectible) {
                if (collectible.markedForRemoval && collectible.body) {
                    // Double check body exists in world before destroying (safety)
                    bool bodyExists = false;
                    for (b2Body* b = world.GetBodyList(); b; b = b->GetNext()) {
                        if (b == collectible.body) {
                            bodyExists = true;
                            break;
                        }
                    }
                    if(bodyExists) {
                        world.DestroyBody(collectible.body);
                    }
                    collectible.body = nullptr;
                    return true; // Remove from vector
                }
                return false;
            }), collectibles.end());

            // Check for game over condition
            if (contactListener.touchedGround || playerBody->GetPosition().y > toMeters(sf::Vector2f(0, windowHeight + playerHeight)).y) {
                currentState = GameOver;
                backgroundMusic.stop();
                // Check and save high score
                if (score > highScore) {
                    highScore = score;
                    saveHighScore("highscore.txt", highScore);
                }
                // std::cout << "Game Over! Final Score: " << score << std::endl; // Debug
            }

            // Update score text
            scoreText.setString("Score: " + std::to_string(score));
            // Update high score text
            highScoreText.setString("High Score: " + std::to_string(highScore));

        } // End if (currentState == Playing)

        // Drawing
        window.clear(sf::Color(50, 50, 100));
        window.draw(backgroundSprite);

        // Draw blocks
        for (const auto& block : blocks) {
            window.draw(block.line);
            window.draw(block.shape);
        }

        // Draw collectibles
        for (const auto& collectible : collectibles) {
            window.draw(collectible.sprite); // Draw the sprite
        }

        // Draw player
        window.draw(playerSprite);

        // Draw UI
        window.draw(scoreText);
        window.draw(highScoreText); // Draw high score

        if (currentState == GameOver) {
            window.draw(gameOverText);
        }

        window.display();
    }

    return 0;
}
