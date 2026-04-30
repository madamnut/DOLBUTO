#include "app/Application.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        dolbuto::Application app;
        app.run();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
