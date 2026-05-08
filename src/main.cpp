#include "app/Application.h"
#include "platform/Log.h"

#include <exception>

int main()
{
    try
    {
        {
            dolbuto::Application app;
            app.run();
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
