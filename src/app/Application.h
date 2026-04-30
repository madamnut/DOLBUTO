#pragma once

struct GLFWwindow;

namespace dolbuto
{
    class Application
    {
    public:
        Application();
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

    private:
        void initWindow();
        void shutdownWindow();

        GLFWwindow* window_ = nullptr;
    };
}
