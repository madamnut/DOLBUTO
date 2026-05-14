#include "game/GameClient.h"
#include "platform/Log.h"
#include "platform/RuntimePaths.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
    constexpr int WindowWidth = 1280;
    constexpr int WindowHeight = 720;
    constexpr const char* WindowTitle = "DOLBUTO";

    void setWindowIcon(GLFWwindow* window)
    {
        const std::filesystem::path path = dolbuto::assetDirectory() / "textures" / "icon" / "icon.png";

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            return;
        }

        GLFWimage icon{};
        icon.width = width;
        icon.height = height;
        icon.pixels = pixels;
        glfwSetWindowIcon(window, 1, &icon);

        stbi_image_free(pixels);
    }

    class GlfwRuntime
    {
    public:
        GlfwRuntime()
        {
            if (glfwInit() != GLFW_TRUE)
            {
                error_ = "Failed to initialize GLFW.";
                return;
            }
            initialized_ = true;

            if (glfwVulkanSupported() != GLFW_TRUE)
            {
                error_ = "GLFW could not find Vulkan support.";
                return;
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

            window_ = glfwCreateWindow(WindowWidth, WindowHeight, WindowTitle, nullptr, nullptr);
            if (window_ == nullptr)
            {
                error_ = "Failed to create GLFW window.";
                return;
            }

            setWindowIcon(window_);
        }

        ~GlfwRuntime()
        {
            if (window_ != nullptr)
            {
                glfwDestroyWindow(window_);
            }
            if (initialized_)
            {
                glfwTerminate();
            }
        }

        GLFWwindow* window() const
        {
            return window_;
        }

        void throwIfFailed() const
        {
            if (!error_.empty())
            {
                throw std::runtime_error(error_);
            }
        }

    private:
        GLFWwindow* window_ = nullptr;
        bool initialized_ = false;
        std::string error_;
    };
}

int main()
{
    try
    {
        dolbuto::log::initialize();
        {
            GlfwRuntime glfw;
            glfw.throwIfFailed();

            dolbuto::GameClient game(glfw.window());
            game.run();
        }
        dolbuto::log::shutdown();
        return 0;
    }
    catch (const std::exception& exception)
    {
        dolbuto::log::error(exception.what());
        dolbuto::log::shutdown();
        return 1;
    }
    catch (...)
    {
        dolbuto::log::error("Unknown fatal error.");
        dolbuto::log::shutdown();
        return 1;
    }
}
