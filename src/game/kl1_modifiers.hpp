#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace Kl1Modifiers {
enum class Id {Cinematics, Reload, Reticle, MountedReticle, ReticleStyle, MountedMouseAim, Laser, Hints, Invulnerable, Count};
struct Option {
    const char* widget;
    const wchar_t* section;
    const wchar_t* key;
    int fallback;
    const char* label;
    const char* help;
};
inline constexpr std::array<Option, static_cast<std::size_t>(Id::Count)> Options{{
    {"KL1VRScenes", L"VR", L"CinematicVR", 0, "VR Cutscenes", "Show in-engine cutscenes in VR."},
    {"KL1VRReload", L"Weapons", L"PhysicalReload", 0, "Physical Reload", "Reload supported weapons with your tracked hands."},
    {"KL1VRReticle", L"Weapons", L"NativeReticle", 1, "Weapon Reticle", "Show the aiming reticle for handheld weapons."},
    {"KL1VRMounted", L"Weapons", L"MountedReticle", 1, "Mounted Gun Reticle", "Show the aiming reticle for mounted guns."},
    {"KL1VRStyle", L"Weapons", L"ReticleStyle", 1, "Reticle Style", "Choose the aiming graphic for handheld and mounted weapons."},
    {"KL1VRMouse", L"Weapons", L"MountedMouseAim", 0, "Mounted Mouse Aim", "Aim mounted guns with the mouse while keeping the VR view."},
    {"KL1VRLaser", L"Weapons", L"LaserSight", 0, "Laser Sight", "Show a laser beam along the barrel."},
    {"KL1VRHints", L"HUD", L"GameHints", 1, "Game Hints", "Show game instructions and nearby actions."},
    {"KL1VRGod", L"Debug", L"Invulnerable", 0, "Invulnerable (Testing)", "Prevent damage to the character you control."},
}};
constexpr std::size_t Index(Id id) { return static_cast<std::size_t>(id); }
constexpr const Option& Spec(Id id) { return Options[Index(id)]; }
inline Id FindWidget(std::string_view name) {
    for (std::size_t i = 0; i < Options.size(); ++i) {
        if (name == Options[i].widget) return static_cast<Id>(i);
    }
    return Id::Count;
}
}
