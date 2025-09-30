// main.cpp
// Box2D + OpenGL Game with Textures, Camera Follow, 1-meter proximity AABB collision, EBO, Health Bar, and GUI

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include <iostream>
#include <cmath>
#include <vector>
#include <cstdlib> // For rand()
#include <map>
#include <string>
#include <algorithm>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <SDL.h>
#include <SDL_mixer.h>

// ---------------- Window State ----------------
bool isFullscreen = false;
int windowedWidth = 1200;
int windowedHeight = 900;
int windowedPosX = 100;
int windowedPosY = 100;
GLFWmonitor* primaryMonitor = nullptr;

// ---------------- Box System ----------------
b2BodyId currentBox = b2_nullBodyId;
bool boxSpawned = false;
const float BOX_RESPAWN_TIME = 5.0f;
float boxRespawnTimer = 0.0f;

// ---------------- Platform Scoring System ----------------
int lastPlatformIndex = 0; // Track the highest platform index passed
float platformPassThreshold = 1.0f; // How far past the platform to count as "passed"

// ---------------- Game Over System ----------------
int deathCount = 0;
const int MAX_DEATHS = 3;
bool gameOver = false;
float gameOverTimer = 0.0f;
const float GAME_OVER_DISPLAY_TIME = 5.0f; // Show game over screen for 5 seconds

// ---------------- Improved Movement System ----------------
float moveSpeed = 4.0f;           // Max horizontal speed
float acceleration = 50.0f;       // How quickly player accelerates
float deceleration = 40.0f;       // How quickly player stops
float airControl = 0.6f;          // Reduced control in air (0.0-1.0)
float maxFallSpeed = -25.0f;      // Terminal velocity

// Improved jump variables
float jumpPower = 12.0f;          // Initial jump velocity
float jumpHoldTime = 0.2f;        // How long space can be held for higher jump
float jumpHoldTimer = 0.0f;
bool isJumping = false;

int maxJumps = 2;  // 1 normal jump + 1 double jump
int jumpsRemaining = maxJumps;

float doubleJumpPower = 7.0f;

float coyoteTime = 0.15f;
float coyoteTimer = 0.0f;

float jumpBufferTime = 0.1f;
float jumpBufferTimer = 0.0f;

bool isGrounded = false;
bool hasDoubleJumped = false;

// Gravity multipliers
float fallMultiplier = 2.0f;      // falling fast
float lowJumpMultiplier = 0.5f;   // early release
float apexHangMultiplier = 0.8f;  // slightly reduce gravity near apex
float apexThreshold = 0.3f;       // velocity threshold for apex

// Variable jump + fall tweaks
float jumpCutMultiplier = 0.5f; // Short hop (reduce upward velocity on release)

// ---------------- High Score System ----------------
int highScore = 0;
const char* HIGH_SCORE_FILE = "highscore.dat";
bool newHighScore = false;
GLuint boxTexture;

// ---------------- Settings ----------------
int WINDOW_WIDTH = 1200;
int WINDOW_HEIGHT = 900;
const float PIXELS_PER_METER = 50.0f;

// ---------------- Platform System ----------------
std::vector<b2BodyId> platforms;
const int NUM_PLATFORMS = 15;
const float PLATFORM_WIDTH = 4.0f;
const float PLATFORM_HEIGHT = 0.5f;
const float MIN_PLATFORM_Y = -2.0f;
const float MAX_PLATFORM_Y = 12.0f;
const float PLATFORM_SPAWN_DISTANCE = 25.0f;
const float PLATFORM_DESPAWN_DISTANCE = -10.0f;
const float MIN_PLATFORM_GAP = 1.0f;
const float MAX_PLATFORM_GAP = 4.0f;
const float MIN_PLATFORM_HEIGHT_CHANGE = -2.0f;
const float MAX_PLATFORM_HEIGHT_CHANGE = 3.0f;

// ---------------- Camera System ----------------
glm::vec2 cameraPosition(0.0f, 0.0f);
float cameraZoom = 1.0f;
const float CAMERA_SMOOTHNESS = 5.0f; // Higher = smoother, lower = more responsive
const float MIN_ZOOM = 0.5f;
const float MAX_ZOOM = 2.0f;

// Add these variables with other camera variables
bool middleMousePressed = false;
double lastMouseX = 0.0, lastMouseY = 0.0;
const float ZOOM_SENSITIVITY = 0.1f;

// Globals
b2WorldId g_world;
GLuint g_vao;
GLuint g_prog;
GLint g_uMVP;
GLint g_uColor;
GLint g_uUseTexture;
GLint g_uTexture;


// Global projection matrix
glm::mat4 proj;

// ---------------- GUI State ----------------
enum GameState { STATE_PLAYING, STATE_PAUSED, STATE_MENU };
GameState currentGameState = STATE_PLAYING;
bool showPauseMenu = false;
GLuint buttonVAO, buttonVBO;
GLuint playButtonTexture, pauseButtonTexture, resumeButtonTexture, quitButtonTexture;

enum EntityType { ENTITY_NONE, ENTITY_PLAYER, ENTITY_BOX, ENTITY_GROUND, ENTITY_BULLET, ENTITY_PLATFORM };

struct UserData {
    EntityType type;
    glm::vec3* color;
    GLuint textureID;
    bool useTexture;

    float animationTime;  // Track animation time for pulsing effect
    bool isAnimating;     // Track if animation is active
    float animationScale;
};

// Colors
glm::vec3 g_playerColor(0.9f, 0.3f, 0.25f);
glm::vec3 g_boxColor(0.2f, 0.5f, 0.8f);
glm::vec3 g_yellowColor(1.0f, 1.0f, 0.0f);
glm::vec3 g_groundColor(0.4f, 0.6f, 0.3f);
glm::vec3 g_bulletColor(1.0f, 0.8f, 0.2f);
glm::vec3 g_platformColor(0.3f, 0.7f, 0.4f);

// ---------------- Audio System ----------------
Mix_Music* backgroundMusic = nullptr;
Mix_Chunk* jumpSound = nullptr;
Mix_Chunk* explosionSound = nullptr;
Mix_Chunk* scoreSound = nullptr;
Mix_Chunk* highScoreSound = nullptr;

bool audioInitialized = false;

bool init_audio() {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cout << "SDL_mixer could not initialize! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return false;
    }

    // Load background music
    backgroundMusic = Mix_LoadMUS("Sunova - Zero.mp3");
    if (!backgroundMusic) {
        std::cout << "Failed to load background music! SDL_mixer Error: " << Mix_GetError() << std::endl;
        // Continue without music, but still try to load sound effects
    }

    // Load sound effects with fallbacks
    jumpSound = Mix_LoadWAV("jump.wav");
    explosionSound = Mix_LoadWAV("bomb.wav");
    scoreSound = Mix_LoadWAV("score.wav");
    highScoreSound = Mix_LoadWAV("highscore.wav");

    // Set volume levels
    if (backgroundMusic) Mix_VolumeMusic(15); // 30% volume for music
    if (jumpSound) Mix_VolumeChunk(jumpSound, 50);
    if (explosionSound) Mix_VolumeChunk(explosionSound, 70);
    if (scoreSound) Mix_VolumeChunk(scoreSound, 80);
    if (highScoreSound) Mix_VolumeChunk(highScoreSound, 100); // Loud for celebration

    audioInitialized = true;
    std::cout << "Audio system initialized successfully!" << std::endl;
    return true;
}

void play_background_music() {
    if (audioInitialized && backgroundMusic) {
        Mix_PlayMusic(backgroundMusic, -1); // -1 for infinite loop
    }
}

void stop_background_music() {
    if (audioInitialized) {
        Mix_HaltMusic();
    }
}

void play_sound(Mix_Chunk* sound, int loops = 0) {
    if (audioInitialized && sound) {
        Mix_PlayChannel(-1, sound, loops); // -1 = use first available channel
    }
}

void pause_background_music() {
    if (audioInitialized && Mix_PlayingMusic()) {
        Mix_PauseMusic();
    }
}

void resume_background_music() {
    if (audioInitialized && Mix_PausedMusic()) {
        Mix_ResumeMusic();
    }
}

// Specific sound functions
void play_jump_sound() { play_sound(jumpSound); }
void play_explosion_sound() { play_sound(explosionSound); }
void play_score_sound() { play_sound(scoreSound); }
void play_high_score_sound() { play_sound(highScoreSound); }

// Health system
int playerHealth = 100;
int maxHealth = 100;
bool isPlayerDead = false;
float respawnTimer = 0.0f;
const float RESPAWN_TIME = 3.0f;

// ---------------- Health Bar System ----------------
GLuint healthBarVAO, healthBarVBO;

void init_health_bar() {
    float vertices[] = {
        // positions
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };

    glGenVertexArrays(1, &healthBarVAO);
    glGenBuffers(1, &healthBarVBO);

    glBindVertexArray(healthBarVAO);
    glBindBuffer(GL_ARRAY_BUFFER, healthBarVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}

void update_camera(b2BodyId player, float deltaTime) {
    if (isPlayerDead) return;

    b2Vec2 playerPos = b2Body_GetPosition(player);
    glm::vec2 targetPosition(playerPos.x * PIXELS_PER_METER, playerPos.y * PIXELS_PER_METER);

    // Smooth camera follow using linear interpolation
    cameraPosition = cameraPosition + (targetPosition - cameraPosition) * (CAMERA_SMOOTHNESS * deltaTime);

    // Keep camera within reasonable vertical bounds
    cameraPosition.y = glm::max(cameraPosition.y, 100.0f); // Don't go too low
}

void render_world_space_health_bar(b2BodyId player, const glm::mat4& viewProj) {
    if (isPlayerDead) return;

    b2Vec2 playerPos = b2Body_GetPosition(player);
    float healthPercent = static_cast<float>(playerHealth) / maxHealth;

    // Use world coordinates (the camera will handle the transformation)
    float worldX = playerPos.x * PIXELS_PER_METER;
    float worldY = playerPos.y * PIXELS_PER_METER + 80.0f; // Above player in world space

    glUseProgram(g_prog);
    glBindVertexArray(healthBarVAO);
    glUniform1i(g_uUseTexture, false);

    // Background (empty part)
    glm::mat4 model(1.0f);
    model = glm::translate(model, { worldX - 40.0f, worldY, 0.0f });
    model = glm::scale(model, { 80.0f, 8.0f, 1.0f });
    glm::mat4 mvp = viewProj * model;
    glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(g_uColor, 0.3f, 0.3f, 0.3f);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Health (filled part)
    model = glm::mat4(1.0f);
    model = glm::translate(model, { worldX - 40.0f, worldY, 0.0f });
    model = glm::scale(model, { 80.0f * healthPercent, 8.0f, 1.0f });
    mvp = viewProj * model;
    glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));

    // Color changes based on health level
    if (healthPercent > 0.6f) {
        glUniform3f(g_uColor, 0.2f, 0.8f, 0.2f); // Green
    }
    else if (healthPercent > 0.3f) {
        glUniform3f(g_uColor, 1.0f, 0.8f, 0.2f); // Yellow
    }
    else {
        glUniform3f(g_uColor, 0.8f, 0.2f, 0.2f); // Red
    }

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Border
    model = glm::mat4(1.0f);
    model = glm::translate(model, { worldX - 40.0f, worldY, 0.0f });
    model = glm::scale(model, { 80.0f, 8.0f, 1.0f });
    mvp = viewProj * model;
    glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(g_uColor, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
}

// ---------------- GUI Button System ----------------
void init_gui_buttons() {
    // Button VAO with texture coordinates
    float vertices[] = {
        // positions   // texture coords
        0.0f, 0.0f,    0.0f, 0.0f,
        1.0f, 0.0f,    1.0f, 0.0f,
        1.0f, 1.0f,    1.0f, 1.0f,
        0.0f, 1.0f,    0.0f, 1.0f
    };

    glGenVertexArrays(1, &buttonVAO);
    glGenBuffers(1, &buttonVBO);

    glBindVertexArray(buttonVAO);
    glBindBuffer(GL_ARRAY_BUFFER, buttonVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // Texture coordinate attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

GLuint load_texture_alpha(const char* path, bool flip_vertical = true) {
    stbi_set_flip_vertically_on_load(flip_vertical);

    GLuint textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
        std::cout << "SUCCESS: Loaded texture: " << path << " (" << width << "x" << height << ", " << nrComponents << " components)" << std::endl;
        return textureID;
    }
    else {
        std::cout << "ERROR: Texture failed to load at path: " << path << std::endl;
        std::cout << "STBI Error: " << stbi_failure_reason() << std::endl;
        stbi_image_free(data);
        return 0;
    }
}

// Font rendering
struct Character {
    GLuint textureID;
    glm::ivec2 size;
    glm::ivec2 bearing;
    unsigned int advance;
};

std::map<char, Character> characters;
GLuint fontVAO, fontVBO;
GLuint fontProgram;
GLint font_uMVP, font_uTextColor, font_uTexture;

void init_font_rendering();
void render_text(const std::string& text, float x, float y, float scale,
    const glm::vec3& color, const glm::vec3& shadowColor,
    const glm::vec2& shadowOffset);
void spawn_score_popup(int points, const glm::vec2& position);
void spawn_damage_effect(int damage, const glm::vec2& position);
void spawn_heal_effect(int amount, const glm::vec2& position);
void update_score_popups(float deltaTime);
void render_score_popups(const glm::mat4& proj);

void render_button(float x, float y, float width, float height, GLuint texture, const std::string& text = "", const glm::vec3& color = glm::vec3(1.0f)) {
    glUseProgram(g_prog);
    glBindVertexArray(buttonVAO);

    // IMPORTANT: Set texture usage and bind texture BEFORE setting uniforms
    glUniform1i(g_uUseTexture, texture != 0);

    if (texture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(g_uTexture, 0); // This tells the shader to use texture unit 0
    }

    glm::mat4 model(1.0f);
    model = glm::translate(model, { x + width / 2.0f, y + height / 2.0f, 0.0f });
    model = glm::scale(model, { width, height, 1.0f });

    // Use the original projection (not viewProj) for UI elements
    glm::mat4 mvp = proj * model;
    glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));

    // Use white color when using textures to preserve original texture colors
    if (texture != 0) {
        glUniform3f(g_uColor, 1.0f, 1.0f, 1.0f); // White to preserve texture colors
    }
    else {
        glUniform3f(g_uColor, color.r, color.g, color.b); // Use provided color for non-textured buttons
    }


    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Render button text if provided
    if (!text.empty()) {
        render_text(text, x + width / 2.0f - text.length() * 5.0f, y + height / 2.0f - 8.0f, 0.4f,
            glm::vec3(1, 1, 1), glm::vec3(0, 0, 0), glm::vec2(1, -1));
    }

    // Unbind texture to avoid affecting other rendering
    if (texture != 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool is_point_in_rect(float px, float py, float x, float y, float width, float height) {
    return px >= x && px <= x + width && py >= y && py <= y + height;
}

void render_text_button(float x, float y, float width, float height, const std::string& text,
    float scale = 0.5f, bool isHovered = false) {
    // No background rendering - just text

    // Calculate text position (centered)
    float textWidth = text.length() * 10.0f * scale; // Approximate text width
    float textX = x + (width - textWidth) / 2.0f;
    float textY = y + (height - 20.0f * scale) / 2.0f;

    // Choose color based on hover state
    glm::vec3 textColor, shadowColor;
    if (isHovered) {
        textColor = glm::vec3(1.0f, 1.0f, 0.0f); // Yellow when hovered
        shadowColor = glm::vec3(0.5f, 0.5f, 0.0f);
    }
    else {
        textColor = glm::vec3(1.0f, 1.0f, 1.0f); // White normally
        shadowColor = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // Render the text
    render_text(text, textX, textY, scale, textColor, shadowColor, glm::vec2(1, -1));
}

bool check_button_hover(float mouseX, float mouseY, float x, float y, float width, float height) {
    return is_point_in_rect(mouseX, mouseY, x, y, width, height);
}


// ---------------- Particle System ----------------
struct Particle {
    glm::vec2 position;
    glm::vec2 velocity;
    float life;
    float size;
    float rotation;
    float rotationSpeed;
};

const int MAX_PARTICLES = 100;
std::vector<Particle> particles;
GLuint g_particleTexture;
float g_particleSize = 0.2f; // Size in meters

// Function declarations
void init_particle_system();
void spawn_explosion(const glm::vec2& position);
void update_particles(float deltaTime);
void render_particles(const glm::mat4& viewProj);

// ---------------- Score System with Pixel Font ----------------
struct FloatingText {
    std::string text;
    glm::vec2 position; // in pixels
    float life;
    float duration;
    float scale;
    glm::vec3 color;
    glm::vec3 shadowColor;
    glm::vec2 shadowOffset;
};

std::vector<FloatingText> floatingTexts;
int currentScore = 0;
bool wasPlayerNear = false;

// ---------------- Platform System Functions ----------------
void generate_initial_platforms() {
    platforms.clear();

    float startX = -15.0f;
    float currentY = 0.0f;

    // Create initial platforms leading up to the player start position
    for (int i = 0; i < NUM_PLATFORMS; i++) {
        b2BodyDef platformDef = b2DefaultBodyDef();
        platformDef.type = b2_staticBody;
        platformDef.position = { startX + i * (PLATFORM_WIDTH + 2.0f), currentY };
        b2BodyId platform = b2CreateBody(g_world, &platformDef);

        b2Polygon platformShape = b2MakeBox(PLATFORM_WIDTH / 2, PLATFORM_HEIGHT / 2);
        b2ShapeDef platformSD = b2DefaultShapeDef();
        b2CreatePolygonShape(platform, &platformSD, &platformShape);

        UserData* platformUD = new UserData{ ENTITY_PLATFORM, &g_platformColor, 0, false, 0.0f, false, 1.0f };
        b2Body_SetUserData(platform, platformUD);

        platforms.push_back(platform);

        // Add some vertical variation after the first few platforms
        if (i > 3) {
            currentY += (rand() % 3) - 1; // -1, 0, or 1
            currentY = glm::clamp(currentY, MIN_PLATFORM_Y, MAX_PLATFORM_Y);
        }
    }
}

void ensure_safe_platform_gaps() {
    if (platforms.size() < 2) return;

    // Check distances between recent platforms
    for (int i = platforms.size() - 1; i > glm::max(0, (int)platforms.size() - 5); i--) {
        if (i == 0) break;

        b2Vec2 currentPos = b2Body_GetPosition(platforms[i]);
        b2Vec2 prevPos = b2Body_GetPosition(platforms[i - 1]);

        float horizontalGap = currentPos.x - prevPos.x;
        float verticalGap = fabs(currentPos.y - prevPos.y);

        // If platforms are too close horizontally and vertically, adjust the current one
        if (horizontalGap < 2.0f && verticalGap < 1.5f) {
            b2Vec2 newPos = currentPos;
            newPos.y += 1.0f; // Move current platform up
            b2Body_SetTransform(platforms[i], newPos, b2MakeRot(0.0f));
        }
    }
}

void update_platforms(b2BodyId player) {
    if (isPlayerDead) return;

    b2Vec2 playerPos = b2Body_GetPosition(player);

    // Remove platforms that are too far behind the player
    auto it = platforms.begin();
    while (it != platforms.end()) {
        b2Vec2 platformPos = b2Body_GetPosition(*it);
        if (platformPos.x < playerPos.x + PLATFORM_DESPAWN_DISTANCE) {
            b2DestroyBody(*it);
            it = platforms.erase(it);
        }
        else {
            ++it;
        }
    }

    // Add new platforms if needed
    if (!platforms.empty()) {
        b2Vec2 lastPlatformPos = b2Body_GetPosition(platforms.back());

        if (lastPlatformPos.x < playerPos.x + PLATFORM_SPAWN_DISTANCE) {
            float newX = lastPlatformPos.x + PLATFORM_WIDTH +
                MIN_PLATFORM_GAP +
                (rand() % static_cast<int>((MAX_PLATFORM_GAP - MIN_PLATFORM_GAP) * 10)) / 10.0f;

            float newY = lastPlatformPos.y +
                MIN_PLATFORM_HEIGHT_CHANGE +
                (rand() % static_cast<int>((MAX_PLATFORM_HEIGHT_CHANGE - MIN_PLATFORM_HEIGHT_CHANGE) * 10)) / 10.0f;

            newY = glm::clamp(newY, MIN_PLATFORM_Y, MAX_PLATFORM_Y);

            // Ensure platforms don't get too high or too low relative to player
            if (fabs(newY - playerPos.y) > 8.0f) {
                newY = playerPos.y + (rand() % 3) - 1; // Keep near player height
            }

            b2BodyDef platformDef = b2DefaultBodyDef();
            platformDef.type = b2_staticBody;
            platformDef.position = { newX, newY };
            b2BodyId newPlatform = b2CreateBody(g_world, &platformDef);

            b2Polygon platformShape = b2MakeBox(PLATFORM_WIDTH / 2, PLATFORM_HEIGHT / 2);
            b2ShapeDef platformSD = b2DefaultShapeDef();
            b2CreatePolygonShape(newPlatform, &platformSD, &platformShape);

            UserData* platformUD = new UserData{ ENTITY_PLATFORM, &g_platformColor, 0, false, 0.0f, false, 1.0f };
            b2Body_SetUserData(newPlatform, platformUD);

            platforms.push_back(newPlatform);

            // Ensure safe gaps between platforms
            ensure_safe_platform_gaps();
        }
    }
}

// ---------------- Health Management ----------------
void player_died(b2BodyId player) {
    stop_background_music();

    isPlayerDead = true;
    respawnTimer = RESPAWN_TIME;
    deathCount++;

    std::cout << "Player died! Deaths: " << deathCount << "/" << MAX_DEATHS << " | Final Score: " << currentScore << std::endl;

    // Death effect
    b2Vec2 playerPos = b2Body_GetPosition(player);
    spawn_explosion(glm::vec2(playerPos.x, playerPos.y));

    // Make player fall through platforms
    b2Body_SetGravityScale(player, 2.0f);

    // Check for game over
    if (deathCount >= MAX_DEATHS) {
        gameOver = true;
        gameOverTimer = GAME_OVER_DISPLAY_TIME;
        std::cout << "GAME OVER! Final Score: " << currentScore << std::endl;
    }
}

void take_damage(int damage, b2BodyId player) {
    if (isPlayerDead) return;

    playerHealth -= damage;
    if (playerHealth < 0) playerHealth = 0;

    // Visual feedback
    b2Vec2 playerPos = b2Body_GetPosition(player);
    spawn_damage_effect(damage, glm::vec2(playerPos.x, playerPos.y));

    if (playerHealth <= 0) {
        player_died(player);
    }
}

void heal(int amount, b2BodyId player) {
    playerHealth += amount;
    if (playerHealth > maxHealth) playerHealth = maxHealth;

    // Visual feedback for healing
    b2Vec2 playerPos = b2Body_GetPosition(player);
    spawn_heal_effect(amount, glm::vec2(playerPos.x, playerPos.y));
}

void respawn_player(b2BodyId player) {
    if (gameOver) {
        // Don't respawn if game is over
        return;
    }

    play_background_music();

    isPlayerDead = false;
    playerHealth = maxHealth;
    b2Body_SetGravityScale(player, 1.0f);

    // Find a safe spawn position - FIXED: Check if platforms exist
    float spawnX = -10.0f;
    float spawnY = 5.0f; // Default safe height

    if (!platforms.empty()) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
        spawnX = platformPos.x;
        spawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
    }

    b2Body_SetTransform(player, { spawnX, spawnY }, b2MakeRot(0.0f));
    b2Body_SetLinearVelocity(player, { 0.0f, 0.0f });

    // Reset camera to player position
    cameraPosition = glm::vec2(spawnX * PIXELS_PER_METER, spawnY * PIXELS_PER_METER);

    // Reset jump variables
    jumpsRemaining = maxJumps;
    hasDoubleJumped = false;
    isJumping = false;
    coyoteTimer = 0.0f;
    jumpBufferTimer = 0.0f;

    // Just stop the respawn timer
    respawnTimer = 0.0f;
    std::cout << "Player respawned" << std::endl;
}

void reset_game(b2BodyId player) {
    // Reset box system
    if (b2Body_IsValid(currentBox)) {
        UserData* ud = (UserData*)b2Body_GetUserData(currentBox);
        if (ud) {
            delete ud->color;
            delete ud;
        }
        b2DestroyBody(currentBox);
    }
    currentBox = b2_nullBodyId;
    boxSpawned = false;
    boxRespawnTimer = 0.0f;

    // Reset all game state variables
    deathCount = 0;
    gameOver = false;
    gameOverTimer = 0.0f;
    currentScore = 0;
    playerHealth = maxHealth;
    isPlayerDead = false;
    newHighScore = false;
    lastPlatformIndex = 0;

    // Reset player position and state
    b2Body_SetGravityScale(player, 1.0f);

    // Find a safe spawn position - FIXED: Check if platforms vector is empty
    float spawnX = -10.0f;
    float spawnY = 5.0f; // Default safe height

    // Only use platform position if platforms exist
    if (!platforms.empty()) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
        spawnX = platformPos.x;
        spawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
    }

    b2Body_SetTransform(player, { spawnX, spawnY }, b2MakeRot(0.0f));
    b2Body_SetLinearVelocity(player, { 0.0f, 0.0f });

    // Reset camera
    cameraPosition = glm::vec2(spawnX * PIXELS_PER_METER, spawnY * PIXELS_PER_METER);

    // Clear existing platforms and generate new ones
    for (auto& platform : platforms) {
        UserData* ud = (UserData*)b2Body_GetUserData(platform);
        if (ud) delete ud;
        b2DestroyBody(platform);
    }
    platforms.clear();

    // Generate new platforms before setting player position
    generate_initial_platforms();

    // Now that platforms are generated, we can reposition the player safely
    if (!platforms.empty()) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
        spawnX = platformPos.x;
        spawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
        b2Body_SetTransform(player, { spawnX, spawnY }, b2MakeRot(0.0f));
        cameraPosition = glm::vec2(spawnX * PIXELS_PER_METER, spawnY * PIXELS_PER_METER);
    }

    // Clear particles and floating text
    particles.clear();
    floatingTexts.clear();

    // Reset jump variables
    jumpsRemaining = maxJumps;
    hasDoubleJumped = false;
    isJumping = false;
    coyoteTimer = 0.0f;
    jumpBufferTimer = 0.0f;

    // Restart music
    play_background_music();

    std::cout << "Game reset! Starting new game..." << std::endl;
}

// ---------------- Fullscreen Toggle ----------------
void toggle_fullscreen(GLFWwindow* window) {
    if (isFullscreen) {
        // Switch to windowed mode
        glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, GLFW_DONT_CARE);
        isFullscreen = false;
        std::cout << "Switched to windowed mode: " << windowedWidth << "x" << windowedHeight << std::endl;
    }
    else {
        // Store current window position and size
        glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

        // Get monitor info for fullscreen
        const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
        glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        isFullscreen = true;
        std::cout << "Switched to fullscreen: " << mode->width << "x" << mode->height << std::endl;
    }

    // Update viewport
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
}

// ---------------- Shaders ----------------
const char* vertex_shader_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uMVP;
out vec2 TexCoord;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* fragment_shader_src = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
uniform sampler2D uTexture;
uniform bool uUseTexture;
in vec2 TexCoord;
void main() {
    vec4 finalColor;
    if (uUseTexture) {
        // When using texture, ignore uColor and use texture color directly
        finalColor = texture(uTexture, TexCoord);
    } else {
        // When not using texture, use the uniform color
        finalColor = vec4(uColor, 1.0);
    }
    FragColor = finalColor;
}
)";

// Font shaders
const char* font_vertex_shader_src = R"(
#version 330 core
layout(location = 0) in vec4 vertex; // xy = pos, zw = tex
out vec2 TexCoords;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

const char* font_fragment_shader_src = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D text;
uniform vec3 textColor;
void main() {
    float alpha = texture(text, TexCoords).r;
    FragColor = vec4(textColor, alpha);
}
)";

// ---------------- Helpers ----------------
GLuint compile_shader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::cerr << "Shader compile error: " << log << "\n"; exit(1); }
    return s;
}

GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(p, 512, nullptr, log); std::cerr << "Program link error: " << log << "\n"; exit(1); }
    return p;
}

// ---------------- Texture Loading ----------------
GLuint load_texture(const char* path, bool flip_vertical = true) {
    stbi_set_flip_vertically_on_load(flip_vertical);

    GLuint textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
        return 0;
    }

    return textureID;
}

// Create a procedural texture for testing if no image files are available
GLuint create_procedural_texture(int width, int height, const glm::vec3& color1, const glm::vec3& color2) {
    std::vector<unsigned char> data(width * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            bool pattern = (x / 16 + y / 16) % 2 == 0;

            if (pattern) {
                data[idx] = static_cast<unsigned char>(color1.r * 255);
                data[idx + 1] = static_cast<unsigned char>(color1.g * 255);
                data[idx + 2] = static_cast<unsigned char>(color1.b * 255);
            }
            else {
                data[idx] = static_cast<unsigned char>(color2.r * 255);
                data[idx + 1] = static_cast<unsigned char>(color2.g * 255);
                data[idx + 2] = static_cast<unsigned char>(color2.b * 255);
            }
        }
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return textureID;
}

// ---------------- VBO + EBO Setup ----------------
GLuint create_square_vao_ebo() {
    float vertices[] = {
        // positions     // texture coords
        -0.5f, -0.5f,    0.0f, 0.0f, // 0
         0.5f, -0.5f,    1.0f, 0.0f, // 1
         0.5f,  0.5f,    1.0f, 1.0f, // 2
        -0.5f,  0.5f,    0.0f, 1.0f  // 3
    };
    unsigned int indices[] = { 0,1,2, 0,2,3 };

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // Texture coordinate attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    return vao;
}

// ---------------- AABB ----------------
struct AABB { float minX, minY, maxX, maxY; };

AABB getAABBWithProximity(b2BodyId body, float halfW, float halfH, float proximity) {
    // Safety check for invalid body
    if (!b2Body_IsValid(body)) {
        return { 0.0f, 0.0f, 0.0f, 0.0f }; // Return empty AABB
    }

    b2Vec2 pos = b2Body_GetPosition(body);
    return {
        pos.x - halfW - proximity,
        pos.y - halfH - proximity,
        pos.x + halfW + proximity,
        pos.y + halfH + proximity
    };
}

bool aabbOverlap(const AABB& a, const AABB& b) {
    return !(a.maxX<b.minX || a.minX>b.maxX || a.maxY<b.minY || a.minY>b.maxY);
}

void spawn_double_jump_effect(b2BodyId player) {
    b2Vec2 pos = b2Body_GetPosition(player);

    // Create a circular burst effect for double jump
    for (int i = 0; i < 12; i++) {
        Particle p;
        p.position = glm::vec2(pos.x, pos.y - 0.5f); // Start at feet

        float angle = (i / 12.0f) * 2.0f * 3.14159f;
        float speed = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
        p.velocity = glm::vec2(cos(angle) * speed, fabs(sin(angle)) * 2.0f); // Upward bias

        p.life = 0.6f;
        p.size = 0.1f;
        p.rotation = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        p.rotationSpeed = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 3.0f;

        particles.push_back(p);
    }
}

// ---------------- Input ----------------
void handle_jump_input(b2BodyId player, GLFWwindow* win, float deltaTime) {
    b2Vec2 velocity = b2Body_GetLinearVelocity(player);

    // Jump buffer system
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
        jumpBufferTimer = jumpBufferTime;
    }

    // Variable jump height (hold space for higher jump)
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS && isJumping) {
        if (jumpHoldTimer < jumpHoldTime && velocity.y > 0) {
            // Apply reduced gravity while holding jump
            velocity.y -= 5.0f * deltaTime; // Reduced gravity effect
            b2Body_SetLinearVelocity(player, velocity);
            jumpHoldTimer += deltaTime;
        }
    }
    else {
        isJumping = false;
        jumpHoldTimer = 0.0f;
    }

    // Try to jump if buffer is active
    if (jumpBufferTimer > 0) {
        bool canGroundJump = (isGrounded || coyoteTimer > 0) && jumpsRemaining > 0;
        bool canDoubleJump = !isGrounded && jumpsRemaining > 0 && !hasDoubleJumped;

        if (canGroundJump) {
            // Ground jump
            velocity.y = jumpPower;
            b2Body_SetLinearVelocity(player, velocity);
            jumpsRemaining--;
            jumpBufferTimer = 0;
            isJumping = true;
            play_jump_sound();
        }
        else if (canDoubleJump) {
            // Double jump
            velocity.y = jumpPower * 0.9f; // Slightly weaker double jump
            b2Body_SetLinearVelocity(player, velocity);
            jumpsRemaining--;
            hasDoubleJumped = true;
            jumpBufferTimer = 0;
            isJumping = true;

            // Double jump effect
            spawn_double_jump_effect(player);
            play_jump_sound();
        }
    }

    // Apply fall gravity multiplier for more responsive falling
    if (velocity.y < 0) {
        // Falling - apply extra gravity
        velocity.y -= 15.0f * deltaTime * fallMultiplier;
    }
    else if (velocity.y > 0 && !glfwGetKey(win, GLFW_KEY_SPACE)) {
        // Rising but not holding jump - low jump (variable height)
        velocity.y -= 15.0f * deltaTime * lowJumpMultiplier;
    }

    // Clamp fall speed
    velocity.y = glm::max(velocity.y, maxFallSpeed);

    b2Body_SetLinearVelocity(player, velocity);
}

void process_input(GLFWwindow* win, b2BodyId player, float deltaTime) {
    // ESC key to toggle pause
    static bool escKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        if (!escKeyPressed) {
            if (currentGameState == STATE_PLAYING) {
                currentGameState = STATE_PAUSED;
                showPauseMenu = true;
            }
            else if (currentGameState == STATE_PAUSED) {
                currentGameState = STATE_PLAYING;
                showPauseMenu = false;
            }
            escKeyPressed = true;
        }
    }
    else {
        escKeyPressed = false;
    }

    // Fullscreen toggle with F11 or Alt+Enter
    static bool f11Pressed = false;
    if (glfwGetKey(win, GLFW_KEY_F11) == GLFW_PRESS) {
        if (!f11Pressed) {
            toggle_fullscreen(win);
            f11Pressed = true;
        }
    }
    else {
        f11Pressed = false;
    }

    // Alt+Enter for fullscreen (common alternative)
    static bool altEnterPressed = false;
    if ((glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) &&
        glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS) {
        if (!altEnterPressed) {
            toggle_fullscreen(win);
            altEnterPressed = true;
        }
    }
    else {
        altEnterPressed = false;
    }

    // Camera zoom controls
    if (glfwGetKey(win, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_KP_ADD) == GLFW_PRESS) {
        cameraZoom = glm::min(cameraZoom + deltaTime, MAX_ZOOM);
    }
    if (glfwGetKey(win, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) {
        cameraZoom = glm::max(cameraZoom - deltaTime, MIN_ZOOM);
    }

    // Only process game input when playing
    if (currentGameState != STATE_PLAYING) return;
    if (isPlayerDead) return;

    // Get current velocity
    b2Vec2 velocity = b2Body_GetLinearVelocity(player);

    // Horizontal movement with proper acceleration/deceleration
    float targetVelocityX = 0.0f;

    if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) {
        targetVelocityX = -moveSpeed;
    }
    if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        targetVelocityX = moveSpeed;
    }

    // Apply acceleration/deceleration
    float currentControl = isGrounded ? 1.0f : airControl;

    if (targetVelocityX != 0.0f) {
        // Accelerate toward target velocity
        velocity.x = glm::mix(velocity.x, targetVelocityX, acceleration * currentControl * deltaTime);
    }
    else {
        // Decelerate to zero
        velocity.x = glm::mix(velocity.x, 0.0f, deceleration * currentControl * deltaTime);
    }

    // Clamp horizontal speed
    velocity.x = glm::clamp(velocity.x, -moveSpeed, moveSpeed);

    // Apply velocity
    b2Body_SetLinearVelocity(player, velocity);

    // Jump handling
    handle_jump_input(player, win, deltaTime);

    // Reset player position (for testing)
    if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) {
        float spawnX = -10.0f;
        float spawnY = 5.0f;
        if (!platforms.empty()) {
            b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
            spawnX = platformPos.x;
            spawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
        }
        b2Body_SetTransform(player, { spawnX, spawnY }, b2MakeRot(0.0f));
        b2Body_SetLinearVelocity(player, { 0.0f, 0.0f });
    }

    // Particle explosion on X key
    static bool xKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_X) == GLFW_PRESS) {
        if (!xKeyPressed) {
            b2Vec2 pos = b2Body_GetPosition(player);
            spawn_explosion(glm::vec2(pos.x, pos.y));
            xKeyPressed = true;
        }
    }
    else {
        xKeyPressed = false;
    }

    // Damage test on H key
    static bool hKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_H) == GLFW_PRESS) {
        if (!hKeyPressed) {
            take_damage(10, player);
            hKeyPressed = true;
        }
    }
    else {
        hKeyPressed = false;
    }

    // Heal test on J key
    static bool jKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_J) == GLFW_PRESS) {
        if (!jKeyPressed) {
            heal(15, player);
            jKeyPressed = true;
        }
    }
    else {
        jKeyPressed = false;
    }

    // Audio controls
    static bool mKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_M) == GLFW_PRESS) {
        if (!mKeyPressed) {
            if (Mix_PlayingMusic()) {
                Mix_PauseMusic();
            }
            else {
                Mix_ResumeMusic();
            }
            mKeyPressed = true;
        }
    }
    else {
        mKeyPressed = false;
    }

    static bool nKeyPressed = false;
    if (glfwGetKey(win, GLFW_KEY_N) == GLFW_PRESS) {
        if (!nKeyPressed) {
            static bool soundsMuted = false;
            soundsMuted = !soundsMuted;
            Mix_Volume(-1, soundsMuted ? 0 : 128);
            nKeyPressed = true;
        }
    }
    else {
        nKeyPressed = false;
    }
}

// ---------------- Mouse Input ----------------
void process_mouse_input(GLFWwindow* window, double xpos, double ypos, int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Convert to screen coordinates (flip Y)
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float mouseX = xpos;
        float mouseY = height - ypos;

        // Pause menu buttons
        if (showPauseMenu) {
            float centerX = width / 2.0f;
            float centerY = height / 2.0f;
            float buttonWidth = 200.0f;
            float buttonHeight = 50.0f;
            float buttonSpacing = 60.0f;

            // Resume button
            if (is_point_in_rect(mouseX, mouseY, centerX - buttonWidth / 2, centerY, buttonWidth, buttonHeight)) {
                currentGameState = STATE_PLAYING;
                showPauseMenu = false;
            }
            // Quit button
            else if (is_point_in_rect(mouseX, mouseY, centerX - buttonWidth / 2, centerY - 50, buttonWidth, buttonHeight)) {
                glfwSetWindowShouldClose(window, true);
            }

        }

        // Play/Pause button in HUD (top-right corner)
        if (is_point_in_rect(mouseX, mouseY, width - 60, height - 60, 50, 50)) {
            if (currentGameState == STATE_PLAYING) {
                currentGameState = STATE_PAUSED;
                showPauseMenu = true;
            }
            else {
                currentGameState = STATE_PLAYING;
                showPauseMenu = false;
            }
        }
    }

    // Middle mouse button handling
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            middleMousePressed = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        else if (action == GLFW_RELEASE) {
            middleMousePressed = false;
        }
    }
}

// ---------------- Animation Functions ----------------
void update_box_animation(UserData* boxUD, float deltaTime, bool isPlayerNear) {
    if (isPlayerNear) {
        // Start or continue animation
        boxUD->isAnimating = true;
        boxUD->animationTime += deltaTime;

        // Pulse effect: scale between 0.9 and 1.1 of original size
        float pulse = 0.1f * sin(boxUD->animationTime * 5.0f); // 5 Hz pulse
        boxUD->animationScale = 1.0f + pulse;
    }
    else {
        // Reset animation when player moves away
        boxUD->isAnimating = false;
        boxUD->animationTime = 0.0f;
        boxUD->animationScale = 1.0f;
    }
}

// ---------------- Particle System Functions ----------------
void init_particle_system() {
    particles.reserve(MAX_PARTICLES);

    // Load the particle texture
    g_particleTexture = load_texture("explosion.png");

    // If loading fails, create a simple fallback texture
    if (g_particleTexture == 0) {
        std::cout << "Failed to load explosion.png, creating fallback texture" << std::endl;

        const int TEX_SIZE = 64;
        std::vector<unsigned char> textureData(TEX_SIZE * TEX_SIZE * 4);

        float center = TEX_SIZE / 2.0f;
        float radius = TEX_SIZE / 2.0f;

        for (int y = 0; y < TEX_SIZE; ++y) {
            for (int x = 0; x < TEX_SIZE; ++x) {
                int idx = (y * TEX_SIZE + x) * 4;
                float dist = sqrt((x - center) * (x - center) + (y - center) * (y - center));

                if (dist < radius) {
                    // Create a circular gradient with transparency
                    float alpha = 1.0f - (dist / radius);
                    textureData[idx] = 255;     // R
                    textureData[idx + 1] = 200; // G
                    textureData[idx + 2] = 100; // B
                    textureData[idx + 3] = static_cast<unsigned char>(alpha * 255); // A
                }
                else {
                    // Transparent outside the circle
                    textureData[idx] = 0;
                    textureData[idx + 1] = 0;
                    textureData[idx + 2] = 0;
                    textureData[idx + 3] = 0;
                }
            }
        }

        glGenTextures(1, &g_particleTexture);
        glBindTexture(GL_TEXTURE_2D, g_particleTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureData.data());
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

void spawn_explosion(const glm::vec2& position) {
    play_explosion_sound();

    // Create 10-15 particles for the explosion
    int numParticles = 10 + rand() % 6;

    for (int i = 0; i < numParticles && particles.size() < MAX_PARTICLES; ++i) {
        Particle p;
        p.position = position;

        // Random direction and speed
        float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        float speed = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
        p.velocity = glm::vec2(cos(angle) * speed, sin(angle) * speed);

        p.life = 0.5f + static_cast<float>(rand()) / RAND_MAX * 0.5f; // 0.5-1.0 seconds
        p.size = g_particleSize * (0.7f + static_cast<float>(rand()) / RAND_MAX * 0.6f); // Vary size
        p.rotation = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        p.rotationSpeed = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f;

        particles.push_back(p);
    }
}

void update_particles(float deltaTime) {
    for (auto it = particles.begin(); it != particles.end(); ) {
        it->life -= deltaTime;

        if (it->life <= 0.0f) {
            // Remove dead particles
            it = particles.erase(it);
        }
        else {
            // Update position and rotation
            it->position += it->velocity * deltaTime;
            it->rotation += it->rotationSpeed * deltaTime;

            // Apply gravity
            it->velocity.y -= 10.0f * deltaTime;

            // Scale down as particle dies
            it->size = g_particleSize * (it->life / 0.5f) * (0.7f + 0.3f * (it->life / 0.5f));

            ++it;
        }
    }
}

void render_particles(const glm::mat4& viewProj) {
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glUniform1i(g_uTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_particleTexture);

    for (const auto& p : particles) {
        float worldX = p.position.x * PIXELS_PER_METER;
        float worldY = p.position.y * PIXELS_PER_METER;

        glm::mat4 model(1.0f);
        model = glm::translate(model, { worldX, worldY, 0.0f });
        model = glm::rotate(model, p.rotation, { 0, 0, 1 });
        model = glm::scale(model, { p.size * PIXELS_PER_METER * 2.0f,
                                  p.size * PIXELS_PER_METER * 2.0f, 1.0f });

        glm::mat4 mvp = viewProj * model;
        glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        // Fade out as particle dies
        float alpha = p.life / 0.5f;
        glm::vec3 color(1.0f, 0.8f, 0.4f * alpha);
        glUniform3f(g_uColor, color.r, color.g, color.b);
        glUniform1i(g_uUseTexture, true);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }
}

// ---------------- Font Rendering Functions ----------------
void init_font_rendering() {
    // Initialize FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return;
    }

    // Load font (try a few common font paths)
    const char* fontPaths[] = {
        "PressStart2P.ttf",  // Pixel font
        "arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };

    FT_Face face = 0;
    bool fontLoaded = false;

    for (const char* fontPath : fontPaths) {
        if (FT_New_Face(ft, fontPath, 0, &face) == 0) {
            fontLoaded = true;
            std::cout << "Loaded font: " << fontPath << std::endl;
            break;
        }
    }

    if (!fontLoaded) {
        std::cout << "ERROR::FREETYPE: Failed to load any font" << std::endl;
        FT_Done_FreeType(ft);
        return;
    }

    // Set size to load glyphs as
    FT_Set_Pixel_Sizes(face, 0, 30); // Pixel font size

    // Disable byte-alignment restriction
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Load first 128 characters of ASCII set
    for (unsigned char c = 0; c < 128; c++) {
        // Load character glyph
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cout << "ERROR::FREETYPE: Failed to load Glyph: " << c << std::endl;
            continue;
        }

        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        // Set texture options - nearest-neighbor to keep pixel look
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // Now store character for later use
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)
        };
        characters.insert(std::pair<char, Character>(c, character));
    }

    // Destroy FreeType once we're finished
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Configure font VAO/VBO for texture quads
    glGenVertexArrays(1, &fontVAO);
    glGenBuffers(1, &fontVBO);
    glBindVertexArray(fontVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fontVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Compile font shaders
    GLuint fontVS = compile_shader(font_vertex_shader_src, GL_VERTEX_SHADER);
    GLuint fontFS = compile_shader(font_fragment_shader_src, GL_FRAGMENT_SHADER);
    fontProgram = link_program(fontVS, fontFS);
    glDeleteShader(fontVS);
    glDeleteShader(fontFS);

    // Get uniform locations
    font_uMVP = glGetUniformLocation(fontProgram, "uMVP");
    font_uTextColor = glGetUniformLocation(fontProgram, "textColor");
    font_uTexture = glGetUniformLocation(fontProgram, "text");
}

void render_text(const std::string& text, float x, float y, float scale,
    const glm::vec3& color, const glm::vec3& shadowColor,
    const glm::vec2& shadowOffset) {

    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);

    glUseProgram(fontProgram);
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width),
        0.0f, static_cast<float>(height));
    glUniformMatrix4fv(font_uMVP, 1, GL_FALSE, glm::value_ptr(projection));

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(fontVAO);

    auto draw = [&](glm::vec3 c, float dx, float dy) {
        glUniform3f(font_uTextColor, c.r, c.g, c.b);

        float xpos = x + dx;
        float ypos = y + dy;

        for (auto ch : text) {
            Character chdata = characters[ch];

            float xposc = xpos + chdata.bearing.x * scale;
            float yposc = ypos - (chdata.size.y - chdata.bearing.y) * scale;
            float w = chdata.size.x * scale;
            float h = chdata.size.y * scale;

            float vertices[6][4] = {
                { xposc, yposc + h, 0.0f, 0.0f },
                { xposc, yposc,     0.0f, 1.0f },
                { xposc + w, yposc, 1.0f, 1.0f },

                { xposc, yposc + h, 0.0f, 0.0f },
                { xposc + w, yposc, 1.0f, 1.0f },
                { xposc + w, yposc + h, 1.0f, 0.0f }
            };

            glBindTexture(GL_TEXTURE_2D, chdata.textureID);
            glBindBuffer(GL_ARRAY_BUFFER, fontVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            xpos += (chdata.advance >> 6) * scale;
        }
        };

    // Draw shadow first
    draw(shadowColor, shadowOffset.x, shadowOffset.y);
    // Draw main text on top
    draw(color, 0, 0);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void spawn_score_popup(int points, const glm::vec2& position) {
    FloatingText ft;
    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);

    ft.text = "+" + std::to_string(points);
    ft.position = glm::vec2(position.x * PIXELS_PER_METER + width / 2.0f,
        position.y * PIXELS_PER_METER + height / 2.0f);
    ft.life = 1.5f;
    ft.duration = 1.5f;
    ft.scale = 0.5f;
    ft.color = glm::vec3(1.0f, 1.0f, 1.0f);
    ft.shadowColor = glm::vec3(0.2f, 0.6f, 1.0f);
    ft.shadowOffset = glm::vec2(2, -2);

    floatingTexts.push_back(ft);
}

void spawn_damage_effect(int damage, const glm::vec2& position) {
    FloatingText ft;
    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);

    ft.text = "-" + std::to_string(damage);
    ft.position = glm::vec2(position.x * PIXELS_PER_METER + width / 2.0f,
        position.y * PIXELS_PER_METER + height / 2.0f + 50.0f);
    ft.life = 1.0f;
    ft.duration = 1.0f;
    ft.scale = 0.7f;
    ft.color = glm::vec3(1.0f, 0.3f, 0.3f);
    ft.shadowColor = glm::vec3(0.5f, 0.1f, 0.1f);
    ft.shadowOffset = glm::vec2(1, -1);

    floatingTexts.push_back(ft);
}

void spawn_heal_effect(int amount, const glm::vec2& position) {
    FloatingText ft;
    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);

    ft.text = "+" + std::to_string(amount) + " HP";
    ft.position = glm::vec2(position.x * PIXELS_PER_METER + width / 2.0f,
        position.y * PIXELS_PER_METER + height / 2.0f + 50.0f);
    ft.life = 1.5f;
    ft.duration = 1.5f;
    ft.scale = 0.6f;
    ft.color = glm::vec3(0.3f, 1.0f, 0.3f);
    ft.shadowColor = glm::vec3(0.1f, 0.5f, 0.1f);
    ft.shadowOffset = glm::vec2(1, -1);

    floatingTexts.push_back(ft);
}

void update_score_popups(float deltaTime) {
    for (auto it = floatingTexts.begin(); it != floatingTexts.end();) {
        it->life -= deltaTime;
        if (it->life <= 0.0f) {
            it = floatingTexts.erase(it);
        }
        else {
            it->position.y += 40.0f * deltaTime;
            ++it;
        }
    }
}

void render_score_popups(const glm::mat4& proj) {
    for (const auto& ft : floatingTexts) {
        float alpha = ft.life / ft.duration;
        glm::vec3 color = ft.color * alpha;
        glm::vec3 shadow = ft.shadowColor * alpha;
        render_text(ft.text, ft.position.x, ft.position.y, ft.scale, color, shadow, ft.shadowOffset);
    }
}

// Add these functions
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    float zoomFactor = 1.0f + (yoffset * ZOOM_SENSITIVITY * 0.5f);
    cameraZoom *= zoomFactor;
    cameraZoom = glm::clamp(cameraZoom, MIN_ZOOM, MAX_ZOOM);
}

void process_mouse_movement(GLFWwindow* window, double xpos, double ypos) {
    if (middleMousePressed) {
        double deltaY = lastMouseY - ypos;
        float zoomFactor = 1.0f + (deltaY * ZOOM_SENSITIVITY * 0.01f);
        cameraZoom *= zoomFactor;
        cameraZoom = glm::clamp(cameraZoom, MIN_ZOOM, MAX_ZOOM);
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

// ---------------- High Score Functions ----------------
void save_high_score() {
    FILE* file = fopen(HIGH_SCORE_FILE, "wb");
    if (file) {
        fwrite(&highScore, sizeof(int), 1, file);
        fclose(file);
        std::cout << "High score saved: " << highScore << std::endl;
    }
    else {
        std::cout << "Failed to save high score!" << std::endl;
    }
}

void load_high_score() {
    FILE* file = fopen(HIGH_SCORE_FILE, "rb");
    if (file) {
        fread(&highScore, sizeof(int), 1, file);
        fclose(file);
        std::cout << "High score loaded: " << highScore << std::endl;
    }
    else {
        // First time playing - create file with 0 high score
        highScore = 0;
        save_high_score();
        std::cout << "Created new high score file" << std::endl;
    }
}

void check_high_score() {
    if (currentScore > highScore) {
        highScore = currentScore;
        newHighScore = true;
        save_high_score();

        std::cout << "NEW HIGH SCORE! " << highScore << std::endl;
    }
    else {
        newHighScore = false;
    }
}

void spawn_high_score_celebration(b2BodyId player) {
    // Big explosion effect
    b2Vec2 playerPos = b2Body_GetPosition(player);
    for (int i = 0; i < 3; i++) {
        spawn_explosion(glm::vec2(playerPos.x + (i - 1) * 2.0f, playerPos.y + 2.0f));
    }

    // Special floating text
    FloatingText ft;
    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);

    ft.text = "NEW HIGH SCORE!";
    ft.position = glm::vec2(playerPos.x * PIXELS_PER_METER + width / 2.0f,
        playerPos.y * PIXELS_PER_METER + height / 2.0f + 100.0f);
    ft.life = 3.0f;
    ft.duration = 3.0f;
    ft.scale = 0.8f;
    ft.color = glm::vec3(1.0f, 1.0f, 0.0f); // Gold color
    ft.shadowColor = glm::vec3(0.5f, 0.5f, 0.0f);
    ft.shadowOffset = glm::vec2(2, -2);

    floatingTexts.push_back(ft);
}

void check_platform_scoring(b2BodyId player) {
    if (isPlayerDead) return;

    b2Vec2 playerPos = b2Body_GetPosition(player);

    // Check if we've passed any new platforms
    for (int i = lastPlatformIndex; i < platforms.size(); i++) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[i]);

        // If player has passed this platform (player is to the right of platform center + threshold)
        if (playerPos.x > platformPos.x + platformPassThreshold) {
            // Only score if this is a new platform we haven't passed yet
            if (i > lastPlatformIndex) {
                lastPlatformIndex = i;
                currentScore += 5; // Add points for passing a platform

                // Visual and audio feedback
                play_score_sound();
                spawn_score_popup(5, glm::vec2(platformPos.x, platformPos.y + 1.0f));

                // Check for high score
                check_high_score();

                std::cout << "Platform passed! Score: " << currentScore << std::endl;

                // Special celebration for every 10 platforms
                if (lastPlatformIndex % 10 == 0) {
                    spawn_explosion(glm::vec2(platformPos.x, platformPos.y + 2.0f));
                    if (lastPlatformIndex % 20 == 0) { // Extra celebration every 20 platforms
                        heal(10, player); // Heal player as bonus
                    }
                }
            }
        }
    }

    // Handle case where player might be going backwards (should be rare)
    for (int i = lastPlatformIndex; i >= 0; i--) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[i]);
        if (playerPos.x < platformPos.x - platformPassThreshold) {
            lastPlatformIndex = glm::max(0, i - 1);
            break;
        }
    }
}

void spawn_box_on_random_platform() {
    // Remove existing box if any
    if (b2Body_IsValid(currentBox)) {
        UserData* ud = (UserData*)b2Body_GetUserData(currentBox);
        if (ud) {
            delete ud->color;
            delete ud;
        }
        b2DestroyBody(currentBox);
        currentBox = b2_nullBodyId;
    }

    if (platforms.empty()) return;

    // Choose a random platform (avoid the first few platforms)
    int platformIndex = 3 + rand() % (platforms.size() - 3);
    if (platformIndex >= platforms.size()) platformIndex = platforms.size() - 1;

    b2Vec2 platformPos = b2Body_GetPosition(platforms[platformIndex]);

    // Create box on top of the platform
    b2BodyDef boxDef = b2DefaultBodyDef();
    boxDef.type = b2_dynamicBody;
    boxDef.position = { platformPos.x, platformPos.y + PLATFORM_HEIGHT / 2 + 0.5f };

    currentBox = b2CreateBody(g_world, &boxDef);
    UserData* boxUD = new UserData{ ENTITY_BOX, new glm::vec3(g_boxColor), boxTexture, true, 0.0f, false, 1.0f };
    b2Body_SetUserData(currentBox, boxUD);

    b2Polygon boxShape = b2MakeBox(0.5f, 0.5f);
    b2ShapeDef boxSD = b2DefaultShapeDef();
    boxSD.density = 1.0f;
    boxSD.material.friction = 0.3f;
    boxSD.material.restitution = 0.1f;
    b2CreatePolygonShape(currentBox, &boxSD, &boxShape);

    boxSpawned = true;
    boxRespawnTimer = 0.0f;

    std::cout << "Box spawned on platform " << platformIndex << std::endl;
}

void update_ground_detection(b2BodyId player, float deltaTime) {
    bool wasGrounded = isGrounded;

    // Use multiple raycasts for better ground detection
    b2Vec2 playerPos = b2Body_GetPosition(player);
    bool groundHit = false;
    int groundHits = 0;

    // Cast 5 rays across the player's bottom for more precise detection
    float rayOffsets[] = { -0.4f, -0.2f, 0.0f, 0.2f, 0.4f };
    for (float offset : rayOffsets) {
        b2Vec2 origin = { playerPos.x + offset, playerPos.y - 0.9f }; // Start from bottom
        b2Vec2 translation = { 0.0f, -0.3f }; // Shorter, more precise ray

        b2QueryFilter filter = b2DefaultQueryFilter();
        b2RayResult result = b2World_CastRayClosest(g_world, origin, translation, filter);

        if (result.hit && result.fraction < 1.0f) {
            groundHits++;
            if (groundHits >= 2) { // Require at least 2 hits to be considered grounded
                groundHit = true;
                break;
            }
        }
    }

    isGrounded = groundHit;

    // Additional check: if player velocity is very low and we're between platforms, 
    // apply a small upward force to unstick
    b2Vec2 velocity = b2Body_GetLinearVelocity(player);
    if (!isGrounded && fabs(velocity.y) < 0.1f && fabs(velocity.x) < 0.1f) {
        // Check if we might be stuck between platforms
        bool mightBeStuck = false;

        // Cast rays upward to detect platforms above
        for (float offset : rayOffsets) {
            b2Vec2 origin = { playerPos.x + offset, playerPos.y };
            b2Vec2 translation = { 0.0f, 1.5f }; // Check above

            b2QueryFilter filter = b2DefaultQueryFilter();
            b2RayResult result = b2World_CastRayClosest(g_world, origin, translation, filter);

            if (result.hit && result.fraction < 1.0f) {
                mightBeStuck = true;
                break;
            }
        }

        if (mightBeStuck) {
            // Apply small upward force to unstick
            b2Body_ApplyForceToCenter(player, { 0.0f, 5.0f }, true);
        }
    }

    // Coyote time and jump reset logic
    if (wasGrounded && !isGrounded) {
        coyoteTimer = coyoteTime;
    }
    else if (isGrounded) {
        coyoteTimer = 0.0f;
        jumpsRemaining = maxJumps;
        hasDoubleJumped = false;
        isJumping = false;
    }
    else {
        coyoteTimer -= deltaTime;
    }

    if (jumpBufferTimer > 0) jumpBufferTimer -= deltaTime;
}

void check_and_resolve_stuck_situation(b2BodyId player) {
    b2Vec2 playerPos = b2Body_GetPosition(player);
    b2Vec2 velocity = b2Body_GetLinearVelocity(player);

    // Check if player is stuck (very low velocity for extended period)
    static float stuckTimer = 0.0f;
    if (fabs(velocity.x) < 0.1f && fabs(velocity.y) < 0.1f && !isGrounded) {
        stuckTimer += 1.0f / 60.0f; // Assuming 60 FPS
    }
    else {
        stuckTimer = 0.0f;
    }

    // If stuck for more than 1 second, apply rescue force
    if (stuckTimer > 1.0f) {
        // Apply upward and slight forward force
        b2Body_ApplyForceToCenter(player, { 10.0f, 15.0f }, true);
        stuckTimer = 0.0f; // Reset timer

        std::cout << "Emergency unstuck applied!" << std::endl;
    }
}

// ---------------- Main ----------------
int main(int argc, char* argv[]) {
    if (!glfwInit()) return -1;

    // Initialize SDL_mixer (must be after GLFW init)
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cout << "SDL could not initialize! SDL Error: " << SDL_GetError() << std::endl;
    }

    // Initialize audio system
    init_audio();

    // Initialize high score system
    load_high_score();

    // Start background music
    play_background_music();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Box2D Platformer with Infinite Platforms", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);

    // Get primary monitor for fullscreen
    primaryMonitor = glfwGetPrimaryMonitor();

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    // Set up mouse callback
    glfwSetMouseButtonCallback(win, [](GLFWwindow* window, int button, int action, int mods) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        process_mouse_input(window, xpos, ypos, button, action);
        });

    glfwSetScrollCallback(win, scroll_callback);
    glfwSetCursorPosCallback(win, [](GLFWwindow* window, double xpos, double ypos) {
        process_mouse_movement(window, xpos, ypos);
        });

    // Set up window callbacks
    glfwSetWindowSizeCallback(win, [](GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);
        });

    glfwSetWindowMaximizeCallback(win, [](GLFWwindow* window, int maximized) {
        if (maximized) {
            std::cout << "Window maximized" << std::endl;
        }
        else {
            std::cout << "Window restored" << std::endl;
        }
        });

    GLuint vs = compile_shader(vertex_shader_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fragment_shader_src, GL_FRAGMENT_SHADER);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);

    g_vao = create_square_vao_ebo();
    g_uMVP = glGetUniformLocation(g_prog, "uMVP");
    g_uColor = glGetUniformLocation(g_prog, "uColor");
    g_uUseTexture = glGetUniformLocation(g_prog, "uUseTexture");
    g_uTexture = glGetUniformLocation(g_prog, "uTexture");
 

    // Load textures (or create procedural ones if files not available)
    GLuint playerTexture = load_texture("enemy2.png");
    if (playerTexture == 0) {
        playerTexture = create_procedural_texture(64, 64, glm::vec3(0.9f, 0.3f, 0.25f), glm::vec3(0.7f, 0.2f, 0.2f));
    }

    boxTexture = load_texture("playegr.png");
    if (boxTexture == 0) {
        boxTexture = create_procedural_texture(64, 64, glm::vec3(0.2f, 0.5f, 0.8f), glm::vec3(0.1f, 0.3f, 0.6f));
    }

    GLuint groundTexture = load_texture("ground_texture.png");
    if (groundTexture == 0) {
        groundTexture = create_procedural_texture(64, 64, glm::vec3(0.4f, 0.6f, 0.3f), glm::vec3(0.3f, 0.5f, 0.2f));
    }

    // Load button textures from PNG files
    playButtonTexture = load_texture_alpha("play.png");
    pauseButtonTexture = load_texture_alpha("pause.png");
    resumeButtonTexture = load_texture("play.png");
    quitButtonTexture = load_texture_alpha("quit.png");

    // Fallback to procedural textures if PNGs not found
    if (playButtonTexture == 0) {
        std::cout << "Creating fallback play button texture" << std::endl;
        playButtonTexture = create_procedural_texture(64, 64, glm::vec3(0.2f, 0.8f, 0.2f), glm::vec3(0.1f, 0.6f, 0.1f));
    }
    if (pauseButtonTexture == 0) {
        std::cout << "Creating fallback pause button texture" << std::endl;
        pauseButtonTexture = create_procedural_texture(64, 64, glm::vec3(0.8f, 0.8f, 0.2f), glm::vec3(0.6f, 0.6f, 0.1f));
    }
    if (resumeButtonTexture == 0) {
        std::cout << "Creating fallback resume button texture" << std::endl;
        resumeButtonTexture = create_procedural_texture(64, 64, glm::vec3(0.2f, 0.5f, 0.8f), glm::vec3(0.1f, 0.3f, 0.6f));
    }
    if (quitButtonTexture == 0) {
        std::cout << "Creating fallback quit button texture" << std::endl;
        quitButtonTexture = create_procedural_texture(64, 64, glm::vec3(0.8f, 0.2f, 0.2f), glm::vec3(0.6f, 0.1f, 0.1f));
    }

    // Initialize systems
    init_particle_system();
    init_font_rendering();
    init_health_bar();
    init_gui_buttons();

    // Box2D world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f,-10.0f };
    g_world = b2CreateWorld(&worldDef);

    // Generate initial platforms
    generate_initial_platforms();

    // Player - spawn on first platform
    b2BodyDef playerDef = b2DefaultBodyDef();
    playerDef.type = b2_dynamicBody;

    // Position player on the first platform
    float spawnX = -10.0f;
    float spawnY = 2.0f;
    if (!platforms.empty()) {
        b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
        spawnX = platformPos.x;
        spawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
    }

    playerDef.position = { spawnX, spawnY };
    b2BodyId player = b2CreateBody(g_world, &playerDef);
    UserData* playerUD = new UserData{ ENTITY_PLAYER,nullptr, playerTexture,true, 0.0f, false, 1.0f };
    b2Body_SetUserData(player, playerUD);
    b2Polygon playerShape = b2MakeBox(1.0f, 1.0f);
    b2ShapeDef playerSD = b2DefaultShapeDef();
    playerSD.density = 1.0f;
    playerSD.material.friction = 0.1f;  // Reduced friction
    playerSD.material.restitution = 0.1f;  // Small bounce
    b2CreatePolygonShape(player, &playerSD, &playerShape);

    // Don't create initial box here - it will be spawned automatically
    currentBox = b2_nullBodyId;
    boxSpawned = false;

    float timeStep = 1.0f / 60.0f;
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // Get current window size for dynamic projection
        int width, height;
        glfwGetFramebufferSize(win, &width, &height);
        proj = glm::ortho(0.0f, float(width), 0.0f, float(height), -1.0f, 1.0f);

        process_input(win, player, deltaTime);
        update_ground_detection(player, deltaTime);

        if (gameOver && glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS) {
            static bool enterKeyHandled = false;
            if (!enterKeyHandled) {
                reset_game(player);
                enterKeyHandled = true;

                // Small delay to prevent multiple rapid restarts
                glfwWaitEventsTimeout(0.1);
            }
        }
        else if (glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_RELEASE) {
            static bool enterKeyHandled = false;
            enterKeyHandled = false;
        }

        // toggles pause/resume
        if (currentGameState == STATE_PAUSED) {
            pause_background_music();
        }
        else if (currentGameState == STATE_PLAYING) {
            resume_background_music();
        }

        // Update camera position
        update_camera(player, deltaTime);

        // Create camera view matrix
        glm::mat4 view = glm::mat4(1.0f);
        view = glm::translate(view, glm::vec3(-cameraPosition.x + width / 2.0f,
            -cameraPosition.y + height / 2.0f, 0.0f));
        view = glm::scale(view, glm::vec3(cameraZoom, cameraZoom, 1.0f));

        // Projection matrix (screen space)
        glm::mat4 viewProj = proj * view;

        // Only update physics and game logic when playing
        if (currentGameState == STATE_PLAYING && !isPlayerDead) {
            b2World_Step(g_world, timeStep, 8);

            // Check for stuck situations
            check_and_resolve_stuck_situation(player);

            // Update game systems
            update_particles(deltaTime);
            update_score_popups(deltaTime);
            update_platforms(player); // Update the infinite platform system
            check_platform_scoring(player);

            // Box respawn logic
            if (!boxSpawned && boxRespawnTimer > 0.0f) {
                boxRespawnTimer -= deltaTime;
                if (boxRespawnTimer <= 0.0f && !platforms.empty()) {
                    spawn_box_on_random_platform();
                }
            }

            // Auto-spawn box if none exists and enough time has passed
            if (!boxSpawned && boxRespawnTimer <= 0.0f && !platforms.empty()) {
                spawn_box_on_random_platform();
            }

            // --- 1-meter proximity AABB ---
            AABB playerBox = getAABBWithProximity(player, 1.0f, 1.0f, 0.5f);
            bool isPlayerNear = false;

            // Only check proximity if the box exists and is valid
            if (b2Body_IsValid(currentBox) && !isPlayerDead) {
                AABB boxAABB = getAABBWithProximity(currentBox, 0.5f, 0.5f, 0.0f);
                isPlayerNear = aabbOverlap(playerBox, boxAABB);

                UserData* boxUD = (UserData*)b2Body_GetUserData(currentBox);
                if (boxUD) {
                    if (isPlayerNear) {
                        *(boxUD->color) = g_yellowColor;
                        update_box_animation(boxUD, deltaTime, true);

                        if (!wasPlayerNear) {
                            // Player collected the box!
                            currentScore += 10;
                            play_score_sound();

                            b2Vec2 boxPos = b2Body_GetPosition(currentBox);
                            spawn_score_popup(10, glm::vec2(boxPos.x, boxPos.y + 1.0f));

                            // Remove the box
                            delete boxUD->color;
                            delete boxUD;
                            b2DestroyBody(currentBox);
                            currentBox = b2_nullBodyId;
                            boxSpawned = false;
                            boxRespawnTimer = BOX_RESPAWN_TIME;

                            // Check for high score
                            check_high_score();

                            std::cout << "Box collected! Score: " << currentScore << std::endl;
                        }
                        wasPlayerNear = true;
                    }
                    else {
                        *(boxUD->color) = g_boxColor;
                        update_box_animation(boxUD, deltaTime, false);
                        wasPlayerNear = false;
                    }
                }
            }
            else {
                wasPlayerNear = false;
            }

            // Check if player fell below all platforms
            b2Vec2 ppos = b2Body_GetPosition(player);
            if (ppos.y < -10.0f) {
                take_damage(10, player);
                // Respawn player on a platform
                float respawnX = -5.0f;
                float respawnY = 5.0f;
                if (!platforms.empty()) {
                    b2Vec2 platformPos = b2Body_GetPosition(platforms[0]);
                    respawnX = platformPos.x;
                    respawnY = platformPos.y + PLATFORM_HEIGHT + 1.0f;
                }
                b2Body_SetTransform(player, { respawnX, respawnY }, b2MakeRot(0.0f));
                b2Body_SetLinearVelocity(player, { 0.0f,0.0f });
            }
        }
        else if (isPlayerDead) {
            // Still update respawn timer when paused but dead
            respawnTimer -= deltaTime;

            if (gameOver) {
                // Update game over timer
                gameOverTimer -= deltaTime;
            }
            else if (respawnTimer <= 0.0f && !gameOver) {  // Only respawn if game is not over
                respawn_player(player);
            }
        }

        // --- Rendering ---
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);
        glUniform1i(g_uTexture, 0);

        // Draw function using camera system
        auto drawBody = [&](b2BodyId b, float w, float h) {
            b2Vec2 pos = b2Body_GetPosition(b);
            float angle = b2Rot_GetAngle(b2Body_GetRotation(b));

            // Use world coordinates (camera will handle transformation)
            float worldX = pos.x * PIXELS_PER_METER;
            float worldY = pos.y * PIXELS_PER_METER;

            glm::mat4 model(1.0f);
            model = glm::translate(model, { worldX, worldY, 0.0f });
            model = glm::rotate(model, angle, { 0,0,1 });

            UserData* ud = (UserData*)b2Body_GetUserData(b);
            float scale = ud ? ud->animationScale : 1.0f;
            model = glm::scale(model, { w * PIXELS_PER_METER * 2.0f * scale,
                                        h * PIXELS_PER_METER * 2.0f * scale, 1.0f });

            // Use viewProj for world objects
            glm::mat4 mvp = viewProj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));

            if (ud) {
                glUniform3f(g_uColor, ud->color ? ud->color->r : 1.0f,
                    ud->color ? ud->color->g : 1.0f,
                    ud->color ? ud->color->b : 1.0f);
                glUniform1i(g_uUseTexture, ud->useTexture);
                if (ud->useTexture && ud->textureID != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, ud->textureID);
                }
            }
            else {
                glUniform3f(g_uColor, 1.0f, 1.0f, 1.0f);
                glUniform1i(g_uUseTexture, false);
            }

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            };

        // Render platforms
        for (auto& platform : platforms) {
            drawBody(platform, PLATFORM_WIDTH / 2, PLATFORM_HEIGHT / 2);
        }

        // Render other objects
        if (!isPlayerDead) {
            drawBody(player, 1.0f, 1.0f);
        }

        if (b2Body_IsValid(currentBox)) {
            drawBody(currentBox, 0.5f, 0.5f);
        }

      

        // Render health bar above player (using camera)
        if (!isPlayerDead) {
            render_world_space_health_bar(player, viewProj);
        }

        // Render particles (using camera)
        render_particles(viewProj);

        // Render score popups (screen space)
        render_score_popups(proj);

        // --- GUI Rendering (screen space) ---
        // Game Over Screen
        if (gameOver) {

            float centerX = width / 2.0f;
            float centerY = height / 2.0f;

            // Game Over text
            render_text("GAME OVER", centerX - 150.0f, centerY + 80.0f, 1.5f,
                glm::vec3(1, 0.2f, 0.2f), glm::vec3(0.5f, 0.1f, 0.1f), glm::vec2(3, -3));

            // Final score
            render_text("Final Score: " + std::to_string(currentScore),
                centerX - 120.0f, centerY + 30.0f, 0.8f,
                glm::vec3(1, 1, 1), glm::vec3(0, 0, 0), glm::vec2(2, -2));

            // High score
            std::string hsText = "High Score: " + std::to_string(highScore);
            glm::vec3 hsColor = newHighScore ? glm::vec3(1, 1, 0) : glm::vec3(1, 1, 1);
            render_text(hsText, centerX - 100.0f, centerY - 10.0f, 0.7f,
                hsColor, glm::vec3(0.2f, 0.2f, 0.4f), glm::vec2(1, -1));

            if (newHighScore) {
                render_text("NEW HIGH SCORE!", centerX - 120.0f, centerY - 40.0f, 0.8f,
                    glm::vec3(1, 1, 0), glm::vec3(0.5f, 0.5f, 0), glm::vec2(2, -2));
            }

            // Death count
            render_text("Deaths: " + std::to_string(deathCount) + "/" + std::to_string(MAX_DEATHS),
                centerX - 80.0f, centerY - 80.0f, 0.6f,
                glm::vec3(1, 0.5f, 0.5f), glm::vec3(0.5f, 0.2f, 0.2f), glm::vec2(1, -1));

            render_text("Press ENTER to restart now", centerX - 120.0f, centerY - 150.0f, 0.5f,
                glm::vec3(0.8f, 0.8f, 0.8f), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec2(1, -1));
        }

        // Play/Pause button in top-right corner
        if (currentGameState == STATE_PLAYING) {
            render_button(width - 60, height - 60, 50, 50, pauseButtonTexture, "PAUSE");
        }
        else {
            render_button(width - 60, height - 60, 50, 50, playButtonTexture, "PLAY");
        }

        // Pause menu
        if (showPauseMenu) {
         

            // Pause menu panel
            float centerX = width / 2.0f;
            float centerY = height / 2.0f;
            // Get mouse position for hover detection
            double mouseX, mouseY;
            glfwGetCursorPos(win, &mouseX, &mouseY);
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(win, &fbWidth, &fbHeight);
            float mouseYFlipped = fbHeight - mouseY; // Flip Y coordinate

            float buttonWidth = 200.0f;
            float buttonHeight = 30.0f; // Smaller height for text-only buttons

            // Check hover states
            bool resumeHovered = check_button_hover(mouseX, mouseYFlipped, centerX - buttonWidth / 2, centerY, buttonWidth, buttonHeight);
            bool quitHovered = check_button_hover(mouseX, mouseYFlipped, centerX - buttonWidth / 2, centerY - 50, buttonWidth, buttonHeight);

            // Pause text
            render_text("GAME PAUSED", centerX - 80, centerY + 80, 0.8f,
                glm::vec3(1, 1, 1), glm::vec3(0, 0, 0), glm::vec2(2, -2));

            // Resume button - text only with hover effect
            render_text_button(centerX - buttonWidth / 2, centerY, buttonWidth, buttonHeight, "RESUME", 0.6f, resumeHovered);

            // Quit button - text only with hover effect  
            render_text_button(centerX - buttonWidth / 2, centerY - 50, buttonWidth, buttonHeight, "QUIT", 0.6f, quitHovered);

            // Game info footer
            render_text("Press ESC to resume", centerX - 80, centerY - 130, 0.4f,
                glm::vec3(0.8f, 0.8f, 0.8f), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec2(1, -1));
        }

        // Current score
        render_text("Score:" + std::to_string(currentScore), 20.0f, height - 40.0f, 0.8f,
            glm::vec3(1, 1, 1), glm::vec3(0.2f, 0.6f, 1.0f), glm::vec2(2, -2));

        // High score
        std::string hsText = "High: " + std::to_string(highScore);
        glm::vec3 hsColor = newHighScore ? glm::vec3(1, 1, 0) : glm::vec3(0.8f, 0.8f, 1.0f);
        render_text(hsText, 20.0f, height - 70.0f, 0.6f,
            hsColor, glm::vec3(0.2f, 0.2f, 0.4f), glm::vec2(1, -1));

        // Death counter
        std::string deathsText = "Deaths: " + std::to_string(deathCount) + "/" + std::to_string(MAX_DEATHS);
        glm::vec3 deathsColor = (deathCount >= MAX_DEATHS - 1) ? glm::vec3(1, 0.3f, 0.3f) : glm::vec3(0.8f, 0.8f, 0.8f);
        render_text(deathsText, 20.0f, height - 100.0f, 0.5f,
            deathsColor, glm::vec3(0.2f, 0.2f, 0.2f), glm::vec2(1, -1));

        // Controls help
        render_text("F11/Alt+Enter:Fullscreen  ESC:Pause  H:Damage  J:Heal  Arrow Keys:Move  Space:Jump",
            20.0f, 30.0f, 0.4f,
            glm::vec3(0.8f, 0.8f, 0.8f), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec2(1, -1));

        // Game state text
        if (currentGameState == STATE_PAUSED && !showPauseMenu) {
            render_text("PAUSED", width / 2 - 40, height / 2, 1.0f,
                glm::vec3(1, 1, 0), glm::vec3(0.5f, 0.5f, 0), glm::vec2(2, -2));
        }

        if (isPlayerDead && !gameOver) {
            std::string respawnText = "Respawning in " + std::to_string(static_cast<int>(respawnTimer)) + "s";
            render_text(respawnText, width / 2 - 150.0f, height / 2, 1.0f,
                glm::vec3(1, 0.3f, 0.3f), glm::vec3(0.5f, 0.1f, 0.1f), glm::vec2(2, -2));
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup
    delete playerUD;

    // Cleanup platforms
    for (auto& platform : platforms) {
        UserData* ud = (UserData*)b2Body_GetUserData(platform);
        delete ud;
        b2DestroyBody(platform);
    }
    platforms.clear();

    // Cleanup health bar
    glDeleteVertexArrays(1, &healthBarVAO);
    glDeleteBuffers(1, &healthBarVBO);

    // Cleanup GUI
    glDeleteVertexArrays(1, &buttonVAO);
    glDeleteBuffers(1, &buttonVBO);
    glDeleteTextures(1, &playButtonTexture);
    glDeleteTextures(1, &pauseButtonTexture);
    glDeleteTextures(1, &resumeButtonTexture);
    glDeleteTextures(1, &quitButtonTexture);

    // Cleanup font resources
    glDeleteVertexArrays(1, &fontVAO);
    glDeleteBuffers(1, &fontVBO);
    glDeleteProgram(fontProgram);
    for (auto& character : characters) {
        glDeleteTextures(1, &character.second.textureID);
    }

    glDeleteTextures(1, &playerTexture);
    glDeleteTextures(1, &boxTexture);
    glDeleteTextures(1, &groundTexture);
    glDeleteTextures(1, &g_particleTexture);

    // Cleanup audio
    if (audioInitialized) {
        Mix_HaltMusic();
        Mix_HaltChannel(-1); // Stop all channels

        if (backgroundMusic) Mix_FreeMusic(backgroundMusic);
        if (jumpSound) Mix_FreeChunk(jumpSound);
        if (explosionSound) Mix_FreeChunk(explosionSound);
        if (scoreSound) Mix_FreeChunk(scoreSound);
        if (highScoreSound) Mix_FreeChunk(highScoreSound);

        Mix_CloseAudio();
        SDL_Quit();
    }

    b2DestroyWorld(g_world);
    glfwTerminate();
    return 0;
}
