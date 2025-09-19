// main.cpp
// Box2D + OpenGL Game with Textures, 1-meter proximity AABB collision and EBO

#include <iostream>
#include <cmath>
#include <vector>
#include <cstdlib> // For rand()
#include <map>
#include <string>

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

// ---------------- Settings ----------------
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const float PIXELS_PER_METER = 50.0f;

// Globals
b2WorldId g_world;
GLuint g_vao;
GLuint g_prog;
GLint g_uMVP;
GLint g_uColor;
GLint g_uUseTexture;
GLint g_uTexture;

enum EntityType { ENTITY_NONE, ENTITY_PLAYER, ENTITY_BOX, ENTITY_GROUND, ENTITY_BULLET };

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
void render_particles(const glm::mat4& proj);



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
void update_score_popups(float deltaTime);
void render_score_popups(const glm::mat4& proj);

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
    if (uUseTexture) {
        FragColor = texture(uTexture, TexCoord) * vec4(uColor, 1.0);
    } else {
        FragColor = vec4(uColor, 1.0);
    }
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

// ---------------- Input ----------------
void process_input(GLFWwindow* win, b2BodyId player) {
    float moveForce = 20.0f;
    float jumpImpulse = 6.0f;
    if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) b2Body_ApplyForceToCenter(player, { -moveForce,0.0f }, true);
    if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) b2Body_ApplyForceToCenter(player, { moveForce,0.0f }, true);
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
        b2Vec2 vel = b2Body_GetLinearVelocity(player);
        if (fabs(vel.y) < 0.01f) b2Body_ApplyLinearImpulseToCenter(player, { 0.0f,jumpImpulse }, true);
    }
    if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) {
        b2Body_SetTransform(player, { 0.0f,10.0f }, b2MakeRot(0.0f));
        b2Body_SetLinearVelocity(player, { 0.0f,0.0f });
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
        std::cout << "Failed to load particle.png, creating fallback texture" << std::endl;

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

void render_particles(const glm::mat4& proj) {
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glUniform1i(g_uTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_particleTexture);

    for (const auto& p : particles) {
        float px = p.position.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
        float py = p.position.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;

        glm::mat4 model(1.0f);
        model = glm::translate(model, { px, py, 0.0f });
        model = glm::rotate(model, p.rotation, { 0, 0, 1 });
        model = glm::scale(model, { p.size * PIXELS_PER_METER * 2.0f,
                                  p.size * PIXELS_PER_METER * 2.0f, 1.0f });

        glm::mat4 mvp = proj * model;
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
    glUseProgram(fontProgram);
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(WINDOW_WIDTH),
        0.0f, static_cast<float>(WINDOW_HEIGHT));
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
    ft.text = "+" + std::to_string(points);
    ft.position = glm::vec2(position.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f,
        position.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f);
    ft.life = 1.5f;
    ft.duration = 1.5f;
    ft.scale = 0.5f; // this makes score popup small
    ft.color = glm::vec3(1.0f, 1.0f, 1.0f); // White
    ft.shadowColor = glm::vec3(0.2f, 0.6f, 1.0f);  // Blue shadow
    ft.shadowOffset = glm::vec2(2, -2); // Smaller shadow offset

    floatingTexts.push_back(ft);
}

void update_score_popups(float deltaTime) {
    for (auto it = floatingTexts.begin(); it != floatingTexts.end();) {
        it->life -= deltaTime;
        if (it->life <= 0.0f) {
            it = floatingTexts.erase(it);
        }
        else {
            it->position.y += 40.0f * deltaTime; // rise speed
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

// ---------------- Main ----------------
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Box2D Textured Game", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

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

    GLuint boxTexture = load_texture("playegr.png");
    if (boxTexture == 0) {
        boxTexture = create_procedural_texture(64, 64, glm::vec3(0.2f, 0.5f, 0.8f), glm::vec3(0.1f, 0.3f, 0.6f));
    }

    GLuint groundTexture = load_texture("ground_texture.png");
    if (groundTexture == 0) {
        groundTexture = create_procedural_texture(64, 64, glm::vec3(0.4f, 0.6f, 0.3f), glm::vec3(0.3f, 0.5f, 0.2f));
    }

    // Initialize bullet system
    init_particle_system();

    // Initialize font rendering
    init_font_rendering();

    // Box2D world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f,-10.0f };
    g_world = b2CreateWorld(&worldDef);

    // Ground
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    groundDef.position = { 0.0f,-5.0f };
    b2BodyId ground = b2CreateBody(g_world, &groundDef);
    UserData* groundUD = new UserData{ ENTITY_GROUND, &g_groundColor, groundTexture, true, 0.0f, false, 1.0f };
    b2Body_SetUserData(ground, groundUD);
    b2Polygon groundShape = b2MakeBox(50.0f, 0.1f);
    b2ShapeDef groundSD = b2DefaultShapeDef();
    b2CreatePolygonShape(ground, &groundSD, &groundShape);

    // Player
    b2BodyDef playerDef = b2DefaultBodyDef();
    playerDef.type = b2_dynamicBody;
    playerDef.position = { 0.0f,10.0f };
    b2BodyId player = b2CreateBody(g_world, &playerDef);
    UserData* playerUD = new UserData{ ENTITY_PLAYER,nullptr, playerTexture,true, 0.0f, false, 1.0f };
    b2Body_SetUserData(player, playerUD);
    b2Polygon playerShape = b2MakeBox(1.0f, 1.0f);
    b2ShapeDef playerSD = b2DefaultShapeDef(); playerSD.density = 1.0f; playerSD.material.friction = 0.3f;
    b2CreatePolygonShape(player, &playerSD, &playerShape);

    // Single Box
    b2BodyDef boxDef = b2DefaultBodyDef();
    boxDef.type = b2_dynamicBody;
    boxDef.position = { 2.0f,6.0f };
    b2BodyId box = b2CreateBody(g_world, &boxDef);
    UserData* boxUD = new UserData{ ENTITY_BOX, new glm::vec3(g_boxColor), boxTexture, true, 0.0f, false, 1.0f };
    b2Body_SetUserData(box, boxUD);
    b2Polygon boxShape = b2MakeBox(0.5f, 0.5f);
    b2ShapeDef boxSD = b2DefaultShapeDef(); boxSD.density = 1.0f; boxSD.material.friction = 0.3f;
    b2CreatePolygonShape(box, &boxSD, &boxShape);

    float timeStep = 1.0f / 60.0f;
    glm::mat4 proj = glm::ortho(0.0f, float(WINDOW_WIDTH), 0.0f, float(WINDOW_HEIGHT), -1.0f, 1.0f);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    // Tells OpenGL to properly handle the alpha channel in your PNGs
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // For tracking time between frames
    float lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        // Calculate delta time
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        process_input(win, player);
        b2World_Step(g_world, timeStep, 8);

        // Update particles
        update_particles(deltaTime);
        

        // Update score popups
        update_score_popups(deltaTime);

        // --- 1-meter proximity AABB ---
        AABB playerBox = getAABBWithProximity(player, 1.0f, 1.0f, 1.0f); // 1 meter
        *(boxUD->color) = g_boxColor; // reset
        AABB boxAABB = getAABBWithProximity(box, 0.5f, 0.5f, 0.0f); // box normal size

        bool isPlayerNear = aabbOverlap(playerBox, boxAABB);
        if (isPlayerNear) {
            *(boxUD->color) = g_yellowColor;

            // Add score and spawn popup (only once per collision)
            if (!wasPlayerNear) {
                currentScore += 10;
                b2Vec2 boxPos = b2Body_GetPosition(box);
                spawn_score_popup(10, glm::vec2(boxPos.x, boxPos.y + 1.0f));
                std::cout << "Score: " << currentScore << std::endl;
            }
            wasPlayerNear = true;
        }
        else {
            wasPlayerNear = false;
        }

        // Update box animation
        update_box_animation(boxUD, deltaTime, isPlayerNear);

        // Auto reset if player falls
        b2Vec2 ppos = b2Body_GetPosition(player);
        if (ppos.y < -20.0f) {
            b2Body_SetTransform(player, { 0.0f,10.0f }, b2MakeRot(0.0f));
            b2Body_SetLinearVelocity(player, { 0.0f,0.0f });
        }

        // --- Rendering ---
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);

        // Set texture unit
        glUniform1i(g_uTexture, 0);

        auto drawBody = [&](b2BodyId b, float w, float h) {
            b2Vec2 pos = b2Body_GetPosition(b);
            float angle = b2Rot_GetAngle(b2Body_GetRotation(b));
            float px = pos.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
            float py = pos.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;
            glm::mat4 model(1.0f);
            model = glm::translate(model, { px,py,0.0f });
            model = glm::rotate(model, angle, { 0,0,1 });

            // Apply animation scale if needed
            UserData* ud = (UserData*)b2Body_GetUserData(b);
            float scale = ud ? ud->animationScale : 1.0f;
            model = glm::scale(model, { w * PIXELS_PER_METER * 2.0f * scale,
                                        h * PIXELS_PER_METER * 2.0f * scale, 1.0f });

            glm::mat4 mvp = proj * model;
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

        drawBody(ground, 50.0f, 0.1f);
        drawBody(player, 1.0f, 1.0f);
        drawBody(box, 0.5f, 0.5f);

        // Render particles
        render_particles(proj);

        // Render score popups
        render_score_popups(proj);

        // Render current score in the corner with pixel font
        render_text("Score:" + std::to_string(currentScore), 20.0f, WINDOW_HEIGHT - 40.0f, 0.8f,
            glm::vec3(1, 1, 1), glm::vec3(0.2f, 0.6f, 1.0f), glm::vec2(2, -2));

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup
    delete playerUD;
    delete boxUD->color;
    delete boxUD;
    delete groundUD;

    // Cleanup font resources
    glDeleteVertexArrays(1, &fontVAO);
    glDeleteBuffers(1, &fontVBO);
    glDeleteProgram(fontProgram);

    // Cleanup character textures
    for (auto& character : characters) {
        glDeleteTextures(1, &character.second.textureID);
    }

    glDeleteTextures(1, &playerTexture);
    glDeleteTextures(1, &boxTexture);
    glDeleteTextures(1, &groundTexture);
   
    glDeleteTextures(1, &g_particleTexture);

    b2DestroyWorld(g_world);
    glfwTerminate();
    return 0;
}