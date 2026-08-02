#pragma once

#include <cstdint>

#include "../SDK/SDK.h"

class C_TerrorPlayer;
class CBaseEntity;

namespace PortalPlayerTeleport
{
    CBaseEntity* ResolveServerLocalPlayer(C_TerrorPlayer* clientPlayer);

    bool Commit(
        C_TerrorPlayer* clientPlayer,
        const Vector& origin,
        const QAngle& angles,
        const Vector& velocity,
        const matrix3x4_t& entryToExit,
        std::uint32_t traversalEpoch,
        int commandNumber);
}
