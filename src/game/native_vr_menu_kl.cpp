#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_vr_menu_kl.hpp"
#include "vr_mod_shared/ini.hpp"
#include <atomic>
#include <cstring>

namespace NativeVRMenuKL {
namespace {
using namespace Kl1Modifiers;
using Parse = bool(__thiscall*)(void*, char*, unsigned, bool);
using Get = int(__thiscall*)(void*);
using Set = void(__thiscall*)(void*, int);
constexpr std::uintptr_t Parser = 0x28960, ParseFromMenu = 0x178198;
constexpr std::uintptr_t ToggleTextTable = 0x55C924, ToggleGet = 0x173840, ToggleSet = 0x1739A0;
constexpr std::uintptr_t IncreaseSetReturn = 0x174CD3, DecreaseSetReturn = 0x174D33;
std::uintptr_t image;
Parse originalParse;
Get originalGet;
Set originalSet;
std::wstring profile;
std::array<std::atomic<int>, Options.size()> values{};
bool showTesting;
Apply applyModifier;
Log logMenu;

std::string_view RootOptions(std::string_view xml) {
    const auto start = xml.find("<Window Name=\"OptionsMenu\"");
    if (start == xml.npos) return {};
    const auto end = xml.find("</Window>", start);
    if (end == xml.npos) return {};
    return xml.substr(start, end + sizeof("</Window>") - 1 - start);
}

std::string Page(bool testing) {
    std::string panel = R"xml(
    <Window Name="KL1VRSettings" Template="WindowOptionsDefault" LocaleBasePath="AllLevels/Menu/Options">
        <GuiElement Template="OptionsDefault"/>
        <GuiElement Type="GUIGroup" Template="ScreenBaseSize">
            <GuiElement Template="TextHeader" Text="%VR Settings"/>
            <GuiElement Template="LongLineTop"/>
            <GuiElement Template="LongLineBottom"/>
            <GuiElement Type="GUIGroup" Template="OptionsBaseSize" AlignH="Center" X="0">
)xml";
    const auto count = testing ? Options.size() : Index(Id::Invulnerable);
    for (std::size_t i = 0; i < count; ++i) {
        const auto bar = std::to_string(80 + i * 35);
        const std::string suffix = "0" + std::to_string(i + 1);
        panel += "<GuiElement Name=\"KL1VRFocus" + suffix + "\" Y=\"" + bar +
            "\" X=\"-30\" Graphic=\"FocusBar_optionsright#" + suffix + "\" AlignH=\"Left\" Template=\"FocusBar\"/>\n";
    }
    panel += "<GuiElement Template=\"OptionsGroup\">\n";
    for (std::size_t i = 0; i < count; ++i) {
        panel += std::string("<GuiElement Template=\"OptionValueText\" Name=\"") + Options[i].widget +
            "Value\" X=\"0\" Y=\"" + std::to_string(85 + i * 35) + "\"/>\n";
    }
    panel += "</GuiElement>\n<GuiElement GeomSpace=\"18\" Template=\"OptionsGroup\">\n";
    for (const bool right : {false, true}) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::string name = Options[i].widget;
            panel += std::string("<GuiElement Template=\"") + (right ? "OptionsArrowR_T" : "OptionsArrowL_T") +
                "\" Message=\"" + (right ? "Increase" : "Decrease") + "\" FocusParent=\"" + name +
                "\" Receiver=\"" + name + "\" Y=\"" + std::to_string(87 + i * 35) + "\"/>\n";
        }
    }
    panel += "</GuiElement>\n";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& option = Options[i];
        panel += std::string("<GuiElement Template=\"OptionsToggler_T\" Name=\"") + option.widget +
            "\" Text=\"%" + option.label + "\" TextReceiver=\"" + option.widget +
            "Value\" FocusGraphic=\"KL1VRFocus0" + std::to_string(i + 1) + "\" Y=\"" + std::to_string(85 + i * 35) +
            "\" HelpText=\"%" + option.help + "\" HelpText2=\"/Menu/Generic/Buttons_Back\" LeftRight=\"True\" Wrap=\"True\">\n";
        panel += i == Index(Id::ReticleStyle)
            ? "<Text Text=\"%Dot\"/><Text Text=\"%Original\"/>\n"
            : "<Text Text=\"/Menu/Generic/Disabled\"/><Text Text=\"/Menu/Generic/Enabled\"/>\n";
        panel += "</GuiElement>\n";
    }
    panel += R"xml(
            </GuiElement>
            <GuiElement Type="GUIGroup" Template="BottomFramePosSize" Navigation="Horizontal" ReverseNavigation="True">
                <Platform PC_GC="True">
                    <GuiElement Template="ButtonBack_INV_GT" RequiresWindowFocus="True"/>
                    <GuiElement Template="Back_GT" Name="OptionsButtonsHelp"/>
                </Platform>
                <Platform PC_KEYBOARD="True">
                    <GuiElement Template="BackButtonNoXPos_PC" AlignH="Right" X="0" TakeFocus="False"/>
                </Platform>
                <GuiElement Template="HelpText_GT" Name="OptionsHelp"/>
            </GuiElement>
        </GuiElement>
    </Window>
)xml";
    return panel;
}

bool Read(std::uintptr_t address, void* output, std::size_t bytes) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, bytes, &got) && got == bytes;
}

Id Widget(void* self) {
    const auto* words = static_cast<const std::uintptr_t*>(self);
    if (words[0] != image + ToggleTextTable) return Id::Count;
    const auto* name = reinterpret_cast<const char*>(words[1]);
    if (!name || std::strncmp(name, "KL1VR", 5) != 0) return Id::Count;
    const auto id = FindWidget(name);
    return id == Id::Invulnerable && !showTesting ? Id::Count : id;
}

bool __fastcall ParseHook(void* self, void*, char* data, unsigned length, bool final) {
    // This call is inside ZGUIXMLReader2, which passes a temporary copy of
    // the archive member to the synchronous parser.
    if (reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) == image + ParseFromMenu + 5 &&
        data && length && length <= 1024 * 1024) {
        std::string updated;
        if (ExtendOptions({data, length}, showTesting, updated)) {
            if (logMenu) logMenu("Native VR settings: extended the game's Options XML.");
            return originalParse(self, updated.data(), static_cast<unsigned>(updated.size()), final);
        }
    }
    return originalParse(self, data, length, final);
}

int __fastcall GetHook(void* self, void*) {
    const auto id = Widget(self);
    return id == Id::Count ? originalGet(self) : values[Index(id)].load(std::memory_order_relaxed);
}

void __fastcall SetHook(void* self, void*, int value) {
    const auto id = Widget(self);
    if (id == Id::Count) {
        originalSet(self, value);
        return;
    }
    // Native setup/reset can also call the setter. Only the stock increment
    // and decrement handlers represent a change by the player.
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if (caller != image + IncreaseSetReturn && caller != image + DecreaseSetReturn) return;
    if ((value != 0 && value != 1) || value == values[Index(id)].load(std::memory_order_relaxed)) return;
    const auto& option = Spec(id);
    std::vector<VrModIni::File> current(1);
    std::wstring error;
    if (VrModIni::Load(profile, current[0], error, true)) {
        auto updated = current;
        VrModIni::Patch(updated[0], option.section, option.key, value ? L"1" : L"0");
        if (VrModIni::Save(current, std::move(updated), error)) {
            applyModifier(id, value != 0);
            values[Index(id)].store(value, std::memory_order_relaxed);
            if (logMenu) {
                const auto message = std::string("Native VR settings: ") + option.widget + "=" + std::to_string(value);
                logMenu(message.c_str());
            }
            return;
        }
    }
    if (logMenu) logMenu("Native VR settings: could not save vr.ini.");
}
}

bool ExtendOptions(std::string_view xml, bool testing, std::string& updated) {
    const auto root = RootOptions(xml);
    const auto close = xml.rfind("</Windows>");
    constexpr std::string_view controls = "Window=\"OptionsControllerMenu\"";
    const auto button = root.find(controls);
    if (button == root.npos || close == xml.npos || xml.find("KL1VRSettings") != xml.npos ||
        xml.find("<Template Name=\"OptionsToggler_T\"") == xml.npos ||
        xml.find("Name=\"OMfbl#07\"") == xml.npos) return false;
    const auto after = root.find("/>", button);
    if (after == root.npos || root.find(controls, after) != root.npos) return false;
    const auto start = static_cast<std::size_t>(root.data() - xml.data());
    if (close < start + root.size()) return false;

    std::string options(root);
    // Make room directly after Controls. Move the existing buttons to the
    // next stock focus bars without renaming any of the graphics themselves.
    for (char slot = '6'; slot >= '4'; --slot) {
        std::string attribute = "FocusGraphic=\"OMfbl#0";
        attribute += slot;
        attribute += '"';
        for (auto at = options.find(attribute, after); at != options.npos; at = options.find(attribute, at + attribute.size())) {
            options[at + attribute.size() - 2] = static_cast<char>(slot + 1);
        }
    }
    options.insert(after + 2, R"xml(
            <GuiElement Template="OptionButton" Window="KL1VRSettings" Text="%VR Settings" FocusGraphic="OMfbl#04"
                HelpText="%Configure VR gameplay. Changes are saved immediately." FadeInDelay="0.20" ClosePrevious="True"/>
)xml");
    updated.assign(xml);
    updated.insert(close, Page(testing));
    updated.replace(start, root.size(), options);
    return true;
}

bool Install(std::uintptr_t base, const std::wstring& ini, bool testing, Apply apply, Log log) {
    const auto match = [base](std::uintptr_t rva, const auto& expected) {
        unsigned char actual[sizeof(expected)];
        return Read(base + rva, actual, sizeof(actual)) && std::memcmp(actual, expected, sizeof(actual)) == 0;
    };
    const unsigned char parser[] = {0x8B,0x44,0x24,0x04,0x56,0x57,0x8B,0xF9};
    const unsigned char readerCall[] = {0xE8,0xC3,0x07,0xEB,0xFF,0xB0,0x01};
    const unsigned char get[] = {0x56,0x8B,0xF1,0x8B,0x86,0xAC,0,0,0};
    const unsigned char set[] = {0x6A,0xFF,0x68,0x20,0x78,0x8F,0x00};
    const unsigned char stepCall[] = {0x56,0x8B,0xCF,0xFF,0xD2,0x3B,0xDE};
    std::uintptr_t propertySlots[2]{};
    if (!apply || !match(Parser, parser) || !match(ParseFromMenu, readerCall) || !match(ToggleGet, get) ||
        !match(ToggleSet, set) || !match(IncreaseSetReturn - 5, stepCall) || !match(DecreaseSetReturn - 5, stepCall) ||
        !Read(base + ToggleTextTable + 0x88, propertySlots, sizeof(propertySlots)) ||
        propertySlots[0] != base + ToggleGet || propertySlots[1] != base + ToggleSet) return false;
    image = base;
    profile = ini;
    showTesting = testing;
    applyModifier = apply;
    logMenu = log;
    for (std::size_t i = 0; i < Options.size(); ++i) {
        const auto& option = Options[i];
        values[i].store(GetPrivateProfileIntW(option.section, option.key, option.fallback, profile.c_str()) != 0,
            std::memory_order_relaxed);
    }

    struct Hook {std::uintptr_t rva; void* detour; void** original;};
    const Hook hooks[] = {
        {ToggleGet, reinterpret_cast<void*>(GetHook), reinterpret_cast<void**>(&originalGet)},
        {ToggleSet, reinterpret_cast<void*>(SetHook), reinterpret_cast<void**>(&originalSet)},
        {Parser, reinterpret_cast<void*>(ParseHook), reinterpret_cast<void**>(&originalParse)},
    };
    const auto target = [base](const Hook& hook) { return reinterpret_cast<void*>(base + hook.rva); };
    std::size_t created = 0;
    for (const auto& hook : hooks) {
        if (MH_CreateHook(target(hook), hook.detour, hook.original) != MH_OK) break;
        ++created;
    }
    bool ready = created == std::size(hooks);
    if (ready) {
        for (const auto& hook : hooks) {
            if (MH_QueueEnableHook(target(hook)) != MH_OK) { ready = false; break; }
        }
        if (ready) ready = MH_ApplyQueued() == MH_OK;
    }
    if (!ready) {
        for (std::size_t i = created; i > 0; --i) MH_RemoveHook(target(hooks[i - 1]));
    }
    return ready;
}
}
