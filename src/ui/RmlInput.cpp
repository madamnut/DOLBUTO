#include "ui/RmlInput.h"

#include <GLFW/glfw3.h>

namespace dolbuto::ui
{
    Rml::Input::KeyIdentifier rmlKeyFromGlfw(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (key - GLFW_KEY_A));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (key - GLFW_KEY_0));
        }

        switch (key)
        {
        case GLFW_KEY_SPACE: return Rml::Input::KI_SPACE;
        case GLFW_KEY_BACKSPACE: return Rml::Input::KI_BACK;
        case GLFW_KEY_TAB: return Rml::Input::KI_TAB;
        case GLFW_KEY_ENTER: return Rml::Input::KI_RETURN;
        case GLFW_KEY_ESCAPE: return Rml::Input::KI_ESCAPE;
        case GLFW_KEY_LEFT: return Rml::Input::KI_LEFT;
        case GLFW_KEY_RIGHT: return Rml::Input::KI_RIGHT;
        case GLFW_KEY_UP: return Rml::Input::KI_UP;
        case GLFW_KEY_DOWN: return Rml::Input::KI_DOWN;
        case GLFW_KEY_DELETE: return Rml::Input::KI_DELETE;
        case GLFW_KEY_HOME: return Rml::Input::KI_HOME;
        case GLFW_KEY_END: return Rml::Input::KI_END;
        case GLFW_KEY_MINUS: return Rml::Input::KI_OEM_MINUS;
        case GLFW_KEY_EQUAL: return Rml::Input::KI_OEM_PLUS;
        case GLFW_KEY_COMMA: return Rml::Input::KI_OEM_COMMA;
        case GLFW_KEY_PERIOD: return Rml::Input::KI_OEM_PERIOD;
        default: return Rml::Input::KI_UNKNOWN;
        }
    }

    int rmlKeyModifiersFromGlfw(int modifiers)
    {
        int rmlModifiers = 0;
        if ((modifiers & GLFW_MOD_CONTROL) != 0)
        {
            rmlModifiers |= Rml::Input::KM_CTRL;
        }
        if ((modifiers & GLFW_MOD_SHIFT) != 0)
        {
            rmlModifiers |= Rml::Input::KM_SHIFT;
        }
        if ((modifiers & GLFW_MOD_ALT) != 0)
        {
            rmlModifiers |= Rml::Input::KM_ALT;
        }
        if ((modifiers & GLFW_MOD_SUPER) != 0)
        {
            rmlModifiers |= Rml::Input::KM_META;
        }
        if ((modifiers & GLFW_MOD_CAPS_LOCK) != 0)
        {
            rmlModifiers |= Rml::Input::KM_CAPSLOCK;
        }
        if ((modifiers & GLFW_MOD_NUM_LOCK) != 0)
        {
            rmlModifiers |= Rml::Input::KM_NUMLOCK;
        }
        return rmlModifiers;
    }

    int currentRmlKeyModifiers(GLFWwindow* window)
    {
        if (window == nullptr)
        {
            return 0;
        }

        int modifiers = 0;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_CONTROL;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_SHIFT;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_ALT;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
        {
            modifiers |= GLFW_MOD_SUPER;
        }
        return rmlKeyModifiersFromGlfw(modifiers);
    }
}
