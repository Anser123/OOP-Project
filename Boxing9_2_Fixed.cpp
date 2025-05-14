#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <Box2D/Box2D.h>
#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <algorithm> 
#include <map> 
#include <sstream> 



const float PIXELS_PER_METER = 50.0f;
const float METERS_PER_PIXEL = 1.0f / PIXELS_PER_METER;

b2Vec2 toMeters(const sf::Vector2f& pixels) {
    return b2Vec2(pixels.x * METERS_PER_PIXEL, pixels.y * METERS_PER_PIXEL);
}

sf::Vector2f toPixels(const b2Vec2& meters) {
    return sf::Vector2f(meters.x * PIXELS_PER_METER, meters.y * PIXELS_PER_METER);
}

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}


struct Block { sf::RectangleShape shape; sf::RectangleShape line; b2Body* body = nullptr; bool markedForRemoval = false; uintptr_t id = 0; };
enum CollectibleType { Magenta, Orange, Green, Red, White, MinusScore };
struct Collectible { sf::Sprite sprite; CollectibleType type; b2Body* body = nullptr; bool markedForRemoval = false; };


static constexpr uintptr_t PLAYER1_ID = 0; 
static constexpr uintptr_t PLAYER2_ID = 10; 
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
    std::map<uintptr_t, int> footContactsMap;
    std::map<uintptr_t, bool> touchedGroundMap;
    std::vector<b2Body*>& collectiblesToRemove;

    PlayerContactListener(std::vector<b2Body*>& bodiesToRemove) : collectiblesToRemove(bodiesToRemove) {}

    void BeginContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        uintptr_t playerUserData = 0;
        uintptr_t otherUserData = 0;

        
        if (userDataA == PLAYER1_ID || userDataA == PLAYER2_ID) {
            playerUserData = userDataA;
            otherUserData = userDataB;
        } else if (userDataB == PLAYER1_ID || userDataB == PLAYER2_ID) {
            playerUserData = userDataB;
            otherUserData = userDataA;
        }

        if (playerUserData == PLAYER1_ID || playerUserData == PLAYER2_ID) { 
            
            if ((userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) ||
                (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE)) {
                
                uintptr_t footSensorOwnerId = (userDataA == FOOT_SENSOR_ID) ? userDataB : userDataA;
                 if (footSensorOwnerId == PLAYER1_ID || footSensorOwnerId == PLAYER2_ID) {
                     footContactsMap[footSensorOwnerId]++;
                 }
            }

            
            if (otherUserData == GROUND_ID) {
                 touchedGroundMap[playerUserData] = true;
            }

            
            if (otherUserData == MAGENTA_COLLECTIBLE_ID || otherUserData == ORANGE_COLLECTIBLE_ID ||
                otherUserData == GREEN_COLLECTIBLE_ID || otherUserData == RED_COLLECTIBLE_ID ||
                otherUserData == WHITE_COLLECTIBLE_ID || otherUserData == MINUS_SCORE_COLLECTIBLE_ID) {
                
                b2Body* collectibleBody = (userDataA == playerUserData) ? fixtureB->GetBody() : fixtureA->GetBody();
                collectiblesToRemove.push_back(collectibleBody);
            }
        }
    }

    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        
        if (((userDataA == PLAYER1_ID || userDataA == PLAYER2_ID) && userDataB >= PLATFORM_ID_BASE) ||
            ((userDataB == PLAYER1_ID || userDataB == PLAYER2_ID) && userDataA >= PLATFORM_ID_BASE)) {
            contact->SetFriction(0.0f);
        }
    }

    void EndContact(b2Contact* contact) override {
        b2Fixture* fixtureA = contact->GetFixtureA();
        b2Fixture* fixtureB = contact->GetFixtureB();
        uintptr_t userDataA = fixtureA->GetUserData().pointer;
        uintptr_t userDataB = fixtureB->GetUserData().pointer;

        
        if ((userDataA == FOOT_SENSOR_ID && userDataB >= PLATFORM_ID_BASE) ||
            (userDataB == FOOT_SENSOR_ID && userDataA >= PLATFORM_ID_BASE)) {
            
            uintptr_t footSensorOwnerId = (userDataA == FOOT_SENSOR_ID) ? userDataB : userDataA;
            if ((footSensorOwnerId == PLAYER1_ID || footSensorOwnerId == PLAYER2_ID) && footContactsMap[footSensorOwnerId] > 0) {
                footContactsMap[footSensorOwnerId]--;
            }
        }

         
        if (((userDataA == PLAYER1_ID || userDataA == PLAYER2_ID) && userDataB == GROUND_ID) ||
            ((userDataB == PLAYER1_ID || userDataB == PLAYER2_ID) && userDataA == GROUND_ID)) {
             uintptr_t playerUserData = (userDataA == GROUND_ID) ? userDataB : userDataA;
             if (playerUserData == PLAYER1_ID || playerUserData == PLAYER2_ID) {
                 touchedGroundMap[playerUserData] = false;
             }
        }
    }

    bool isGrounded(uintptr_t playerId) const {
        auto it = footContactsMap.find(playerId);
        return (it != footContactsMap.end() && it->second > 0);
    }

    bool hasTouchedGround(uintptr_t playerId) const {
         auto it = touchedGroundMap.find(playerId);
         return (it != touchedGroundMap.end() && it->second);
    }

    void reset() {
        footContactsMap.clear();
        touchedGroundMap.clear();
        collectiblesToRemove.clear();
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

void saveHighScore(const std::string& filename, int highscore) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << highscore;
        file.close();
    }
}


enum PlatformEffect { None, Lengthen, Shorten };


enum GameState { StartScreen, PlayingSingle, PlayingMulti, GameOver };


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
    const float playerJumpForce = 700.0f;
    const int maxJumps = 10;
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
    const float fastFallGravityScale = 100.0f;

    sf::Color defaultBlockColor = sf::Color(255, 200, 0);
    sf::Color greenBlockColor = sf::Color::Green;
    sf::Color redBlockColor = sf::Color::Red;

    
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rat Rider");
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

    
    sf::Texture staticPlayer2Texture;
     if (!staticPlayer2Texture.loadFromFile("Idle2.png")) { 
        std::cerr << "Error loading texture 'Idle2.png'" << std::endl;
        
        staticPlayer2Texture = staticPlayerTexture;
    }
    sf::Texture jumpPlayer2Texture;
     if (!jumpPlayer2Texture.loadFromFile("Jump2.png")) { 
        std::cerr << "Error loading texture 'Jump2.png'" << std::endl;
         
        jumpPlayer2Texture = jumpPlayerTexture;
    }


    sf::Texture collectibleTextures[6];
    if (!collectibleTextures[0].loadFromFile("CHEEZE.png")) { std::cerr << "Error loading texture 'CHEEZE.png'" << std::endl; return 1; }
    if (!collectibleTextures[1].loadFromFile("Pizza2.png")) { std::cerr << "Error loading texture 'Pizza2.png'" << std::endl; return 1; }
    if (!collectibleTextures[2].loadFromFile("Long_Platform_Green.png")) { std::cerr << "Error loading texture 'Long_Platform_Green.png'" << std::endl; return 1; }
    if (!collectibleTextures[3].loadFromFile("Short_Platform_Red.png")) { std::cerr << "Error loading texture 'Short_Platform_Red.png'" << std::endl; return 1; }
    if (!collectibleTextures[4].loadFromFile("Cheese_Rain.png")) { std::cerr << "Error loading texture 'Cheese_Rain.png'" << std::endl; return 1; }
    if (!collectibleTextures[5].loadFromFile("Poison.png")) { std::cerr << "Error loading texture 'Poison.png'" << std::endl; return 1; }

    
    sf::SoundBuffer collectBuffer;
    if (!collectBuffer.loadFromFile("collectible.wav")) { std::cerr << "Error loading sound 'collectible.wav'" << std::endl; }
    sf::Sound collectSound;
    collectSound.setBuffer(collectBuffer);

    sf::Music backgroundMusic;
    if (!backgroundMusic.openFromFile("background.ogg")) { std::cerr << "Error loading music 'background.ogg'" << std::endl; }
    else { backgroundMusic.setLoop(true); backgroundMusic.setVolume(50); } 

    
    sf::Font font;
    if (!font.loadFromFile("font.ttf")) { std::cerr << "Error loading font font.ttf" << std::endl; return 1; }

    
    b2Vec2 gravity(0.0f, 7.0f);
    b2World world(gravity);

    std::vector<b2Body*> collectiblesToRemoveFromWorld;
    PlayerContactListener contactListener(collectiblesToRemoveFromWorld);
    world.SetContactListener(&contactListener);

    
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


    
    sf::Sprite playerSprite;
    playerSprite.setTexture(staticPlayerTexture); 
    playerSprite.setScale(playerWidth / staticPlayerTexture.getSize().x, playerHeight / staticPlayerTexture.getSize().y);
    playerSprite.setOrigin(staticPlayerTexture.getSize().x / 2.f, staticPlayerTexture.getSize().y / 2.f); 
    b2Body* playerBody = nullptr; 
    int jumpsRemaining = maxJumps;
    
    bool fastFallActive = false;
    bool player1Alive = true; 

    
    sf::Sprite player2Sprite;
    player2Sprite.setTexture(staticPlayer2Texture); 
    player2Sprite.setScale(playerWidth / staticPlayer2Texture.getSize().x, playerHeight / staticPlayer2Texture.getSize().y);
    player2Sprite.setOrigin(staticPlayer2Texture.getSize().x / 2.f, staticPlayer2Texture.getSize().y / 2.f); 
    b2Body* player2Body = nullptr; 
    int jumpsRemaining2 = maxJumps;
    
    bool fastFallActive2 = false;
    bool player2Alive = true; 

    int winner = 0; 

    
    std::vector<Block> blocks;
    std::vector<Collectible> collectibles;
    uintptr_t nextPlatformId = PLATFORM_ID_BASE;

    
    GameState currentState = GameState::StartScreen;
    sf::Clock spawnClock;
    float nextSpawnTime = 0; 
    sf::Clock deltaClock;
    float gameTime = 0.f;

    
    int score = 0;
    int highScore = loadHighScore("highscore.txt");
    PlatformEffect currentPlatformEffect = PlatformEffect::None;
    sf::Clock platformEffectClock;
    bool isRainingMagenta = false;
    sf::Clock magentaRainClock;
    sf::Clock magentaRainSpawnClock;

    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> yPosDist(windowHeight - 450u, windowHeight - 150u);
    std::uniform_real_distribution<float> initialSpawnTimeDist(0.5f, 1.5f);
    std::uniform_real_distribution<float> collectibleTypeDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rainXPosDist(collectibleRadius, windowWidth - collectibleRadius);


    
    sf::Text gameOverText("Game Over!", font, 50);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setStyle(sf::Text::Bold);
    

    sf::Text scoreText("Score 0", font, 30);
    scoreText.setFillColor(sf::Color::White);
    scoreText.setPosition(25.f, 10.f);

    sf::Text highScoreText("High Score 0", font, 30);
    highScoreText.setFillColor(sf::Color::White);
    highScoreText.setPosition(850.f, 10.f);

    
    sf::Text titleText("Rat Rider", font, 80);
    titleText.setFillColor(sf::Color::Yellow);
    titleText.setStyle(sf::Text::Bold);
    titleText.setPosition(windowWidth / 2.f - titleText.getLocalBounds().width / 2.f, windowHeight / 4.f);

    sf::Text singlePlayerText("Single Player", font, 40);
    singlePlayerText.setFillColor(sf::Color::White);
    singlePlayerText.setPosition(windowWidth / 2.f - singlePlayerText.getLocalBounds().width / 2.f, windowHeight / 2.f - 50.f);

    sf::Text multiPlayerText("Multiplayer", font, 40);
    multiPlayerText.setFillColor(sf::Color::White);
    multiPlayerText.setPosition(windowWidth / 2.f - multiPlayerText.getLocalBounds().width / 2.f, windowHeight / 2.f + 20.f);


    
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (currentState == GameState::StartScreen) {
                if (event.type == sf::Event::MouseButtonPressed) {
                    if (event.mouseButton.button == sf::Mouse::Left) {
                        sf::Vector2f mousePos = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
                        if (singlePlayerText.getGlobalBounds().contains(mousePos)) {
                            currentState = GameState::PlayingSingle;
                            score = 0;
                            highScore = loadHighScore("highscore.txt");
                            blocks.clear();
                            collectibles.clear();
                            collectiblesToRemoveFromWorld.clear();
                            contactListener.reset();
                            gameTime = 0.f;
                            blockSpeed = 200.f;
                            minLength = baseMinLength;
                            maxLength = baseMaxLength;
                            minSpawnTime = 2.5f;
                            maxSpawnTime = 3.5f;
                            currentPlatformEffect = PlatformEffect::None;
                            isRainingMagenta = false;
                            spawnClock.restart();
                            deltaClock.restart();
                            backgroundMusic.play();

                            b2BodyDef playerBodyDef;
                            playerBodyDef.type = b2_dynamicBody;
                            playerBodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f, windowHeight - 200.f));
                            playerBodyDef.fixedRotation = true;
                            playerBodyDef.allowSleep = false;
                            playerBody = world.CreateBody(&playerBodyDef);

                            b2PolygonShape playerBox;
                            playerBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);
                            b2FixtureDef playerFixtureDef;
                            playerFixtureDef.shape = &playerBox;
                            playerFixtureDef.density = 1.0f;
                            playerFixtureDef.friction = 0.5f;
                            playerFixtureDef.restitution = 0.0f;
                            playerFixtureDef.userData.pointer = PLAYER1_ID;
                            playerBody->CreateFixture(&playerFixtureDef);

                            b2PolygonShape footSensorBox;
                            b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                            footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y, footSensorCenter, 0);
                            b2FixtureDef footSensorFixtureDef;
                            footSensorFixtureDef.shape = &footSensorBox;
                            footSensorFixtureDef.isSensor = true;
                            footSensorFixtureDef.userData.pointer = FOOT_SENSOR_ID;
                            playerBody->CreateFixture(&footSensorFixtureDef);

                            playerSprite.setTexture(staticPlayerTexture);
                            jumpsRemaining = maxJumps;
                            fastFallActive = false;
                            player1Alive = true;

                        } else if (multiPlayerText.getGlobalBounds().contains(mousePos)) {
                            currentState = GameState::PlayingMulti;
                            blocks.clear();
                            collectibles.clear();
                            collectiblesToRemoveFromWorld.clear();
                            contactListener.reset();
                            gameTime = 0.f;
                            blockSpeed = 200.f;
                            minLength = baseMinLength;
                            maxLength = baseMaxLength;
                            minSpawnTime = 2.5f;
                            maxSpawnTime = 3.5f;
                            currentPlatformEffect = PlatformEffect::None;
                            isRainingMagenta = false;
                            spawnClock.restart();
                            deltaClock.restart();
                            backgroundMusic.play();
                            winner = 0;

                            b2BodyDef playerBodyDef;
                            playerBodyDef.type = b2_dynamicBody;
                            playerBodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f, windowHeight - 200.f));
                            playerBodyDef.fixedRotation = true;
                            playerBodyDef.allowSleep = false;
                            playerBody = world.CreateBody(&playerBodyDef);

                            b2PolygonShape playerBox;
                            playerBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);
                            b2FixtureDef playerFixtureDef;
                            playerFixtureDef.shape = &playerBox;
                            playerFixtureDef.density = 1.0f;
                            playerFixtureDef.friction = 0.5f;
                            playerFixtureDef.restitution = 0.0f;
                            playerFixtureDef.userData.pointer = PLAYER1_ID;
                            playerBody->CreateFixture(&playerFixtureDef);

                            b2PolygonShape footSensorBox;
                            b2Vec2 footSensorCenter = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                            footSensorBox.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y, footSensorCenter, 0);
                            b2FixtureDef footSensorFixtureDef;
                            footSensorFixtureDef.shape = &footSensorBox;
                            footSensorFixtureDef.isSensor = true;
                            footSensorFixtureDef.userData.pointer = FOOT_SENSOR_ID;
                            playerBody->CreateFixture(&footSensorFixtureDef);

                            playerSprite.setTexture(staticPlayerTexture);
                            jumpsRemaining = maxJumps;
                            fastFallActive = false;
                            player1Alive = true;

                            b2BodyDef player2BodyDef;
                            player2BodyDef.type = b2_dynamicBody;
                            player2BodyDef.position = toMeters(sf::Vector2f(windowWidth / 4.f - 100.f, windowHeight - 200.f));
                            player2BodyDef.fixedRotation = true;
                            player2BodyDef.allowSleep = false;
                            player2Body = world.CreateBody(&player2BodyDef);

                            b2PolygonShape player2Box;
                            player2Box.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f, playerHeight / 2.f)).y);
                            b2FixtureDef player2FixtureDef;
                            player2FixtureDef.shape = &player2Box;
                            player2FixtureDef.density = 1.0f;
                            player2FixtureDef.friction = 0.5f;
                            player2FixtureDef.restitution = 0.0f;
                            player2FixtureDef.userData.pointer = PLAYER2_ID;
                            player2Body->CreateFixture(&player2FixtureDef);

                            b2PolygonShape footSensor2Box;
                            b2Vec2 footSensor2Center = b2Vec2(0, toMeters(sf::Vector2f(0, playerHeight / 2.f)).y);
                            footSensor2Box.SetAsBox(toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).x, toMeters(sf::Vector2f(playerWidth / 2.f * 0.9f, 5.f)).y, footSensor2Center, 0);
                            b2FixtureDef footSensor2FixtureDef;
                            footSensor2FixtureDef.shape = &footSensor2Box;
                            footSensor2FixtureDef.isSensor = true;
                            footSensor2FixtureDef.userData.pointer = FOOT_SENSOR_ID;
                            player2Body->CreateFixture(&footSensor2FixtureDef);

                            player2Sprite.setTexture(staticPlayer2Texture);
                            jumpsRemaining2 = maxJumps;
                            fastFallActive2 = false;
                            player2Alive = true;
                        }
                    }
                }
            } else if (currentState == GameState::PlayingSingle) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::W) {
                        if (playerBody && jumpsRemaining > 0) {
                            float impulseMagnitude = playerJumpForce * METERS_PER_PIXEL * playerBody->GetMass();
                            playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulseMagnitude), true);
                            jumpsRemaining--;
                        }
                    } else if (event.key.code == sf::Keyboard::S ) {
                        fastFallActive = true;
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == sf::Keyboard::S) {
                        fastFallActive = false;
                    }
                }
            } else if (currentState == GameState::PlayingMulti) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::W) {
                        if (playerBody && jumpsRemaining > 0) {
                            float impulseMagnitude = playerJumpForce * METERS_PER_PIXEL * playerBody->GetMass();
                            playerBody->ApplyLinearImpulseToCenter(b2Vec2(0, -impulseMagnitude), true);
                            jumpsRemaining--;
                        }
                    } else if (event.key.code == sf::Keyboard::S ) {
                        fastFallActive = true;
                    }
                    if (event.key.code == sf::Keyboard::Up) {
                        if (player2Body && jumpsRemaining2 > 0) {
                            float impulseMagnitude = playerJumpForce * METERS_PER_PIXEL * player2Body->GetMass();
                            player2Body->ApplyLinearImpulseToCenter(b2Vec2(0, -impulseMagnitude), true);
                            jumpsRemaining2--;
                        }
                    } else if (event.key.code == sf::Keyboard::Down ) {
                        fastFallActive2 = true;
                    }
                }
                if (event.type == sf::Event::KeyReleased) {
                    if (event.key.code == sf::Keyboard::S) {
                        fastFallActive = false;
                    }
                    if (event.key.code == sf::Keyboard::Down) {
                        fastFallActive2 = false;
                    }
                }
            } else if (currentState == GameState::GameOver) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::Space) {
                        currentState = GameState::StartScreen;
                        // Bodies are destroyed on game over, ensure pointers are null
                        playerBody = nullptr;
                        player2Body = nullptr;
                        blocks.clear();
                        collectibles.clear();
                        collectiblesToRemoveFromWorld.clear();
                        contactListener.reset();
                        backgroundMusic.stop();
                    }
                }
            }
        }

        float dt = deltaClock.restart().asSeconds();
        if (currentState != GameState::PlayingSingle && currentState != GameState::PlayingMulti) {
            dt = 0;
        } else {
            dt = std::min(dt, 0.1f);
            gameTime += dt;

            // Apply fast fall gravity scale if active and not grounded
            if (playerBody) {
                if (fastFallActive && !contactListener.isGrounded(PLAYER1_ID)) {
                    playerBody->SetGravityScale(fastFallGravityScale);
                } else {
                    playerBody->SetGravityScale(1.0f);
                }
            }
            if (player2Body) {
                if (fastFallActive2 && !contactListener.isGrounded(PLAYER2_ID)) {
                    player2Body->SetGravityScale(fastFallGravityScale);
                } else {
                    player2Body->SetGravityScale(1.0f);
                }
            }

            world.Step(dt, 8, 3);

            
            if (playerBody) playerSprite.setPosition(toPixels(playerBody->GetPosition()));
            if (player2Body) player2Sprite.setPosition(toPixels(player2Body->GetPosition()));

            
            for (auto& block : blocks) {
                if (block.body) {
                    block.shape.setPosition(toPixels(block.body->GetPosition()));
                    block.line.setPosition(block.shape.getPosition().x, block.shape.getPosition().y + fixedHeight / 2.f);
                    if (block.shape.getPosition().x < -block.shape.getSize().x / 2.f) {
                        block.markedForRemoval = true;
                    }
                }
            }

            
            if (currentState == GameState::PlayingSingle) {
                for (auto& collectible : collectibles) {
                    if (collectible.body) {
                        collectible.sprite.setPosition(toPixels(collectible.body->GetPosition()));
                        if (collectible.sprite.getPosition().x < -collectible.sprite.getGlobalBounds().width / 2.f || collectible.sprite.getPosition().y > windowHeight + collectible.sprite.getGlobalBounds().height / 2.f) {
                            collectible.markedForRemoval = true;
                        }
                    }
                }
            }


            
            if (currentState == GameState::PlayingSingle) {
                for (b2Body* bodyToRemove : collectiblesToRemoveFromWorld) {
                    for (auto it = collectibles.begin(); it != collectibles.end(); ++it) {
                        if (it->body == bodyToRemove) {
                            switch (it->type) {
                                case CollectibleType::Magenta: score++; collectSound.play(); break;
                                case CollectibleType::Orange: score += 3; collectSound.play(); break;
                                case CollectibleType::Green: currentPlatformEffect = PlatformEffect::Lengthen; platformEffectClock.restart(); collectSound.play(); break;
                                case CollectibleType::Red: currentPlatformEffect = PlatformEffect::Shorten; platformEffectClock.restart(); collectSound.play(); break;
                                case CollectibleType::White: isRainingMagenta = true; magentaRainClock.restart(); magentaRainSpawnClock.restart(); collectSound.play(); break;
                                case CollectibleType::MinusScore: score = std::max(0, score - 2); collectSound.play(); break;
                            }
                            it->markedForRemoval = true;
                            break;
                        }
                    }
                }
                collectiblesToRemoveFromWorld.clear();
            } else { 
                 for (b2Body* bodyToRemove : collectiblesToRemoveFromWorld) {
                     for (auto it = collectibles.begin(); it != collectibles.end(); ++it) {
                         if (it->body == bodyToRemove) {
                             it->markedForRemoval = true;
                             break;
                         }
                     }
                 }
                 collectiblesToRemoveFromWorld.clear();
            }


            
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


            
            if (currentState == GameState::PlayingSingle) {
                if (playerBody) { 
                    if (contactListener.isGrounded(PLAYER1_ID)) {
                        jumpsRemaining = maxJumps;
                        playerSprite.setTexture(staticPlayerTexture);
                    } else {
                        playerSprite.setTexture(jumpPlayerTexture);
                    }

                    if (fastFallActive && !contactListener.isGrounded(PLAYER1_ID)) {
                        playerBody->SetGravityScale(fastFallGravityScale);
                    } else {
                        playerBody->SetGravityScale(1.0f); 
                    }
        

                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    

                     
                    if (contactListener.hasTouchedGround(PLAYER1_ID) || (playerBody && (playerBody->GetPosition().y > toMeters(sf::Vector2f(0, windowHeight + playerHeight)).y || playerBody->GetPosition().x < toMeters(sf::Vector2f(-playerWidth, 0)).x))) {
                        currentState = GameState::GameOver;
                        backgroundMusic.stop();
                        if (score > highScore) {
                            highScore = score;
                            saveHighScore("highscore.txt", highScore);
                        }
                        
                        gameOverText.setString("Game Over!");
                        sf::FloatRect textRect = gameOverText.getLocalBounds();
                        gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
                        gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));

                        
                        if(playerBody) world.DestroyBody(playerBody);
                        playerBody = nullptr;
                    }
                } 
            } else if (currentState == GameState::PlayingMulti) {
                
                if (player1Alive && playerBody) { 
                    if (contactListener.isGrounded(PLAYER1_ID)) {
                        jumpsRemaining = maxJumps;
                        playerSprite.setTexture(staticPlayerTexture);
                    } else {
                        playerSprite.setTexture(jumpPlayerTexture);
                    }

                    if (fastFallActive && !contactListener.isGrounded(PLAYER1_ID)) {
                        playerBody->SetGravityScale(fastFallGravityScale);
                    } else {
                        playerBody->SetGravityScale(1.0f); 
                    }
        

                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    

                    
                    if (contactListener.hasTouchedGround(PLAYER1_ID) || (playerBody && (playerBody->GetPosition().y > toMeters(sf::Vector2f(0, windowHeight + playerHeight)).y || playerBody->GetPosition().x < toMeters(sf::Vector2f(-playerWidth, 0)).x))) {
                        player1Alive = false;
                        world.DestroyBody(playerBody);
                        playerBody = nullptr; 
                        if (player2Alive) winner = 2; 
                        else winner = 0; 
                    }
                }

                
                if (player2Alive && player2Body) { 
                     if (contactListener.isGrounded(PLAYER2_ID)) {
                        jumpsRemaining2 = maxJumps;
                        player2Sprite.setTexture(staticPlayer2Texture);
                    } else {
                        player2Sprite.setTexture(jumpPlayer2Texture);
                    }

                    if (fastFallActive2 && !contactListener.isGrounded(PLAYER2_ID)) {
                        player2Body->SetGravityScale(fastFallGravityScale);
                    } else {
                        player2Body->SetGravityScale(1.0f); 
                    }
        

                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    
                    

                    
                    if (contactListener.hasTouchedGround(PLAYER2_ID) || (player2Body && (player2Body->GetPosition().y > toMeters(sf::Vector2f(0, windowHeight + playerHeight)).y || player2Body->GetPosition().x < toMeters(sf::Vector2f(-playerWidth, 0)).x))) {
                        player2Alive = false;
                        world.DestroyBody(player2Body);
                        player2Body = nullptr; 
                        if (player1Alive) winner = 1; 
                        else winner = 0; 
                    }
                }

                
                if (!player1Alive || !player2Alive) {
                    currentState = GameState::GameOver;
                    backgroundMusic.stop();
                    
                    std::string winMessage;
                    if (winner == 1) winMessage = "Player 1 Wins!";
                    else if (winner == 2) winMessage = "Player 2 Wins!";
                    else winMessage = "Tie!"; 
                    gameOverText.setString(winMessage);
                    sf::FloatRect textRect = gameOverText.getLocalBounds();
                    gameOverText.setOrigin(textRect.left + textRect.width/2.0f, textRect.top + textRect.height/2.0f);
                    gameOverText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f));
                }

            } 

            
            if (currentState == GameState::PlayingSingle) {
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


            
            float currentMinLength = baseMinLength;
            float currentMaxLength = baseMaxLength;
            sf::Color currentBlockColor = defaultBlockColor;

            if (currentState == GameState::PlayingSingle) { 
                if (currentPlatformEffect == PlatformEffect::Lengthen) {
                    currentMinLength = baseMinLength * lengthenFactor;
                    currentMaxLength = baseMaxLength * lengthenFactor;
                    currentBlockColor = greenBlockColor;
                } else if (currentPlatformEffect == PlatformEffect::Shorten) {
                    currentMinLength = baseMinLength * shortenFactor;
                    currentMaxLength = baseMaxLength * shortenFactor;
                    currentBlockColor = redBlockColor;
                }
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

                    
                    if (currentState == GameState::PlayingSingle) {
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
                    } 
                } else {
                    
                    world.DestroyBody(newBlock.body);
                }

                spawnClock.restart();
                std::uniform_real_distribution<float> nextSpawnTimeDist(minSpawnTime, maxSpawnTime);
                nextSpawnTime = nextSpawnTimeDist(gen);
            } 

            
            if (blockSpeed < maxBlockSpeed) {
                blockSpeed += blockSpeedIncreaseFactor * dt;
                blockSpeed = std::min(blockSpeed, maxBlockSpeed);
                
                float speedRatio = (blockSpeed - 200.f) / (maxBlockSpeed - 200.f);
                minSpawnTime = lerp(2.5f, minSpawnTimeBase, speedRatio);
                maxSpawnTime = lerp(3.5f, maxSpawnTimeBase, speedRatio);
            }

            
            if (currentState == GameState::PlayingSingle) {
                scoreText.setString("Score \n " + std::to_string(score));
                highScoreText.setString("High Score \n " + std::to_string(highScore));
            }


        } 


        
        window.clear(sf::Color(50, 50, 100));
        window.draw(backgroundSprite);

        if (currentState == GameState::StartScreen) {
            window.draw(titleText);
            window.draw(singlePlayerText);
            window.draw(multiPlayerText);
        } else { 
            for (const auto& block : blocks) {
                window.draw(block.line);
                window.draw(block.shape);
            }

            
            if (currentState == GameState::PlayingSingle) {
                for (const auto& collectible : collectibles) {
                    window.draw(collectible.sprite);
                }
            }

            
            if (playerBody) window.draw(playerSprite);
            if (player2Body) window.draw(player2Sprite); 

            
            if (currentState == GameState::PlayingSingle) {
                window.draw(scoreText);
                window.draw(highScoreText);
            } else if (currentState == GameState::GameOver) {
                window.draw(gameOverText);
                
                sf::Text returnText("Press SPACE to return to menu", font, 20);
                returnText.setFillColor(sf::Color::White);
                sf::FloatRect returnRect = returnText.getLocalBounds();
                returnText.setOrigin(returnRect.left + returnRect.width/2.0f, returnRect.top + returnRect.height/2.0f);
                returnText.setPosition(sf::Vector2f(windowWidth/2.0f, windowHeight/2.0f + 100.f));
                window.draw(returnText);
            }
        }


        window.display();
    } 

    
    if (playerBody) world.DestroyBody(playerBody);
    if (player2Body) world.DestroyBody(player2Body);
    

    return 0;
}
