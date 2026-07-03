#pragma once

#include <algorithm>

namespace dolbuto::game
{
    struct PlayerStats
    {
        int hp = 100;
        int maxHp = 100;
        int hunger = 100;
        int maxHunger = 100;
        int thirst = 100;
        int maxThirst = 100;
        int oxygen = 100;
        int maxOxygen = 100;

        void clamp()
        {
            maxHp = std::max(1, maxHp);
            maxHunger = std::max(1, maxHunger);
            maxThirst = std::max(1, maxThirst);
            maxOxygen = std::max(1, maxOxygen);
            hp = std::clamp(hp, 0, maxHp);
            hunger = std::clamp(hunger, 0, maxHunger);
            thirst = std::clamp(thirst, 0, maxThirst);
            oxygen = std::clamp(oxygen, 0, maxOxygen);
        }
    };
}
