#pragma once

#include "../../Util/Hook/Hook.h"

namespace Hooks::InputAngles
{
    namespace AdjustAngles
    {
        inline Hook::CFunction Func;

        using FN = void(__thiscall*)(void*, int, float);
        void __fastcall Detour(void* ecx, void* edx, int slot, float frametime);
    }

    void Init();
    void LogLateDiagnosticProbe();
    bool IsSignatureValidated();
    bool IsAbiValidated();
}
