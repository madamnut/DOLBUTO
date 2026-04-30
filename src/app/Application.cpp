#include "app/Application.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr int WindowWidth = 1280;
        constexpr int WindowHeight = 720;
        constexpr const char* WindowTitle = "DOLBUTO";
    }

    Application::Application()
    {
        initWindow();
    }

    Application::~Application()
    {
        shutdownWindow();
    }

    void Application::run()
    {
        while (!glfwWindowShouldClose(window_))
        {
            glfwPollEvents();
        }
    }

    void Application::initWindow()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            throw std::runtime_error("Failed to initialize GLFW.");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window_ = glfwCreateWindow(WindowWidth, WindowHeight, WindowTitle, nullptr, nullptr);
        if (window_ == nullptr)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window.");
        }
    }

    void Application::shutdownWindow()
    {
        if (window_ != nullptr)
        {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }

        glfwTerminate();
    }
}
