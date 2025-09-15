// main.cpp
// Simple Box2D + OpenGL Game: Move & Jump a Box

#include <iostream>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ---------------- Settings ----------------
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const float PIXELS_PER_METER = 50.0f; // scale Box2D meters -> pixels

// Globals to be used in callback
b2WorldId g_world;
std::vector<b2BodyId> g_boxes;
GLuint g_vao;
GLuint g_prog;
GLint g_uMVP;
GLint g_uColor;

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

// ---------------- Input ----------------
void create_box_at_mouse(double xpos, double ypos) {
    // Convert screen coordinates to Box2D world coordinates
    float worldX = (static_cast<float>(xpos) - WINDOW_WIDTH / 2.0f) / PIXELS_PER_METER;
    float worldY = (static_cast<float>(ypos) - WINDOW_HEIGHT / 2.0f) / PIXELS_PER_METER;
    // Invert Y-axis since screen coordinates start from top
    worldY *= -1.0f;

    b2BodyDef boxDef = b2DefaultBodyDef();
    boxDef.type = b2_dynamicBody;
    boxDef.position = { worldX, worldY };
    b2BodyId newBox = b2CreateBody(g_world, &boxDef);

    b2Polygon dynBox = b2MakeBox(0.5f, 0.5f); // 1x1 meter box
    b2ShapeDef boxSD = b2DefaultShapeDef();
    boxSD.density = 1.0f;
    boxSD.material.friction = 0.3f;
    b2CreatePolygonShape(newBox, &boxSD, &dynBox);

    g_boxes.push_back(newBox);
}

void mouse_button_callback(GLFWwindow* win, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        create_box_at_mouse(xpos, ypos);
    }
}

void process_input(GLFWwindow* win, b2BodyId player) {
    float moveForce = 20.0f;
    float jumpImpulse = 6.0f;

    if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) {
        b2Body_ApplyForceToCenter(player, { -moveForce, 0.0f }, true);
    }
    if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        b2Body_ApplyForceToCenter(player, { moveForce, 0.0f }, true);
    }
    if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
        // Only jump if nearly on the ground
        b2Vec2 vel = b2Body_GetLinearVelocity(player);
        if (fabs(vel.y) < 0.01f) {
            b2Body_ApplyLinearImpulseToCenter(player, { 0.0f, jumpImpulse }, true);
        }
    }
    if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) {
        // Manual reset
        b2Body_SetTransform(player, { 0.0f, 10.0f }, b2MakeRot(0.0f));
        b2Body_SetLinearVelocity(player, { 0.0f, 0.0f });
    }
}

// ---------------- Main ----------------
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Simple Box2D Game", nullptr, nullptr);
    if (!win) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(win);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    GLuint vs = compile_shader(vertex_shader_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fragment_shader_src, GL_FRAGMENT_SHADER);
    g_prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    g_vao = create_square_vao();
    g_uMVP = glGetUniformLocation(g_prog, "uMVP");
    g_uColor = glGetUniformLocation(g_prog, "uColor");

    // Set mouse button callback
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    // --------- Box2D world setup ---------
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f, -10.0f };
    g_world = b2CreateWorld(&worldDef);

    // Ground
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;

    float groundHeight = 0.1f; // half-height in meters (so total height = 1m)
    groundDef.position = { 0.0f, -5.0f };
    b2BodyId ground = b2CreateBody(g_world, &groundDef);

   
    // Shape: make it wide but thin
    b2Polygon groundShape = b2MakeBox(50.0f, groundHeight); // 100m wide, 1m tall
    b2ShapeDef groundSD = b2DefaultShapeDef();
    b2CreatePolygonShape(ground, &groundSD, &groundShape);

    // Player box
    b2BodyDef playerBoxDef = b2DefaultBodyDef();
    playerBoxDef.type = b2_dynamicBody;
    playerBoxDef.position = { 0.0f, 10.0f };
    b2BodyId player = b2CreateBody(g_world, &playerBoxDef);
    b2Polygon playerBoxShape = b2MakeBox(1.0f, 1.0f); // 2x2 meters
    b2ShapeDef playerBoxSD = b2DefaultShapeDef();
    playerBoxSD.density = 1.0f;
    playerBoxSD.material.friction = 0.3f;
    b2CreatePolygonShape(player, &playerBoxSD, &playerBoxShape);

    float timeStep = 1.0f / 60.0f;

    // Projection: pixels to NDC
    glm::mat4 proj = glm::ortho(0.0f, float(WINDOW_WIDTH),
        0.0f, float(WINDOW_HEIGHT),
        -1.0f, 1.0f);

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    while (!glfwWindowShouldClose(win)) {
        // Input
        process_input(win, player);

        // Physics step
        b2World_Step(g_world, timeStep, 8);

        // Auto reset if player falls below screen
        b2Vec2 ppos = b2Body_GetPosition(player);
        if (ppos.y < -20.0f) {
            b2Body_SetTransform(player, { 0.0f, 10.0f }, b2MakeRot(0.0f));
            b2Body_SetLinearVelocity(player, { 0.0f, 0.0f });
        }

        // Rendering
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);

        // --- Draw ground ---
        {
            b2Vec2 pos = b2Body_GetPosition(ground);
            b2Rot rot = b2Body_GetRotation(ground);
            float angle = b2Rot_GetAngle(rot);
            float px = pos.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
            float py = pos.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;
            float w = 100.0f * PIXELS_PER_METER;
            float h = 2.0f * groundHeight * PIXELS_PER_METER; // make height match

            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(px, py, 0.0f));
            model = glm::rotate(model, angle, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(w, h, 1.0f));

            glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(g_uColor, 0.2f, 0.8f, 0.2f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // --- Draw player box ---
        {
            b2Vec2 pos = b2Body_GetPosition(player);
            b2Rot rot = b2Body_GetRotation(player);
            float angle = b2Rot_GetAngle(rot);
            float px = pos.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
            float py = pos.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;
            float w = 2.0f * PIXELS_PER_METER;
            float h = 2.0f * PIXELS_PER_METER;

            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(px, py, 0.0f));
            model = glm::rotate(model, angle, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(w, h, 1.0f));

            glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(g_uColor, 0.9f, 0.3f, 0.25f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // --- Draw all created boxes ---
        for (b2BodyId box : g_boxes) {
            b2Vec2 pos = b2Body_GetPosition(box);
            b2Rot rot = b2Body_GetRotation(box);
            float angle = b2Rot_GetAngle(rot);
            float px = pos.x * PIXELS_PER_METER + WINDOW_WIDTH / 2.0f;
            float py = pos.y * PIXELS_PER_METER + WINDOW_HEIGHT / 2.0f;
            float w = 1.0f * PIXELS_PER_METER;
            float h = 1.0f * PIXELS_PER_METER;

            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(px, py, 0.0f));
            model = glm::rotate(model, angle, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(w, h, 1.0f));

            glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(g_uColor, 0.2f, 0.5f, 0.8f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }


        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    b2DestroyWorld(g_world);
    glfwTerminate();
    return 0;
}
