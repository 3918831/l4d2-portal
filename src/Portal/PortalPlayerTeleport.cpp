#include "PortalPlayerTeleport.h"
#include "PortalPhysicsMode.h"
#include "PortalTransform.h"
#include "PortalTransitionDecision.h"

#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/edict.h"
#include "../SDK/L4D2/Includes/iserverunknown.h"
#include "../SDK/L4D2/Interfaces/ClientEntityList.h"
#include "../SDK/L4D2/Interfaces/CServerTools.h"
#include "../SDK/L4D2/Interfaces/EngineClient.h"
#include "../SDK/L4D2/Interfaces/IPlayerInfoManager.h"
#include "../SDK/L4D2/Server/ServerDataMap.h"
#include "../Util/Logger/Logger.h"

#include <Windows.h>

#include <cmath>

namespace
{
    constexpr size_t kServerTeleportVTableIndex = 118;

    struct ServerTeleportFieldCache
    {
        CBaseEntity* entity = nullptr;
        L4D2::ServerDataMap::FieldInfo origin;
        L4D2::ServerDataMap::FieldInfo velocity;
        L4D2::ServerDataMap::FieldInfo baseVelocity;
        L4D2::ServerDataMap::FieldInfo angles;
        L4D2::ServerDataMap::FieldInfo eyeAngles;
        L4D2::ServerDataMap::FieldInfo moveType;
        L4D2::ServerDataMap::FieldInfo flags;
        L4D2::ServerDataMap::FieldInfo groundEntity;
        bool originResolved = false;
        bool velocityResolved = false;
        bool baseVelocityResolved = false;
        bool anglesResolved = false;
        bool eyeAnglesResolved = false;
        bool moveTypeResolved = false;
        bool flagsResolved = false;
        bool groundEntityResolved = false;
    };

    struct ServerTeleportSnapshot
    {
        Vector toolsOrigin;
        Vector toolsAngles;
        Vector origin;
        Vector velocity;
        Vector baseVelocity;
        Vector angles;
        Vector eyeAngles;
        int moveType = -1;
        int flags = 0;
        int groundEntity = -1;
        bool toolsPositionValid = false;
        bool originValid = false;
        bool velocityValid = false;
        bool baseVelocityValid = false;
        bool anglesValid = false;
        bool eyeAnglesValid = false;
        bool moveTypeValid = false;
        bool flagsValid = false;
        bool groundEntityValid = false;
    };

    ServerTeleportFieldCache g_serverTeleportFields;

    float VectorLength(const Vector& value)
    {
        return std::sqrt(value.LenghtSqr());
    }

    float Speed2D(const Vector& value)
    {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    const char* BoolText(bool value)
    {
        return value ? "true" : "false";
    }

    bool FindFirstField(
        datamap_t* dataMap,
        const char* primary,
        const char* fallback,
        L4D2::ServerDataMap::FieldInfo* field)
    {
        return L4D2::ServerDataMap::FindField(dataMap, primary, field)
            || (fallback
                && L4D2::ServerDataMap::FindField(dataMap, fallback, field));
    }

    void RefreshServerTeleportFields(CBaseEntity* entity)
    {
        if (!entity || g_serverTeleportFields.entity == entity)
            return;

        g_serverTeleportFields = {};
        g_serverTeleportFields.entity = entity;
        datamap_t* dataMap = L4D2::ServerDataMap::GetDataDescMap(entity);
        if (!dataMap)
            return;

        g_serverTeleportFields.originResolved = FindFirstField(
            dataMap, "m_vecOrigin", "m_vecAbsOrigin",
            &g_serverTeleportFields.origin);
        g_serverTeleportFields.velocityResolved = FindFirstField(
            dataMap, "m_vecVelocity", "m_vecAbsVelocity",
            &g_serverTeleportFields.velocity);
        g_serverTeleportFields.baseVelocityResolved = FindFirstField(
            dataMap, "m_vecBaseVelocity", nullptr,
            &g_serverTeleportFields.baseVelocity);
        g_serverTeleportFields.anglesResolved = FindFirstField(
            dataMap, "m_angRotation", "m_angAbsRotation",
            &g_serverTeleportFields.angles);
        g_serverTeleportFields.eyeAnglesResolved = FindFirstField(
            dataMap, "m_angEyeAngles", "v_angle",
            &g_serverTeleportFields.eyeAngles);
        g_serverTeleportFields.moveTypeResolved = FindFirstField(
            dataMap, "m_MoveType", nullptr,
            &g_serverTeleportFields.moveType);
        g_serverTeleportFields.flagsResolved = FindFirstField(
            dataMap, "m_fFlags", nullptr,
            &g_serverTeleportFields.flags);
        g_serverTeleportFields.groundEntityResolved = FindFirstField(
            dataMap, "m_hGroundEntity", nullptr,
            &g_serverTeleportFields.groundEntity);
    }

    ServerTeleportSnapshot CaptureServerTeleportSnapshot(
        CBaseEntity* entity,
        IClientEntity* clientPlayer)
    {
        ServerTeleportSnapshot result;
        if (!entity)
            return result;

        if (I::CServerTools)
        {
            QAngle toolsAngles;
            result.toolsPositionValid = I::CServerTools->GetPlayerPosition(
                result.toolsOrigin,
                toolsAngles,
                clientPlayer);
            result.toolsAngles = Vector(
                toolsAngles.x,
                toolsAngles.y,
                toolsAngles.z);
        }

        RefreshServerTeleportFields(entity);
        if (g_serverTeleportFields.entity != entity)
            return result;

        result.originValid = g_serverTeleportFields.originResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity, g_serverTeleportFields.origin, &result.origin);
        result.velocityValid = g_serverTeleportFields.velocityResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity, g_serverTeleportFields.velocity, &result.velocity);
        result.baseVelocityValid = g_serverTeleportFields.baseVelocityResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity, g_serverTeleportFields.baseVelocity, &result.baseVelocity);
        result.anglesValid = g_serverTeleportFields.anglesResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity, g_serverTeleportFields.angles, &result.angles);
        result.eyeAnglesValid = g_serverTeleportFields.eyeAnglesResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity, g_serverTeleportFields.eyeAngles, &result.eyeAngles);
        result.moveTypeValid = g_serverTeleportFields.moveTypeResolved
            && L4D2::ServerDataMap::TryReadFieldInt(
                entity, g_serverTeleportFields.moveType, &result.moveType);
        result.flagsValid = g_serverTeleportFields.flagsResolved
            && L4D2::ServerDataMap::TryReadFieldInt(
                entity, g_serverTeleportFields.flags, &result.flags);
        result.groundEntityValid = g_serverTeleportFields.groundEntityResolved
            && L4D2::ServerDataMap::TryReadFieldInt(
                entity, g_serverTeleportFields.groundEntity, &result.groundEntity);
        return result;
    }

    std::uintptr_t ModuleRelativeAddress(const char* moduleName, const void* address)
    {
        const HMODULE module = GetModuleHandleA(moduleName);
        if (!module || !address)
            return 0u;

        const std::uintptr_t absolute = reinterpret_cast<std::uintptr_t>(address);
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
        return absolute >= base ? absolute - base : 0u;
    }

    void LogServerTeleportSnapshot(
        const char* stage,
        std::uint32_t traversalEpoch,
        int commandNumber,
        CBaseEntity* entity,
        void* teleportTarget,
        const ServerTeleportSnapshot& snapshot,
        const Vector& expectedExitOrigin,
        const Vector& expectedExitVelocity,
        const Vector& expectedEntryOrigin,
        const Vector& expectedEntryVelocity)
    {
        const float exitOriginDelta = snapshot.originValid
            ? VectorLength(snapshot.origin - expectedExitOrigin)
            : -1.0f;
        const float entryOriginDelta = snapshot.originValid
            ? VectorLength(snapshot.origin - expectedEntryOrigin)
            : -1.0f;
        const float exitVelocityDelta = snapshot.velocityValid
            ? VectorLength(snapshot.velocity - expectedExitVelocity)
            : -1.0f;
        const float entryVelocityDelta = snapshot.velocityValid
            ? VectorLength(snapshot.velocity - expectedEntryVelocity)
            : -1.0f;
        const float toolsExitOriginDelta = snapshot.toolsPositionValid
            ? VectorLength(snapshot.toolsOrigin - expectedExitOrigin)
            : -1.0f;
        const float toolsEntryOriginDelta = snapshot.toolsPositionValid
            ? VectorLength(snapshot.toolsOrigin - expectedEntryOrigin)
            : -1.0f;
        const char* originSpace = !snapshot.originValid
            ? "Unavailable"
            : (exitOriginDelta <= entryOriginDelta ? "Exit" : "Entry");
        const char* velocitySpace = !snapshot.velocityValid
            ? "Unavailable"
            : (exitVelocityDelta <= entryVelocityDelta ? "Exit" : "Entry");
        const char* toolsOriginSpace = !snapshot.toolsPositionValid
            ? "Unavailable"
            : (toolsExitOriginDelta <= toolsEntryOriginDelta ? "Exit" : "Entry");

        U::LogInfo("[PortalServerTeleportTxn] epoch=%u cmd=%d stage=%s entity=%p slot=%u target=%p targetRva=0x%08X diagnosticMutation=false toolsPositionValid=%s toolsOrigin=(%.3f %.3f %.3f) toolsAngles=(%.3f %.3f %.3f) toolsOriginSpace=%s toolsExitOriginDelta=%.3f toolsEntryOriginDelta=%.3f originValid=%s originField=%s origin=(%.3f %.3f %.3f) originSpace=%s exitOriginDelta=%.3f entryOriginDelta=%.3f velocityValid=%s velocityField=%s requestedVelocity=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f) velocitySpace=%s exitVelocityDelta=%.3f entryVelocityDelta=%.3f speed2D=%.3f speed3D=%.3f velocityZ=%.3f baseVelocityValid=%s baseVelocity=(%.3f %.3f %.3f) anglesValid=%s angles=(%.3f %.3f %.3f) eyeAnglesValid=%s eyeAngles=(%.3f %.3f %.3f) moveTypeValid=%s moveType=%d flagsValid=%s flags=0x%X groundValid=%s ground=%d physicsSnapshot=unavailable.\n",
            traversalEpoch,
            commandNumber,
            stage ? stage : "unknown",
            entity,
            static_cast<unsigned int>(kServerTeleportVTableIndex),
            teleportTarget,
            static_cast<unsigned int>(ModuleRelativeAddress("server.dll", teleportTarget)),
            BoolText(snapshot.toolsPositionValid),
            snapshot.toolsOrigin.x, snapshot.toolsOrigin.y, snapshot.toolsOrigin.z,
            snapshot.toolsAngles.x, snapshot.toolsAngles.y, snapshot.toolsAngles.z,
            toolsOriginSpace,
            toolsExitOriginDelta,
            toolsEntryOriginDelta,
            BoolText(snapshot.originValid),
            g_serverTeleportFields.origin.fieldName ? g_serverTeleportFields.origin.fieldName : "unresolved",
            snapshot.origin.x, snapshot.origin.y, snapshot.origin.z,
            originSpace,
            exitOriginDelta,
            entryOriginDelta,
            BoolText(snapshot.velocityValid),
            g_serverTeleportFields.velocity.fieldName ? g_serverTeleportFields.velocity.fieldName : "unresolved",
            expectedExitVelocity.x,
            expectedExitVelocity.y,
            expectedExitVelocity.z,
            snapshot.velocity.x, snapshot.velocity.y, snapshot.velocity.z,
            velocitySpace,
            exitVelocityDelta,
            entryVelocityDelta,
            snapshot.velocityValid ? Speed2D(snapshot.velocity) : -1.0f,
            snapshot.velocityValid ? VectorLength(snapshot.velocity) : -1.0f,
            snapshot.velocityValid ? snapshot.velocity.z : 0.0f,
            BoolText(snapshot.baseVelocityValid),
            snapshot.baseVelocity.x, snapshot.baseVelocity.y, snapshot.baseVelocity.z,
            BoolText(snapshot.anglesValid),
            snapshot.angles.x, snapshot.angles.y, snapshot.angles.z,
            BoolText(snapshot.eyeAnglesValid),
            snapshot.eyeAngles.x, snapshot.eyeAngles.y, snapshot.eyeAngles.z,
            BoolText(snapshot.moveTypeValid),
            snapshot.moveType,
            BoolText(snapshot.flagsValid),
            snapshot.flags,
            BoolText(snapshot.groundEntityValid),
            snapshot.groundEntity);
    }
}

CBaseEntity* PortalPlayerTeleport::ResolveServerLocalPlayer(C_TerrorPlayer* clientPlayer)
{
    if (!clientPlayer || !I::EngineClient || !I::ClientEntityList)
        return nullptr;

    const int localIndex = I::EngineClient->GetLocalPlayer();
    if (localIndex <= 0)
        return nullptr;

    IClientEntity* clientLocal = I::ClientEntityList->GetClientEntity(localIndex);
    if (!clientLocal || clientLocal != clientPlayer)
        return nullptr;

    if (I::CServerTools)
    {
        IServerEntity* serverEntity = I::CServerTools->GetIServerEntity(clientLocal);
        CBaseEntity* baseEntity = serverEntity ? serverEntity->GetBaseEntity() : nullptr;
        if (baseEntity)
            return baseEntity;
    }

    CGlobalVars* globals = I::PlayerInfoManager ? I::PlayerInfoManager->GetGlobalVars() : nullptr;
    if (!globals || !globals->pEdicts)
        return nullptr;

    edict_t* localEdict = &globals->pEdicts[localIndex];
    IServerUnknown* unknown = localEdict ? localEdict->GetUnknown() : nullptr;
    return unknown ? unknown->GetBaseEntity() : nullptr;
}

bool PortalPlayerTeleport::Commit(
    C_TerrorPlayer* clientPlayer,
    const Vector& origin,
    const QAngle& angles,
    const Vector& velocity,
    const matrix3x4_t& entryToExit,
    std::uint32_t traversalEpoch,
    int commandNumber)
{
    if (!PortalPhysicsMode::ShouldCommitTeleport())
    {
        U::LogWarning("[PortalTeleport] rejected: physics mode=%s disables teleport.\n",
            PortalPhysicsMode::CurrentName());
        return false;
    }

    CBaseEntity* serverPlayer = PortalPlayerTeleport::ResolveServerLocalPlayer(clientPlayer);
    if (!serverPlayer)
    {
        U::LogWarning("[PortalTeleport] rejected: server local player could not be resolved.\n");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(serverPlayer);
    if (!vtable || !vtable[kServerTeleportVTableIndex])
    {
        U::LogError("[PortalTeleport] rejected: Teleport[%u] is unavailable serverPlayer=%p vtable=%p.\n",
            static_cast<unsigned int>(kServerTeleportVTableIndex), serverPlayer, vtable);
        return false;
    }

    using FnTeleport = void(__thiscall*)(void*, const Vector*, const QAngle*, const Vector*);
    const FnTeleport teleport = reinterpret_cast<FnTeleport>(vtable[kServerTeleportVTableIndex]);
    if (PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled())
    {
        matrix3x4_t exitToEntry;
        Vector expectedEntryOrigin;
        Vector expectedEntryVelocity;
        if (U::Math.MatrixInverse(entryToExit, exitToEntry))
        {
            expectedEntryOrigin =
                PortalTransform::TransformPoint(exitToEntry, origin);
            expectedEntryVelocity =
                PortalTransform::TransformVector(exitToEntry, velocity);
        }

        LogServerTeleportSnapshot(
            "pre",
            traversalEpoch,
            commandNumber,
            serverPlayer,
            reinterpret_cast<void*>(teleport),
            CaptureServerTeleportSnapshot(serverPlayer, clientPlayer),
            origin,
            velocity,
            expectedEntryOrigin,
            expectedEntryVelocity);
    }

    teleport(serverPlayer, &origin, &angles, &velocity);

    if (PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled())
    {
        matrix3x4_t exitToEntry;
        Vector expectedEntryOrigin;
        Vector expectedEntryVelocity;
        if (U::Math.MatrixInverse(entryToExit, exitToEntry))
        {
            expectedEntryOrigin =
                PortalTransform::TransformPoint(exitToEntry, origin);
            expectedEntryVelocity =
                PortalTransform::TransformVector(exitToEntry, velocity);
        }

        LogServerTeleportSnapshot(
            "post",
            traversalEpoch,
            commandNumber,
            serverPlayer,
            reinterpret_cast<void*>(teleport),
            CaptureServerTeleportSnapshot(serverPlayer, clientPlayer),
            origin,
            velocity,
            expectedEntryOrigin,
            expectedEntryVelocity);
    }
    return true;
}
