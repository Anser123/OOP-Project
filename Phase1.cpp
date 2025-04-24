#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <vector>
#include <random>

struct Block {
    sf::RectangleShape shape;
    sf::Vector2f velocity;
};

int main() {
    // Window settings
    const unsigned int windowWidth = 800;
    const unsigned int windowHeight = 600;
    const float fixedHeight = 25.f;  // Block height

    const float minLength = 60.f;      
    const float maxLength = 150.f;     
    const float blockSpeed = 150.f;    
    const float minSpawnTime = 0.5f;   
    const float maxSpawnTime = 2.0f;   
    const float maxAngle = 30.f;       

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Rectangular Blocks");
    window.setFramerateLimit(60);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lengthDist(minLength, maxLength);
    std::uniform_real_distribution<float> yPosDist(fixedHeight, windowHeight - fixedHeight);
    std::uniform_real_distribution<float> angleDist(-maxAngle, maxAngle);
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
                candidate.shape.setFillColor(sf::Color::Magenta);
                candidate.shape.setOrigin(blockLength / 2.f, fixedHeight / 2.f);

                // Set starting position on the right side (moving right to left)
                float spawnY = yPosDist(gen);
                candidate.shape.setPosition(windowWidth + blockLength / 2.f, spawnY);

                // Set a random rotation angle
                float angle = angleDist(gen);
                candidate.shape.setRotation(angle);

                // Velocity: moving left
                candidate.velocity = sf::Vector2f(-blockSpeed, 0.f);

                // Check for overlaps with existing blocks using bounding boxes
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
            // Reset spawn clock and set next spawn time regardless of spawn success.
            spawnClock.restart();
            nextSpawnTime = spawnTimeDist(gen);
        }

        // Move blocks and remove those that are completely off screen to the left
        for (auto it = blocks.begin(); it != blocks.end();) {
            it->shape.move(it->velocity * dt);
            if (it->shape.getPosition().x + (it->shape.getSize().x / 2.f) < 0.f) {
                it = blocks.erase(it);
            } else {
                ++it;
            }
        }

        window.clear(sf::Color::White);
        for (const auto& block : blocks) {
            window.draw(block.shape);
        }
        window.display();
    }
    return 0;
}
