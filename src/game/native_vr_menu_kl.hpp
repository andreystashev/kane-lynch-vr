#pragma once
#include "kl1_modifiers.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace NativeVRMenuKL {
using Apply = void(*)(Kl1Modifiers::Id, bool);
using Log = void(*)(const char*);
bool ExtendOptions(std::string_view xml, bool testing, std::string& updated);
bool Install(std::uintptr_t image, const std::wstring& ini, bool testing, Apply apply, Log log);
}
