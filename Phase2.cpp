
#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <vector>
#include <random>
#include <iostream>

struct Block {
    sf::RectangleShape shape;
    sf::RectangleShape line; // vertical line
    sf::Vector2f velocity;
};

int main() {
    // Window settings
    const unsigned int windowWidth = 1200;
    const unsigned int windowHeight = 700;
    const float fixedHeight = 20.f;  // Block height

    const float minLength = 100.f;
    const float maxLength = 300.f;
    const float blockSpeed = 250.f;
    const float minSpawnTime = 2.0f;
    const float maxSpawnTime = 3.0f;
    const float maxAngle = 30.f;

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rat Rider");
    window.setFramerateLimit(60);

    sf::Texture backgroundTexture;
    if (!backgroundTexture.loadFromFile("/Users/anserabbas/Documents/IBA - 2nd Semester/Object Oriented Programming/Rat Rider/resources/silhouette.jpg")) {
        std::cerr << "Error loading background image 'silhouette.png'" << std::endl;
        
        return 1; 
    }
    sf::Sprite backgroundSprite(backgroundTexture);
    
    backgroundSprite.setScale(
         (float)windowWidth / backgroundTexture.getSize().x,
         (float)windowHeight / backgroundTexture.getSize().y
    );



    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lengthDist(minLength, maxLength);
    std::uniform_real_distribution<float> yPosDist(windowHeight - 150u, windowHeight -450u);
    std::uniform_real_distribution<float> angleDist(-maxAngle , maxAngle - 25.0f );
    std::uniform_real_distribution<float> spawnTimeDist(minSpawnTime, maxSpawnTime);

    std::vector<Block> blocks;
    sf::Clock spawnClock;
    float nextSpawnTime = spawnTimeDist(gen);
    sf::Clock deltaClock;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
        }

        float dt = deltaClock.restart().asSeconds();

        // Spawn new blocks at random intervals
        if (spawnClock.getElapsedTime().asSeconds() >= nextSpawnTime) {
            const int maxAttempts = 10;
            bool spawned = false;
            for (int attempt = 0; attempt < maxAttempts && !spawned; ++attempt) {
                Block candidate;
                float blockLength = lengthDist(gen);

                candidate.shape.setSize(sf::Vector2f(blockLength, fixedHeight));
                //candidate.shape.set
                //candidate.shape.setFillColor(sf::Color(137,1,56)); Maroon
                //candidate.shape.setFillColor(sf::Color(225,77,36));
                candidate.shape.setFillColor(sf::Color(255, 200, 0));
                candidate.shape.setOutlineColor(sf::Color::Black);
                candidate.shape.setOutlineThickness(2.5f);
                candidate.shape.setOrigin(blockLength / 2.f, fixedHeight / 2.f);

                candidate.line.setSize(sf::Vector2f(15.f, 500)); // Width = 3, Length = 100
                candidate.line.setFillColor(sf::Color(150,150,150));
                candidate.line.setOutlineColor(sf::Color::Black);
                candidate.line.setOutlineThickness(2.5f);
                candidate.line.setOrigin(1.5f, 0.f); // Center horizontally
                // No rotation needed for line

                // Set starting position on the right side
                float spawnY = yPosDist(gen);
                candidate.shape.setPosition(windowWidth + blockLength / 2.f, spawnY);

                // Set the line position AFTER block position is set
                candidate.line.setPosition(
                    candidate.shape.getPosition().x,
                    candidate.shape.getPosition().y + (fixedHeight / 2.f)
                );

                // Set random rotation for the block (optional, but line won't rotate)
                float angle = angleDist(gen);
                candidate.shape.setRotation(angle);

                // Velocity
                candidate.velocity = sf::Vector2f(-blockSpeed, 0.f);

                // Check for overlaps
                sf::FloatRect candidateBounds = candidate.shape.getGlobalBounds();
                bool overlap = false;
                for (const auto& block : blocks) {
                    if (candidateBounds.intersects(block.shape.getGlobalBounds())) {
                        overlap = true;
                        break;
                    }
                }

                if (!overlap) {
                    blocks.push_back(candidate);
                    spawned = true;
                }
            }
            spawnClock.restart();
            nextSpawnTime = spawnTimeDist(gen);
        }

        // Move blocks and update line positions
        for (auto it = blocks.begin(); it != blocks.end();) {
            it->shape.move(it->velocity * dt);

            // Update line to follow block
            it->line.setPosition(
                it->shape.getPosition().x,
                it->shape.getPosition().y + (fixedHeight / 2.f)
            );

            if (it->shape.getPosition().x + (it->shape.getSize().x / 2.f) < 0.f) {
                it = blocks.erase(it);
            } else {
                ++it;
            }
        }

        window.clear();
        window.draw(backgroundSprite);

        for (const auto& block : blocks) {
            window.draw(block.line);  
            window.draw(block.shape); 
        }
        window.display();
    }
    return 0;
}