#pragma once
#include <cstdint>
#include "PortalNativeResetPolicy.h"
#include "PortalNativePredictionErrorPolicy.h"
#include "PortalNativeFieldPolicy.h"
#include "PortalNativeFallPolicy.h"
class C_BasePlayer;
class CUserCmd;
class CMoveData;
class Vector;
class CViewSetup;
namespace PortalNativePart1Policy { struct Observation; }

// Observation only. None of these entry points may write game/player state.
namespace PortalNativeDiagnostics
{
    void SetEnabled(bool enabled);
    void Report();
    void Reset(const char* reason);
    void ObserveInput(const CUserCmd* cmd);
    int LatestInputCommand();
    void BeginPortal(std::uint32_t transaction, int command, bool floorWall);
    void ArmNormalFall();
    void RenderStart(bool beforeOriginal);
    int InterpolationBegin(C_BasePlayer* player, float time, Vector& before);
    void InterpolationEnd(C_BasePlayer* player, float time, const Vector& before, bool result, int frame);
    bool Part1Begin(C_BasePlayer* player, int frame, PortalNativePart1Policy::Observation& sample);
    void Part1End(C_BasePlayer* player, int frame, PortalNativePart1Policy::Observation& sample);
    void CalcView(C_BasePlayer* player, const Vector& eye, bool beforeAdapter);
    void FinalView(const CViewSetup& view);
    void EyeState(bool valid, bool active, bool released, bool shadow);
    bool AllowFloorWallCommand(std::uint32_t transaction, bool server);
    void Movement(const char* stage, C_BasePlayer* player, CUserCmd* cmd, CMoveData* move);
    PortalNativeResetPolicy::Scope EnterPredictionScope(C_BasePlayer* player,const CUserCmd* cmd,PortalNativeResetPolicy::Phase phase);
    void LeavePredictionScope(const PortalNativeResetPolicy::Scope& previous);
    C_BasePlayer* OriginResetPlayer();
    bool OriginResetBegin(C_BasePlayer* player,PortalNativeResetPolicy::Observation& sample);
    void OriginResetEnd(C_BasePlayer* player,PortalNativeResetPolicy::Observation& sample);
    bool PredictionErrorBegin(const PortalNativePredictionErrorPolicy::PacketScope& packet,void* entity,int ack,PortalNativePredictionErrorPolicy::Observation& sample);
    std::uint64_t PredictionErrorEpoch();
    void PredictionFieldMessage(const PortalNativePredictionErrorPolicy::Observation& owner,const PortalNativeFieldPolicy::Message& message);
    void PredictionErrorEnd(PortalNativePredictionErrorPolicy::Observation& sample);
    std::uint32_t FallCapture();
    void FallMovement(bool server,bool fullWalk,void* gameMovement,const CUserCmd* cmd);
    void FallState(unsigned phase,const PortalNativeFallPolicy::Sample& sample,const CUserCmd* cmd,std::uint32_t capture);
}
