#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <Box2D/Box2D.h>
#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <algorithm>
#include <cmath>

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

enum CollectibleType {
    Magenta,
    Orange,
    Green,
    Red,
    White,
    MinusScore
};

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
static constexpr uintptr_t WHITE_COLLECTIBLE_ID = 8;
static constexpr uintptr_t MINUS_SCORE_COLLECTIBLE_ID = 9;
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

        
        if (userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) footContacts++;
        if (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE) footContacts++;

        
        if ((userDataA == 0 && userDataB == GROUND_ID) || (userDataB == 0 && userDataA == GROUND_ID)) {
            touchedGround = true;
        }

        
        b2Body* collectibleBody = nullptr;
        uintptr_t collectibleUserData = 0;
        if (userDataA == 0) { 
            collectibleUserData = userDataB;
            collectibleBody = fixtureB->GetBody();
        } else if (userDataB == 0) { 
            collectibleUserData = userDataA;
            collectibleBody = fixtureA->GetBody();
        }

        if (collectibleBody) {
            if (collectibleUserData == MAGENTA_COLLECTIBLE_ID ||
                collectibleUserData == ORANGE_COLLECTIBLE_ID ||
                collectibleUserData == GREEN_COLLECTIBLE_ID ||
                collectibleUserData == RED_COLLECTIBLE_ID ||
                collectibleUserData == WHITE_COLLECTIBLE_ID ||
                collectibleUserData == MINUS_SCORE_COLLECTIBLE_ID)
            {
                
                collectiblesToRemove.push_back(collectibleBody);
            }
        }
    }

    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        
        if ((userDataA >= PLATFORM_ID_BASE && userDataB == 0) || (userDataB >= PLATFORM_ID_BASE && userDataA == 0)) {
            contact->SetFriction(0.0f);
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

int loadHighScore(const std::string& filename) {
    std::ifstream file(filename);
    int highscore = 0;
    if (file.is_open()) {
        file >> highscore;
        file.close();
    }
    return highscore;
}

enum PlatformEffect {
    None,
    Lengthen,
    Shorten
};

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
    const float fixedHeight = 20.f;
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
    const float playerJumpForce = 350.0f; 
    const int maxJumps = 3;
    float collectibleRadius = 25.f;
    const float collectibleSpawnChance = 0.45f;
    const float magentaCollectibleProb = 0.35f;
    const float orangeCollectibleProb = 0.20f;
    const float greenCollectibleProb = 0.125f;
    const float redCollectibleProb = 0.125f;
    const float whiteCollectibleProb = 0.05f;
    const float minusScoreCollectibleProb = 0.15f;
    const float platformEffectDuration = 10.0f;
    const float lengthenFactor = 2.0f;
    const float shortenFactor = 0.5f;
    const float magentaRainDuration = 10.0f;
    const float magentaRainSpawnInterval = 0.25f;
    const float magentaRainSpeed = 300.0f;

    sf::Color defaultBlockColor = sf::Color(255, 200, 0);
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

    sf::Texture collectibleTextures[6];
    if (!collectibleTextures[0].loadFromFile("CHEEZE.png")) { std::cerr << "Error loading texture 'CHEEZE.png'" << std::endl; return 1; }
    if (!collectibleTextures[1].loadFromFile("Pizza2.png")) { std::cerr << "Error loading texture 'Pizza2.png'" << std::endl; return 1; }
    if (!collectibleTextures[2].loadFromFile("Long_Platform_Green.png")) { std::cerr << "Error loading texture 'Long_Platform_Green.png'" << std::endl; return 1; }
    if (!collectibleTextures[3].loadFromFile("Short_Platform_Red.png")) { std::cerr << "Error loading texture 'Short_Platform_Red.png'" << std::endl; return 1; }
    if (!collectibleTextures[4].loadFromFile("Cheese_Rain.png")) { std::cerr << "Error loading texture 'Cheese_Rain.png'" << std::endl; return 1; }
    if (!collectibleTextures[5].loadFromFile("Poison.png")) { std::cerr << "Error loading texture 'Poison.png'" << std::endl; return 1; }


    sf::SoundBuffer collectBuffer;
    if (!collectBuffer.loadFromFile("collectible.wav")) {
        std::cerr << "Error loading sound 'collectible.wav'" << std::endl;
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
    if (!font.loadFromFile("font.ttf")) {
        std::cerr << "Error loading font font.ttf" << std::endl;
        return 1;
    }

    b2Vec2 gravity(0.0f, 7.0f);
    b2World world(gravity);

    std::vector<b2Body*> collectiblesToRemoveFromWorld;
    int score = 0;
    int highScore = loadHighScore("highscore.txt");

    PlayerContactListener contactListener(collectiblesToRemoveFromWorld);
    world.SetContactListener(&contactListener);

    sf::Sprite playerSprite;
    playerSprite.setTexture(staticPlayerTexture);
    playerSprite.setScale(playerWidth / staticPlayerTexture.getSize().x, playerHeight / staticPlayerTexture.getSize().y);
    playerSprite.setOrigin(staticPlayerTexture.getSize().x / 2.f, staticPlayerTexture.getSize().y / 2.f);

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
    playerFixtureDef.userData.pointer = 0; 
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
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> collectibleTypeDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rainXPosDist(collectibleRadius, windowWidth - collectibleRadius);

    std::vector<Block> blocks;
    std::vector<Collectible> collectibles;
    uintptr_t nextPlatformId = PLATFORM_ID_BASE;

    sf::Clock spawnClock;
    float nextSpawnTime = initialSpawnTimeDist(gen);

    sf::Clock deltaClock;
    float gameTime = 0.f;

    PlatformEffect currentPlatformEffect = PlatformEffect::None;
    sf::Clock platformEffectClock;

    bool isRainingMagenta = false;
    sf::Clock magentaRainClock;
    sf::Clock magentaRainSpawnClock;

    enum GameState {
        Playing,
        GameOver
    };
    GameState currentState = Playing;

    sf::Text gameOverText("Game Over!", font, 50);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setStyle(sf::Text::Bold);
    sf::FloatRect textRect = gameOverText.getLocalBounds();
    gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
    gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));

    sf::Text scoreText("Score 0", font, 30);
    scoreText.setFillColor(sf::Color::White);
    scoreText.setPosition(25.f, 10.f);

    sf::Text highScoreText("High Score 0", font, 30);
    highScoreText.setFillColor(sf::Color::White);
    highScoreText.setPosition(850.f, 10.f);


    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (currentState == Playing) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == (sf::Keyboard::W )) {
                        if (jumpsRemaining > 0) {
                            
                            float impulseMagnitude = playerJumpForce * METERS_PER_PIXEL * playerBody->GetMass();
                            playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulseMagnitude), true);
                            jumpsRemaining--;
                        }
                    } else if (event.key.code == (sf::Keyboard::S ) && !snapKeyPressed) {
                        snapKeyPressed = true;
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == (sf::Keyboard::W )) {
                        
                        
                        
                        
                    }
                    if (event.key.code == (sf::Keyboard::S )) {
                        snapKeyPressed = false; 
                    }
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();

        if (currentState == GameOver) {
            dt = 0; 
        } else {
            dt = std::min(dt, 0.1f); 
            gameTime += dt;

            
            if (currentPlatformEffect != PlatformEffect::None) {
                if (platformEffectClock.getElapsedTime().asSeconds() >= platformEffectDuration) {
                    currentPlatformEffect = PlatformEffect::None;
                }
            }

            
            if (isRainingMagenta) {
                if (magentaRainClock.getElapsedTime().asSeconds() >= magentaRainDuration) {
                    isRainingMagenta = false;
                } else {
                    if (magentaRainSpawnClock.getElapsedTime().asSeconds() >= magentaRainSpawnInterval) {
                        Collectible rainCollectible;
                        sf::Vector2f spawnPos(rainXPosDist(gen), -collectibleRadius); 
                        rainCollectible.type = CollectibleType::Magenta;
                        uintptr_t collectibleUserData = MAGENTA_COLLECTIBLE_ID;
                        sf::Texture* texturePtr = &collectibleTextures[0]; 

                        rainCollectible.sprite.setTexture(*texturePtr);
                        rainCollectible.sprite.setScale(
                            (collectibleRadius * 2.f) / rainCollectible.sprite.getTexture()->getSize().x,
                            (collectibleRadius * 2.f) / rainCollectible.sprite.getTexture()->getSize().y
                        );
                        rainCollectible.sprite.setOrigin(rainCollectible.sprite.getTexture()->getSize().x / 2.f, rainCollectible.sprite.getTexture()->getSize().y / 2.f);
                        rainCollectible.sprite.setPosition(spawnPos);

                        b2BodyDef collectibleBodyDef;
                        collectibleBodyDef.type = b2_kinematicBody; 
                        collectibleBodyDef.position = toMeters(spawnPos);
                        rainCollectible.body = world.CreateBody(&collectibleBodyDef);

                        b2CircleShape collectibleCircle;
                        collectibleCircle.m_radius = toMeters(sf::Vector2f(collectibleRadius, 0)).x;
                        b2FixtureDef collectibleFixtureDef;
                        collectibleFixtureDef.shape = &collectibleCircle;
                        collectibleFixtureDef.isSensor = true;
                        collectibleFixtureDef.userData.pointer = collectibleUserData;
                        rainCollectible.body->CreateFixture(&collectibleFixtureDef);

                        
                        rainCollectible.body->SetLinearVelocity(b2Vec2(0.0f, toMeters(sf::Vector2f(0, magentaRainSpeed)).y));

                        collectibles.push_back(rainCollectible);
                        magentaRainSpawnClock.restart();
                    }
                }
            }
        }


        if (currentState == Playing) {
            
            if (blockSpeed < maxBlockSpeed) {
                blockSpeed += blockSpeedIncreaseFactor * dt;
                blockSpeed = std::min(blockSpeed, maxBlockSpeed);
                
                float speedRatio = (blockSpeed - 200.f) / (maxBlockSpeed - 200.f); 
                minSpawnTime = lerp(2.5f, minSpawnTimeBase, speedRatio);
                maxSpawnTime = lerp(3.5f, maxSpawnTimeBase, speedRatio);
            }

            
            if (contactListener.isGrounded()) {
                jumpsRemaining = maxJumps;
                playerSprite.setTexture(staticPlayerTexture);
            } else {
                playerSprite.setTexture(jumpPlayerTexture);
            }

            
            if (snapKeyPressed && !contactListener.isGrounded()) {
                SnapRayCastCallback callback;
                b2Vec2 playerPos = playerBody->GetPosition();
                
                b2Vec2 p1 = playerPos + b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f + 1.f)).y);
                
                b2Vec2 p2 = p1 + b2Vec2(0, toMeters(sf::Vector2f(0, windowHeight)).y);
                world.RayCast(&callback, p1, p2);

                if (callback.closestFixture) {
                    
                    b2Vec2 targetPos = callback.hitPoint - b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                    
                    playerBody->SetTransform(b2Vec2(playerPos.x, targetPos.y), 0);
                    
                    playerBody->SetLinearVelocity(b2Vec2(playerBody->GetLinearVelocity().x, 0));
                    
                    jumpsRemaining = maxJumps;
                }
                
                snapKeyPressed = false;
            }

            
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


            
            if (spawnClock.getElapsedTime().asSeconds() >= nextSpawnTime) {
                Block newBlock;
                float blockLength = currentLengthDist(gen);
                float spawnY = yPosDist(gen);
                sf::Vector2f spawnPos(windowWidth + blockLength / 2.f, spawnY); 

                
                newBlock.shape.setSize(sf::Vector2f(blockLength, fixedHeight));
                newBlock.shape.setFillColor(currentBlockColor);
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
                blockBox.SetAsBox(toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).x, toMeters(sf::Vector2f(blockLength / 2.f, fixedHeight / 2.f)).y);
                b2FixtureDef blockFixtureDef;
                blockFixtureDef.shape = &blockBox;
                blockFixtureDef.friction = 0.7f;
                blockFixtureDef.userData.pointer = newBlock.id; 
                newBlock.body->CreateFixture(&blockFixtureDef);

                
                newBlock.body->SetLinearVelocity(b2Vec2(toMeters(sf::Vector2f(-blockSpeed, 0.f)).x, 0.0f));

                
                bool visualOverlap = false;
                sf::FloatRect candidateBounds = newBlock.shape.getGlobalBounds();
                
                candidateBounds.left -= 50;
                candidateBounds.width += 100;

                for (const auto& block : blocks) {
                    if (!block.markedForRemoval && block.body && candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        visualOverlap = true;
                        break;
                    }
                }

                if (!visualOverlap) {
                    blocks.push_back(newBlock);

                    
                    float collectibleRoll = collectibleTypeDist(gen);
                    if (collectibleRoll < collectibleSpawnChance) {
                        Collectible newCollectible;
                        sf::Vector2f collectiblePos = spawnPos;
                        
                        collectiblePos.y -= (fixedHeight / 2.f + collectibleRadius + 5.f);

                        float typeRoll = collectibleTypeDist(gen);
                        uintptr_t collectibleUserData = 0;
                        sf::Texture* texturePtr = nullptr;
                        CollectibleType type;

                        
                        if (typeRoll < magentaCollectibleProb) {
                            type = CollectibleType::Magenta;
                            collectibleUserData = MAGENTA_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[0];
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb) {
                            type = CollectibleType::Orange;
                            collectibleUserData = ORANGE_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[1];
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb + greenCollectibleProb) {
                            type = CollectibleType::Green;
                            collectibleUserData = GREEN_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[2];
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb + greenCollectibleProb + redCollectibleProb) {
                            type = CollectibleType::Red;
                            collectibleUserData = RED_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[3];
                        } else if (typeRoll < magentaCollectibleProb + orangeCollectibleProb + greenCollectibleProb + redCollectibleProb + whiteCollectibleProb) {
                            type = CollectibleType::White;
                            collectibleUserData = WHITE_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[4];
                        } else {
                            type = CollectibleType::MinusScore;
                            collectibleUserData = MINUS_SCORE_COLLECTIBLE_ID;
                            texturePtr = &collectibleTextures[5];
                        }

                        newCollectible.type = type;
                        newCollectible.sprite.setTexture(*texturePtr);
                        newCollectible.sprite.setScale(
                            (collectibleRadius * 2.f) / newCollectible.sprite.getTexture()->getSize().x,
                            (collectibleRadius * 2.f) / newCollectible.sprite.getTexture()->getSize().y
                        );
                        newCollectible.sprite.setOrigin(newCollectible.sprite.getTexture()->getSize().x / 2.f, newCollectible.sprite.getTexture()->getSize().y / 2.f);
                        newCollectible.sprite.setPosition(collectiblePos);

                        b2BodyDef collectibleBodyDef;
                        collectibleBodyDef.type = b2_kinematicBody; 
                        collectibleBodyDef.position = toMeters(collectiblePos);
                        newCollectible.body = world.CreateBody(&collectibleBodyDef);

                        b2CircleShape collectibleCircle;
                        collectibleCircle.m_radius = toMeters(sf::Vector2f(collectibleRadius, 0)).x;
                        b2FixtureDef collectibleFixtureDef;
                        collectibleFixtureDef.shape = &collectibleCircle;
                        collectibleFixtureDef.isSensor = true; 
                        collectibleFixtureDef.userData.pointer = collectibleUserData; 
                        newCollectible.body->CreateFixture(&collectibleFixtureDef);

                        
                        newCollectible.body->SetLinearVelocity(newBlock.body->GetLinearVelocity());

                        collectibles.push_back(newCollectible);
                    }

                } else {
                    
                    world.DestroyBody(newBlock.body);
                }

                
                spawnClock.restart();
                std::uniform_real_distribution<float> nextSpawnTimeDist(minSpawnTime, maxSpawnTime);
                nextSpawnTime = nextSpawnTimeDist(gen);
            }

            
            world.Step(dt, 8, 3);

            
            playerSprite.setPosition(toPixels(playerBody->GetPosition()));

            for (auto& block : blocks) {
                if (block.body) {
                    block.shape.setPosition(toPixels(block.body->GetPosition()));
                    
                    block.line.setPosition(block.shape.getPosition().x, block.shape.getPosition().y + fixedHeight / 2.f);

                    
                    if (block.shape.getPosition().x < -block.shape.getSize().x / 2.f) {
                        block.markedForRemoval = true;
                    }
                }
            }

            for (auto& collectible : collectibles) {
                if (collectible.body) {
                    collectible.sprite.setPosition(toPixels(collectible.body->GetPosition()));

                    
                    if (collectible.sprite.getPosition().x < -collectible.sprite.getGlobalBounds().width / 2.f ||
                        collectible.sprite.getPosition().y > windowHeight + collectible.sprite.getGlobalBounds().height / 2.f)
                    {
                        collectible.markedForRemoval = true;
                    }
                }
            }

            
            for (b2Body* bodyToRemove : collectiblesToRemoveFromWorld) {
                for (auto it = collectibles.begin(); it != collectibles.end(); ++it) {
                    if (it->body == bodyToRemove) {
                        
                        switch (it->type) {
                            case CollectibleType::Magenta:
                                score++;
                                collectSound.play();
                                break;
                            case CollectibleType::Orange:
                                score += 3;
                                collectSound.play();
                                break;
                            case CollectibleType::Green:
                                currentPlatformEffect = PlatformEffect::Lengthen;
                                platformEffectClock.restart();
                                collectSound.play();
                                break;
                            case CollectibleType::Red:
                                currentPlatformEffect = PlatformEffect::Shorten;
                                platformEffectClock.restart();
                                collectSound.play();
                                break;
                            case CollectibleType::White:
                                isRainingMagenta = true;
                                magentaRainClock.restart();
                                magentaRainSpawnClock.restart();
                                collectSound.play();
                                break;
                            case CollectibleType::MinusScore:
                                score = std::max(0, score - 2); 
                                collectSound.play();
                                break;
                        }
                        it->markedForRemoval = true; 
                        break; 
                    }
                }
            }
            collectiblesToRemoveFromWorld.clear(); 

            
            blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](Block& block) {
                if (block.markedForRemoval && block.body) {
                    world.DestroyBody(block.body);
                    block.body = nullptr; 
                    return true;
                }
                return false;
            }), blocks.end());

            collectibles.erase(std::remove_if(collectibles.begin(), collectibles.end(), [&](Collectible& collectible) {
                if (collectible.markedForRemoval && collectible.body) {
                    world.DestroyBody(collectible.body);
                    collectible.body = nullptr; 
                    return true;
                }
                return false;
            }), collectibles.end());


            
            if (contactListener.touchedGround || 
                playerBody->GetPosition().y > toMeters(sf::Vector2f(0, windowHeight + playerHeight)).y || 
                playerBody->GetPosition().x < toMeters(sf::Vector2f(-playerWidth, 0)).x) 
            {
                currentState = GameOver;
                backgroundMusic.stop();
                if (score > highScore) {
                    highScore = score;
                    saveHighScore("highscore.txt", highScore);
                }
            }

            
            scoreText.setString("Score \n " + std::to_string(score));
            highScoreText.setString("High Score \n " + std::to_string(highScore));
        }

        
        window.clear(sf::Color(50, 50, 100)); 

        window.draw(backgroundSprite);

        
        for (const auto& block : blocks) {
            window.draw(block.line);
            window.draw(block.shape);
        }

        
        for (const auto& collectible : collectibles) {
            window.draw(collectible.sprite);
        }

        
        window.draw(playerSprite);

        
        window.draw(scoreText);
        window.draw(highScoreText);

        
        if (currentState == GameOver) {
            window.draw(gameOverText);
        }

        window.display();
    }

    return 0;
}
