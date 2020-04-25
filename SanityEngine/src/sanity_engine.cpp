﻿#include "sanity_engine.hpp"

#include <GLFW/glfw3.h>
#include <minitrace.h>
#include <spdlog/spdlog.h>
#include <time.h>

#include "core/abort.hpp"

int main() {
    const Settings settings{};

    SanityEngine engine{settings};

    engine.run();

    spdlog::warn("REMAIN INDOORS");
}

static void error_callback(const int error, const char* description) { spdlog::error("{} (GLFW error {}}", description, error); }

static void key_func(GLFWwindow* window, const int key, int /* scancode */, const int action, const int mods) {
    auto* input_manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));

    input_manager->on_key(key, action, mods);
}

SanityEngine::SanityEngine(const Settings& settings_in) : settings{settings_in}, input_manager{std::make_unique<InputManager>()} {
    mtr_init("SanityEngine.json");

    MTR_SCOPE("D3D12Engine", "D3D12Engine");

    spdlog::info("HELLO HUMAN");

    if(!glfwInit()) {
        critical_error("Could not initialize GLFW");
    }

    glfwSetErrorCallback(error_callback);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(1000, 480, "Sanity Engine", nullptr, nullptr);
    if(window == nullptr) {
        critical_error("Could not create GLFW window");
    }

    spdlog::info("Created window");

    glfwSetWindowUserPointer(window, input_manager.get());

    glfwSetKeyCallback(window, key_func);

    renderer = std::make_unique<renderer::Renderer>(window, settings.num_in_flight_frames);
    spdlog::info("Initialized renderer");

    create_debug_cube();
}

SanityEngine::~SanityEngine() {
    glfwDestroyWindow(window);

    glfwTerminate();

    mtr_shutdown();
}

void SanityEngine::run() {
    double last_frame_duration = 0;

    clock_t last_frame_end_time{0};

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        update_player_movement(last_frame_duration);

        tick(last_frame_duration);

        const auto end_time = clock();

        last_frame_duration = static_cast<double>(end_time - last_frame_end_time) / static_cast<double>(CLOCKS_PER_SEC);
        last_frame_end_time = end_time;

        mtr_flush();
    }
}

void SanityEngine::create_debug_cube() {
    const auto vertices = std::vector<BveVertex>{
        // Front
        {/* .position = */ {-0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},

        // Right
        {/* .position = */ {0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},

        // Left
        {/* .position = */ {-0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},

        // Back
        {/* .position = */ {0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},

        // Top
        {/* .position = */ {-0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, 0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, 0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},

        // Bottom
        {/* .position = */ {0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {0.5f, -0.5f, -0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
        {/* .position = */ {-0.5f, -0.5f, 0.5f}, /* .normal = */ {}, /* .color = */ {}, /* .texcoord = */ {}},
    };

    const auto indices = std::vector<uint32_t>{
        // front face
        0,
        1,
        2, // first triangle
        0,
        3,
        1, // second triangle

        // left face
        4,
        5,
        6, // first triangle
        4,
        7,
        5, // second triangle

        // right face
        8,
        9,
        10, // first triangle
        8,
        11,
        9, // second triangle

        // back face
        12,
        13,
        14, // first triangle
        12,
        15,
        13, // second triangle

        // top face
        16,
        17,
        18, // first triangle
        16,
        19,
        17, // second triangle

        // bottom face
        20,
        21,
        22, // first triangle
        20,
        23,
        21, // second triangle
    };

    auto cube_renderable = renderer->create_static_mesh(vertices, indices);

    const auto cube_entity = registry.create();

    registry.emplace<renderer::StaticMeshRenderableComponent>(cube_entity, cube_renderable);
}

void SanityEngine::update_player_movement(const double delta_time) {
    auto& player_transform = registry.get<TransformComponent>(player);
    const auto forward = player_transform.get_forward_vector();

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        // Move the player entity in its forward direction
        player_transform.position += forward * delta_time;

    } else if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        // Move the player entity in its backward direction
    }

    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        // Move the player entity in its right direction

    } else if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        // Move the player entity in its left direction
    }

    if(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        // Move the player entity along the global up direction

    } else if(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        // Move the player along the global down direction
    }
}

void SanityEngine::tick(double delta_time) {
    MTR_SCOPE("D3D12Engine", "tick");

    renderer->render_scene(registry);
}
