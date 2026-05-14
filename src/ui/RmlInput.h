#pragma once

#include <RmlUi/Core/Input.h>

struct GLFWwindow;

namespace dolbuto::ui
{
    Rml::Input::KeyIdentifier rmlKeyFromGlfw(int key);
    int rmlKeyModifiersFromGlfw(int modifiers);
    int currentRmlKeyModifiers(GLFWwindow* window);
}
