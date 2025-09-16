// main.cpp
// Simple Box2D + OpenGL Game with one dynamic box and reliable AABB collision color

#include <iostream>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

enum EntityType {
    ENTITY_NONE,
    ENTITY_PLAYER,
    ENTITY_BOX
};

struct UserData {
    EntityType type;
    glm::vec3* color;
};

// Colors
glm::vec3 g_playerColor(0.9f, 0.3f, 0.25f);
glm::vec3 g_boxColor(0.2f, 0.5f, 0.8f);
glm::vec3 g_yellowColor(1.0f, 1.0f, 0.0f);

// ---------------- Shaders ----------------
const char* vertex_shader_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

const char* fragment_shader_src = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ---------------- Helpers ----------------
GLuint compile_shader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "Shader compile error: " << log << "\n";
        exit(1);
    }
    return s;
}

GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        std::cerr << "Program link error: " << log << "\n";
        exit(1);
    }
    return p;
}

GLuint create_square_vao() {
    float vertices[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f
    };
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return vao;
}

// ---------------- AABB ----------------
struct AABB {
    float minX, minY, maxX, maxY;
};

AABB getAABB(b2BodyId body, float halfW, float halfH) {
    b2Vec2 pos = b2Body_GetPosition(body);
    return { pos.x - halfW, pos.y - halfH, pos.x + halfW, pos.y + halfH };
}

bool aabbOverlap(const AABB& a, const AABB& b) {
    return !(a.maxX < b.minX || a.minX > b.maxX || a.maxY < b.minY || a.minY > b.maxY);
}

// ---------------- Input ----------------
void process_input(GLFWwindow* win, b2BodyId player) {
    float moveForce = 20.0f;
    float jumpImpulse = 6.0f;

    if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS)
        b2Body_ApplyForceToCenter(player, { -moveForce,0.0f }, true);
    if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS)
        b2Body_ApplyForceToCenter(player, { moveForce,0.0f }, true);
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
        b2Vec2 vel = b2Body_GetLinearVelocity(player);
        if (fabs(vel.y) < 0.01f)
            b2Body_ApplyLinearImpulseToCenter(player, { 0.0f,jumpImpulse }, true);
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

    GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Box2D One Box", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    GLuint vs = compile_shader(vertex_shader_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fragment_shader_src, GL_FRAGMENT_SHADER);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    g_vao = create_square_vao();
    g_uMVP = glGetUniformLocation(g_prog, "uMVP");
    g_uColor = glGetUniformLocation(g_prog, "uColor");

    // --------- Box2D world ---------
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f,-10.0f };
    g_world = b2CreateWorld(&worldDef);

    // Ground
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    groundDef.position = { 0.0f,-5.0f };
    b2BodyId ground = b2CreateBody(g_world, &groundDef);
    b2Polygon groundShape = b2MakeBox(50.0f, 0.1f);
    b2ShapeDef groundSD = b2DefaultShapeDef();
    b2CreatePolygonShape(ground, &groundSD, &groundShape);

    // Player
    b2BodyDef playerDef = b2DefaultBodyDef();
    playerDef.type = b2_dynamicBody;
    playerDef.position = { 0.0f,10.0f };
    b2BodyId player = b2CreateBody(g_world, &playerDef);
    UserData* playerUD = new UserData{ ENTITY_PLAYER,&g_playerColor };
    b2Body_SetUserData(player, playerUD);
    b2Polygon playerShape = b2MakeBox(1.0f, 1.0f);
    b2ShapeDef playerSD = b2DefaultShapeDef(); playerSD.density = 1.0f; playerSD.material.friction = 0.3f;
    b2CreatePolygonShape(player, &playerSD, &playerShape);

    // Single Box
    b2BodyDef boxDef = b2DefaultBodyDef();
    boxDef.type = b2_dynamicBody;
    boxDef.position = { 2.0f,6.0f };
    b2BodyId box = b2CreateBody(g_world, &boxDef);
    UserData* boxUD = new UserData{ ENTITY_BOX,new glm::vec3(g_boxColor) };
    b2Body_SetUserData(box, boxUD);
    b2Polygon boxShape = b2MakeBox(0.5f, 0.5f);
    b2ShapeDef boxSD = b2DefaultShapeDef(); boxSD.density = 1.0f; boxSD.material.friction = 0.3f;
    b2CreatePolygonShape(box, &boxSD, &boxShape);

    float timeStep = 1.0f / 60.0f;
    glm::mat4 proj = glm::ortho(0.0f, float(WINDOW_WIDTH), 0.0f, float(WINDOW_HEIGHT), -1.0f, 1.0f);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    while (!glfwWindowShouldClose(win)) {
        process_input(win, player);
        b2World_Step(g_world, timeStep, 8);

        // --- AABB collision ---
        AABB playerBox = getAABB(player, 1.0f, 1.0f);
        *(boxUD->color) = g_boxColor; // reset
        AABB boxAABB = getAABB(box, 0.5f, 0.5f);
        if (aabbOverlap(playerBox, boxAABB))
            *(boxUD->color) = g_yellowColor;

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

        auto drawBody = [&](b2BodyId body, float w_m, float h_m) {
            b2Vec2 pos = b2Body_GetPosition(body);
            float angle = b2Rot_GetAngle(b2Body_GetRotation(body));
            float px = pos.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
            float py = pos.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;
            float w = w_m * PIXELS_PER_METER * 2.0f;
            float h = h_m * PIXELS_PER_METER * 2.0f;
            glm::mat4 model(1.0f);
            model = glm::translate(model, { px,py,0.0f });
            model = glm::rotate(model, angle, { 0,0,1 });
            model = glm::scale(model, { w,h,1.0f });
            glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            UserData* ud = (UserData*)b2Body_GetUserData(body);
            glm::vec3 color = ud ? *(ud->color) : glm::vec3(1.0f, 1.0f, 1.0f);
            glUniform3f(g_uColor, color.r, color.g, color.b);
            glDrawArrays(GL_TRIANGLES, 0, 6);
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
    b2DestroyWorld(g_world);
    glfwTerminate();
    return 0;
}
