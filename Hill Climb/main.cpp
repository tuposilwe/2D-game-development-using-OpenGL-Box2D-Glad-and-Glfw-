// main.cpp
// Box2D + OpenGL Game with Textures, 1-meter proximity AABB collision and EBO

#include <iostream>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

enum EntityType { ENTITY_NONE, ENTITY_PLAYER, ENTITY_BOX, ENTITY_GROUND };

struct UserData {
    EntityType type;
    glm::vec3* color;
    GLuint textureID;
    bool useTexture;
};

// Colors
glm::vec3 g_playerColor(0.9f, 0.3f, 0.25f);
glm::vec3 g_boxColor(0.2f, 0.5f, 0.8f);
glm::vec3 g_yellowColor(1.0f, 1.0f, 0.0f);
glm::vec3 g_groundColor(0.4f, 0.6f, 0.3f);

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

    // Box2D world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f,-10.0f };
    g_world = b2CreateWorld(&worldDef);

    // Ground
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    groundDef.position = { 0.0f,-5.0f };
    b2BodyId ground = b2CreateBody(g_world, &groundDef);
    UserData* groundUD = new UserData{ ENTITY_GROUND, &g_groundColor, groundTexture, true };
    b2Body_SetUserData(ground, groundUD);
    b2Polygon groundShape = b2MakeBox(50.0f, 0.1f);
    b2ShapeDef groundSD = b2DefaultShapeDef();
    b2CreatePolygonShape(ground, &groundSD, &groundShape);

    // Player
    b2BodyDef playerDef = b2DefaultBodyDef();
    playerDef.type = b2_dynamicBody;
    playerDef.position = { 0.0f,10.0f };
    b2BodyId player = b2CreateBody(g_world, &playerDef);
    UserData* playerUD = new UserData{ ENTITY_PLAYER,&g_playerColor, playerTexture,false };
    b2Body_SetUserData(player, playerUD);
    b2Polygon playerShape = b2MakeBox(1.0f, 1.0f);
    b2ShapeDef playerSD = b2DefaultShapeDef(); playerSD.density = 1.0f; playerSD.material.friction = 0.3f;
    b2CreatePolygonShape(player, &playerSD, &playerShape);

    // Single Box
    b2BodyDef boxDef = b2DefaultBodyDef();
    boxDef.type = b2_dynamicBody;
    boxDef.position = { 2.0f,6.0f };
    b2BodyId box = b2CreateBody(g_world, &boxDef);
    UserData* boxUD = new UserData{ ENTITY_BOX, new glm::vec3(g_boxColor), boxTexture, true };
    b2Body_SetUserData(box, boxUD);
    b2Polygon boxShape = b2MakeBox(0.5f, 0.5f);
    b2ShapeDef boxSD = b2DefaultShapeDef(); boxSD.density = 1.0f; boxSD.material.friction = 0.3f;
    b2CreatePolygonShape(box, &boxSD, &boxShape);

    float timeStep = 1.0f / 60.0f;
    glm::mat4 proj = glm::ortho(0.0f, float(WINDOW_WIDTH), 0.0f, float(WINDOW_HEIGHT), -1.0f, 1.0f);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);


    // tells OpenGL to properly handle the alpha channel in your PNGs.
    // Now only the opaque parts of the texture are drawn,and the transparent parts stay transparent.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!glfwWindowShouldClose(win)) {
        process_input(win, player);
        b2World_Step(g_world, timeStep, 8);

        // --- 1-meter proximity AABB ---
        AABB playerBox = getAABBWithProximity(player, 1.0f, 1.0f, 1.0f); // 1 meter
        *(boxUD->color) = g_boxColor; // reset
        AABB boxAABB = getAABBWithProximity(box, 0.5f, 0.5f, 0.0f); // box normal size
        if (aabbOverlap(playerBox, boxAABB)) *(boxUD->color) = g_yellowColor;

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
            model = glm::scale(model, { w * PIXELS_PER_METER * 2.0f,h * PIXELS_PER_METER * 2.0f,1.0f });
            glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));

            UserData* ud = (UserData*)b2Body_GetUserData(b);
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

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup
    delete playerUD;
    delete boxUD->color;
    delete boxUD;
    delete groundUD;

    glDeleteTextures(1, &playerTexture);
    glDeleteTextures(1, &boxTexture);
    glDeleteTextures(1, &groundTexture);

    b2DestroyWorld(g_world);
    glfwTerminate();
    return 0;
}