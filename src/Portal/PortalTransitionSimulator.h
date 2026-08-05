#pragma once

#include <cstdint>

#include "../SDK/SDK.h"
#include "PortalTransitionDecision.h"
#include "PortalTransform.h"

class C_TerrorPlayer;
class CUserCmd;
struct PortalInfo_t;

enum class PortalTransitionSide
{
    None,
    Blue,
    Orange,
};

struct PortalPlayerAnchor
{
    Vector origin;
    Vector eye;
    Vector center;
    Vector feet;
    Vector velocity;
    QAngle viewAngles;
};

struct PortalTransitionContext
{
    PortalTransitionPhase phase = PortalTransitionPhase::Idle;
    PortalTransitionSide entrySide = PortalTransitionSide::None;
    PortalTransitionSide exitSide = PortalTransitionSide::None;
    float enterTime = 0.0f;
    float lastUpdateTime = 0.0f;
    float signedDepth = 0.0f;
    float cooldownUntil = 0.0f;
    std::uint32_t traversalEpoch = 0;
    int lastCommandNumber = 0;
    int teleportCommandNumber = 0;
    bool insideAperture = false;
    bool movingIntoPortal = false;
    bool hasValidExitPlacement = false;
    bool hasSignedDepth = false;
};

struct PortalServerRunCommandObservation
{
    std::uint32_t transactionId = 0;
    int commandNumber = 0;
    int committedCommandNumber = 0;
    int commandDelta = 0;
    QAngle prePortalAngles;
    QAngle postPortalAngles;
    QAngle inputAngles;
    matrix3x4_t entryToExit = {};
    float differenceFromPrePortalAngles = 0.0f;
    float differenceFromPostPortalAngles = 0.0f;
    PortalServerCommandTiming timing = PortalServerCommandTiming::NoCommit;
    PortalCommandAngleSpace angleSpace = PortalCommandAngleSpace::Unknown;
    bool hasCommittedTeleport = false;
    bool hasEntryToExitMatrix = false;
    bool commandAngleFixPending = false;
};

struct PortalMovementCommandEndDiagnostic
{
    std::uint32_t transactionId = 0;
    int commandNumber = 0;
    PortalTransitionSide entrySide = PortalTransitionSide::None;
    PortalTransitionSide exitSide = PortalTransitionSide::None;
    bool projectedPath = false;
    bool wasOnGround = false;
    bool crossesInsideCommand = false;
    float commandInterval = 0.0f;
    float projectedLeadTime = 0.0f;
    float planeCrossingTime = -1.0f;
    float postPortalCommandTime = -1.0f;
    Vector originFromCenter;
    Vector observedEntryOrigin;
    Vector observedEntryCenter;
    Vector linearEntryCommandEndOrigin;
    Vector linearEntryCommandEndCenter;
    Vector committedExitOrigin;
    Vector officialExitCommandEndOrigin;
    matrix3x4_t entryToExit = {};
    bool hasActualClientCommandEnd = false;
    Vector actualEntryCommandEndOrigin;
    Vector actualEntryCommandEndVelocity;
    QAngle actualEntryCommandEndAngles;
    Vector actualExitCommandEndOrigin;
    Vector actualExitCommandEndVelocity;
    QAngle actualExitCommandEndAngles;
    Vector entryPlaneOrigin;
    Vector entryPlaneNormal;
    Vector exitPlaneOrigin;
    Vector exitPlaneNormal;
};

class CPortalTransitionSimulator
{
public:
    void Reset();
    void Update(CUserCmd* cmd);

    PortalTransitionPhase GetPhase() const { return m_context.phase; }
    const PortalTransitionContext& GetContext() const { return m_context; }
    bool IsLocalPlayerTransitioning() const;
    bool IsInCollisionBridgePhase() const;
    PortalTransitionSide GetCollisionBridgeSide() const;
    bool TryCommitMovementCrossing(int commandNumber, const Vector& movementOrigin, const Vector& movementVelocity, Vector* committedOrigin, Vector* committedVelocity);
    bool TryGetCommittedMovementForCommand(int commandNumber, Vector* committedOrigin, Vector* committedVelocity, QAngle* committedAngles = nullptr) const;
    bool TryGetCommittedMovementForDiagnostic(
        int commandNumber,
        Vector* committedOrigin,
        Vector* committedVelocity,
        QAngle* committedAngles,
        int* commandDelta,
        std::uint32_t* transactionId) const;
    bool TryGetMovementCommandEndDiagnostic(
        int commandNumber,
        PortalMovementCommandEndDiagnostic* result) const;
    bool TryGetMovementCommandEndDiagnosticForWindow(
        int commandNumber,
        PortalMovementCommandEndDiagnostic* result,
        int* commandDelta) const;
    bool RecordClientMovementCommandEndDiagnostic(
        int commandNumber,
        const Vector& entryOrigin,
        const Vector& entryVelocity,
        const QAngle& entryAngles,
        PortalMovementCommandEndDiagnostic* result);
    bool ObserveServerRunCommand(
        const CUserCmd* cmd,
        PortalServerRunCommandObservation* result) const;
    bool ConsumeServerPortalCommandAngleFix(int commandNumber);

private:
    struct PortalProbe
    {
        PortalTransitionSide side = PortalTransitionSide::None;
        PortalInfo_t* entry = nullptr;
        PortalInfo_t* exit = nullptr;
        float originDistance = 0.0f;
        float eyeDistance = 0.0f;
        float centerDistance = 0.0f;
        float feetDistance = 0.0f;
        float cmdDot = 0.0f;
        float velDot = 0.0f;
        float observedCenterDistance = 0.0f;
        float previousCenterDistance = 0.0f;
        float sweptPlaneFraction = 0.0f;
        bool originInside = false;
        bool eyeInside = false;
        bool centerInside = false;
        bool feetInside = false;
        bool insideAperture = false;
        bool hasPreviousSample = false;
        bool sweptThroughAperture = false;
        bool gmodWallBounds = false;
        float gmodFrontDistance = 0.0f;
        float feetLocalRight = 0.0f;
        float feetLocalUp = 0.0f;
        bool movingIntoPortal = false;
    };

    C_TerrorPlayer* GetLocalPlayer() const;
    bool ArePortalsReady() const;
    bool TryGetPortalPair(PortalTransitionSide entrySide, PortalInfo_t*& entry, PortalInfo_t*& exit) const;
    PortalPlayerAnchor BuildPlayerAnchor(C_TerrorPlayer* player) const;
    PortalProbe BuildProbe(CUserCmd* cmd, const PortalPlayerAnchor& anchor, PortalTransitionSide side, PortalInfo_t& entry, PortalInfo_t& exit) const;
    const PortalProbe* SelectBestProbe(const PortalProbe& blue, const PortalProbe& orange) const;

    void UpdatePhase(C_TerrorPlayer* player, CUserCmd* cmd, const PortalProbe* probe, float currentTime);
    void LogTraversalFrame(CUserCmd* cmd, C_TerrorPlayer* player, const PortalPlayerAnchor& anchor, const PortalProbe& blue, const PortalProbe& orange) const;
    void LogNearMiss(const PortalProbe& probe, float currentTime);
    void UpdateExitPhase(const PortalProbe& exitProbe, float currentTime);
    bool ShouldPredictPlaneCrossing(C_TerrorPlayer* player, const PortalProbe& probe) const;
    bool TryCommitPredictedPlaneCrossing(C_TerrorPlayer* player, CUserCmd* cmd, const PortalProbe& probe, float currentTime);
    bool TryCommitTeleport(C_TerrorPlayer* player, CUserCmd* cmd, const PortalProbe& probe, float currentTime, const PortalPlayerAnchor* overrideAnchor = nullptr, Vector* committedOrigin = nullptr, Vector* committedVelocity = nullptr, int commandNumberOverride = 0);
    void SetPhase(PortalTransitionPhase phase, const PortalProbe* probe, float currentTime, const char* reason);
    void ClearPhase(float currentTime, const char* reason);

    bool ShouldEnterApproach(const PortalProbe& probe) const;
    bool ShouldEnterIntersecting(const PortalProbe& probe) const;
    bool HasCrossingApertureEvidence(const PortalProbe& probe) const;
    bool ShouldTrackNearPortal(const PortalProbe& probe) const;
    bool ShouldStayInCurrentPhase(const PortalProbe* probe) const;
    bool ShouldLog(float currentTime, float& nextLogTime, float intervalSeconds) const;

    const char* PhaseName(PortalTransitionPhase phase) const;
    const char* SideName(PortalTransitionSide side) const;
    const char* BoolText(bool value) const;

    PortalTransitionContext m_context;
    float m_nextReadinessLogTime = 0.0f;
    float m_nextProbeLogTime = 0.0f;
    float m_nextPhaseLogTime = 0.0f;
    float m_nextNearMissLogTime = 0.0f;
    std::uint32_t m_nextTraversalEpoch = 0;
    int m_lastCommittedMovementCommandNumber = 0;
    Vector m_lastCommittedMovementOrigin;
    Vector m_lastCommittedMovementVelocity;
    QAngle m_lastCommittedMovementAngles;
    bool m_hasLastCommittedMovement = false;
    QAngle m_lastPrePortalViewAngles;
    matrix3x4_t m_lastCommittedEntryToExit = {};
    bool m_hasLastCommittedEntryToExit = false;
    bool m_serverPortalCommandAngleFixPending = false;
    std::uint32_t m_lastCommittedTransactionId = 0;
    PortalMovementCommandEndDiagnostic m_lastMovementCommandEndDiagnostic;
    bool m_hasLastMovementCommandEndDiagnostic = false;
    PortalPlayerAnchor m_previousAnchor;
    Vector m_previousBlueOrigin;
    Vector m_previousBlueNormal;
    Vector m_previousOrangeOrigin;
    Vector m_previousOrangeNormal;
    int m_previousAnchorCommandNumber = 0;
    bool m_hasPreviousAnchor = false;
};
