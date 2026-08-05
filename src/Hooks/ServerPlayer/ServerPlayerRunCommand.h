#pragma once

#include <cstddef>

#include "../../SDK/SDK.h"

class C_TerrorPlayer;
class CUserCmd;
class IMoveHelper;

namespace Hooks::ServerPlayerRunCommand
{
    // SourceMod's L4D2 SDKTools gamedata identifies CBasePlayer::PlayerRunCommand
    // as Windows vtable slot 454; the official signature takes
    // (CUserCmd*, IMoveHelper*).
    constexpr std::size_t VTableIndex = 454u;

    bool EnsureInstalled(C_TerrorPlayer* clientPlayer);
    bool IsInstalled();
    CUserCmd* GetActiveCommand();

    namespace PlayerRunCommand
    {
        inline Hook::CFunction Func;
        using FN = void(__fastcall*)(void*, void*, CUserCmd*, IMoveHelper*);

        void __fastcall Detour(
            void* ecx,
            void* edx,
            CUserCmd* command,
            IMoveHelper* moveHelper);
    }
}
