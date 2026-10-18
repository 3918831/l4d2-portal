#include "PortalBspPhase1.h"
#include "PortalNearViewRenderPolicy.h"
#include "PortalRenderFixRenderer.h"
#include "PortalTransitionDecision.h"
#include "PortalPlacementFloorSnap.h"
#include "PortalNativeDiagnostics.h"
#include "PortalFallWrite.h"
#include "L4D2_Portal.h"
#include "../Hooks/Input/InputAngles.h"

#include "../SDK/L4D2/Includes/convar.h"
#include "../Util/Logger/Logger.h"

#include <cstdlib>

CON_COMMAND(portal_diag_native, "Opt-in detailed native prediction/fall diagnostics: portal_diag_native <0|1>")
{
    if(args.ArgC()<2)PortalNativeDiagnostics::Report();
    else PortalNativeDiagnostics::SetEnabled(std::atoi(args.Arg(1))!=0);
}

CON_COMMAND(portal_diag_nativefall, "Read-only capture of the next ordinary descending airborne interval (requires portal_diag_native 1)")
{
    PortalNativeDiagnostics::ArmNormalFall();
}

CON_COMMAND(portal_server_fallfix, "Single-factor official signed fall update after portal commit: portal_server_fallfix <0|1>")
{
    if(args.ArgC()<2)PortalFallWrite::Report();
    else PortalFallWrite::SetEnabled(std::atoi(args.Arg(1))!=0);
}

CON_COMMAND(portal_bsp_phase1, "Enable unrestricted Phase 1 BSP carving: portal_bsp_phase1 <0|1>")
{
    if (args.ArgC() < 2)
    {
        PortalBspPhase1::LogStatus("console-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    const bool result = PortalBspPhase1::SetEnabled(
        enabled,
        enabled ? "console-enable" : "console-disable");
    U::LogInfo("[PortalBsp][Command] command=portal_bsp_phase1 requested=%s result=%s.\n",
        enabled ? "true" : "false",
        result ? "true" : "false");
    PortalBspPhase1::LogStatus("console-phase1");
}

CON_COMMAND(portal_bsp_status, "Print Phase 1 BSP carving status")
{
    PortalBspPhase1::LogStatus("console-status");
    PortalFallWrite::Report();
    PortalNativeDiagnostics::Report();
    U::PortalFileLog::WriteAlwaysFormat(
        "[PortalFileLog] flush reason=portal_bsp_status build=%s modulePath=%s.\n",
        U::PortalFileLog::GetBuildId(),
        U::PortalFileLog::GetLoadedModulePath());
    U::PortalFileLog::Flush();
}

CON_COMMAND(portal_log_flush, "Flush buffered portal diagnostics and print the loaded build identity")
{
    U::LogInfo(
        "[PortalFileLog] flush reason=console-command build=%s modulePath=%s.\n",
        U::PortalFileLog::GetBuildId(),
        U::PortalFileLog::GetLoadedModulePath());
    U::PortalFileLog::Flush();
}

CON_COMMAND(portal_placement_floorsnap, "Toggle provisional Portal SDK near-floor portal-bottom matching: portal_placement_floorsnap <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_placement_floorsnap current=%s halfHeight=%.3f halfHeightSource=portal-sdk-2013-provisional maxDistance=%.3f usage='portal_placement_floorsnap <0|1>'.\n",
            PortalPlacementFloorSnap::GetEnabled() ? "true" : "false",
            PortalPlacementFloorSnap::kPortalSdk2013ProvisionalHalfHeight,
            PortalPlacementFloorSnap::kMaximumSnapDistance);
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalPlacementFloorSnap::SetEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_placement_floorsnap result=true enabled=%s halfHeight=%.3f halfHeightSource=portal-sdk-2013-provisional maxDistance=%.3f scope=portal-placement-only playerMovementMutation=false.\n",
        enabled ? "true" : "false",
        PortalPlacementFloorSnap::kPortalSdk2013ProvisionalHalfHeight,
        PortalPlacementFloorSnap::kMaximumSnapDistance);
    PortalBspPhase1::LogStatus("console-placement-floorsnap-change");
}

CON_COMMAND(portal_bsp_clearance, "Select exit clearance: portal_bsp_clearance <full|plane|exact>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_bsp_clearance current=%s usage='portal_bsp_clearance <full|plane|exact>'.\n",
            PortalTransitionDecision::ExitClearanceModeName(
                PortalTransitionDecision::GetExitClearanceMode()));
        PortalBspPhase1::LogStatus("console-clearance-query");
        return;
    }

    PortalExitClearanceMode mode = PortalExitClearanceMode::FullHull;
    if (!PortalTransitionDecision::TryParseExitClearanceMode(args.Arg(1), &mode))
    {
        U::LogError("[PortalBsp][Command] command=portal_bsp_clearance requested=%s result=false expected=full|plane|exact.\n",
            args.Arg(1));
        return;
    }

    PortalTransitionDecision::SetExitClearanceMode(mode);
    U::LogInfo("[PortalBsp][Command] command=portal_bsp_clearance result=true clearanceMode=%s.\n",
        PortalTransitionDecision::ExitClearanceModeName(mode));
    PortalBspPhase1::LogStatus("console-clearance-change");
}

CON_COMMAND(portal_visual_nearclip, "Toggle portal-aware main-view near clip: portal_visual_nearclip <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_nearclip current=%s usage='portal_visual_nearclip <0|1>'.\n",
            PortalTransitionDecision::GetPortalNearClipFixEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-nearclip-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalNearClipFixEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_nearclip result=true nearClipFix=%s.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-nearclip-change");
}

CON_COMMAND(portal_visual_reorientation,
    "Toggle official full-roll preservation and render-frame reorientation: portal_visual_reorientation <0|1>")
{
    if (args.ArgC() < 2)
    {
        Hooks::InputAngles::LogLateDiagnosticProbe();
        const bool signatureValidated = Hooks::InputAngles::IsSignatureValidated();
        const bool abiValidated = Hooks::InputAngles::IsAbiValidated();
        const bool active = G::G_L4D2Portal.m_PortalTransition.IsPortalViewReorientationActive();
        const std::uint32_t activeTransactionId = active
            ? G::G_L4D2Portal.m_PortalTransition.GetPortalViewReorientationTransactionId()
            : 0;
        U::LogInfo("[PortalBsp][Command] command=portal_visual_reorientation enabled=%s hookValidated=%s signatureValidated=%s abiValidated=%s activeTxn=%u active=%s usage='portal_visual_reorientation <0|1>' curve=%s timeScale=%.3f.\n",
            PortalTransitionDecision::GetPortalViewReorientationEnabled() ? "true" : "false",
            signatureValidated && abiValidated ? "true" : "false",
            signatureValidated ? "true" : "false",
            abiValidated ? "true" : "false",
            activeTransactionId,
            active ? "true" : "false",
            PortalViewReorientation::GetCurveName(),
            PortalViewReorientation::GetTimeScale());
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    if (PortalTransitionDecision::GetPortalViewReorientationEnabled() != enabled)
        G::G_L4D2Portal.m_PortalTransition.ResetPortalViewReorientation("console-view-reorientation-ab-switch");
    PortalTransitionDecision::SetPortalViewReorientationEnabled(enabled);

    const bool signatureValidated = Hooks::InputAngles::IsSignatureValidated();
    const bool abiValidated = Hooks::InputAngles::IsAbiValidated();
    const bool active = G::G_L4D2Portal.m_PortalTransition.IsPortalViewReorientationActive();
    const std::uint32_t activeTransactionId = active
        ? G::G_L4D2Portal.m_PortalTransition.GetPortalViewReorientationTransactionId()
        : 0;
    U::LogInfo("[PortalBsp][Command] command=portal_visual_reorientation result=true enabled=%s hookValidated=%s signatureValidated=%s abiValidated=%s activeTxn=%u active=%s scope=view-angles-only physicsMutation=false curve=%s timeScale=%.3f.\n",
        PortalTransitionDecision::GetPortalViewReorientationEnabled() ? "true" : "false",
        signatureValidated && abiValidated ? "true" : "false",
        signatureValidated ? "true" : "false",
        abiValidated ? "true" : "false",
        activeTransactionId,
        active ? "true" : "false",
        PortalViewReorientation::GetCurveName(),
        PortalViewReorientation::GetTimeScale());
}

CON_COMMAND(portal_visual_stencilproxy_guard, "Toggle extreme-near stencil-proxy guard: portal_visual_stencilproxy_guard <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_stencilproxy_guard current=%s usage='portal_visual_stencilproxy_guard <0|1>'.\n",
            PortalRenderFixRenderer::GetExtremeNearGuardEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalRenderFixRenderer::SetExtremeNearGuardEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_stencilproxy_guard result=true extremeNearGuard=%s.\n",
        enabled ? "true" : "false");
}

CON_COMMAND(portal_visual_viewmodel_depthclear, "Toggle official-style near-portal depth clear before viewmodel: portal_visual_viewmodel_depthclear <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_viewmodel_depthclear current=%s usage='portal_visual_viewmodel_depthclear <0|1>'.\n",
            PortalNearViewRenderPolicy::GetViewmodelDepthClearEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalNearViewRenderPolicy::SetViewmodelDepthClearEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_viewmodel_depthclear result=true enabled=%s.\n",
        enabled ? "true" : "false");
}

CON_COMMAND(portal_visual_nearframe_guard, "Toggle suppression of the legacy whole-portal frame redraw inside zNear: portal_visual_nearframe_guard <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_nearframe_guard current=%s usage='portal_visual_nearframe_guard <0|1>'.\n",
            PortalNearViewRenderPolicy::GetNearFrameGuardEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalNearViewRenderPolicy::SetNearFrameGuardEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_nearframe_guard result=true enabled=%s.\n",
        enabled ? "true" : "false");
}

CON_COMMAND(portal_visual_exitguard, "Toggle ExactTransform exit visibility/near-clip guard: portal_visual_exitguard <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_exitguard current=%s usage='portal_visual_exitguard <0|1>'.\n",
            PortalTransitionDecision::GetExactExitVisualGuardEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-exitguard-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetExactExitVisualGuardEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_exitguard result=true exactExitVisualGuard=%s.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-exitguard-change");
}

CON_COMMAND(portal_visual_entryhandoff, "Toggle official-style pre-Teleport entry camera handoff: portal_visual_entryhandoff <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_entryhandoff current=%s usage='portal_visual_entryhandoff <0|1>'.\n",
            PortalTransitionDecision::GetEntryCameraHandoffEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-entryhandoff-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetEntryCameraHandoffEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_entryhandoff result=true entryCameraHandoff=%s.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-entryhandoff-change");
}

CON_COMMAND(portal_visual_eyehistory, "Toggle official-style post-Teleport eye-history continuity: portal_visual_eyehistory <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_eyehistory current=%s usage='portal_visual_eyehistory <0|1>'.\n",
            PortalTransitionDecision::GetPortalEyeContinuityEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalEyeContinuityEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_eyehistory result=true eyeHistoryContinuity=%s.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-eyehistory-change");
}

CON_COMMAND(portal_visual_shadowpredictor, "Toggle the validated render-time eye extrapolation baseline: portal_visual_shadowpredictor <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_shadowpredictor current=%s usage='portal_visual_shadowpredictor <0|1>'.\n",
            PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalEyeShadowPredictorEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_shadowpredictor result=true shadowPredictor=%s scope=render-eye-only horizon=one-runtime-tick physicsMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-shadowpredictor-change");
}

CON_COMMAND(portal_visual_crossleafhandoff, "Toggle geometry-clear cross-leaf render-eye handoff: portal_visual_crossleafhandoff <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_crossleafhandoff current=%s usage='portal_visual_crossleafhandoff <0|1>'.\n",
            PortalTransitionDecision::GetPortalEyeCrossLeafHandoffEnabled()
                ? "true"
                : "false");
        PortalBspPhase1::LogStatus("console-cross-leaf-handoff-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalEyeCrossLeafHandoffEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_crossleafhandoff result=true enabled=%s scope=render-eye-only geometryContract=bidirectional-world-props-clear physicsMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-cross-leaf-handoff-change");
}

CON_COMMAND(portal_visual_commitseed, "Toggle the official command-end seed for the first post-commit render eye: portal_visual_commitseed <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_commitseed current=%s usage='portal_visual_commitseed <0|1>' scope=first-render-eye-only physicsMutation=false.\n",
            PortalTransitionDecision::GetPortalVisualCommitSeedEnabled()
                ? "true"
                : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    G::G_L4D2Portal.m_PortalTransition.SetPortalVisualCommitSeedEnabled(
        enabled,
        "console-commit-seed-ab-edge");
    U::LogInfo("[PortalBsp][Command] command=portal_visual_commitseed result=true enabled=%s scope=first-render-eye-only physicsMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-commit-seed-change");
}

CON_COMMAND(portal_visual_shadowtail, "Toggle one-tick shadow-predictor release handoff: portal_visual_shadowtail <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_shadowtail current=%s usage='portal_visual_shadowtail <0|1>'.\n",
            PortalTransitionDecision::GetPortalEyeShadowReleaseTailEnabled() ? "true" : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalEyeShadowReleaseTailEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_shadowtail result=true shadowReleaseTail=%s scope=render-eye-only horizon=one-runtime-tick physicsMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-shadowtail-change");
}

CON_COMMAND(portal_visual_singleeyehistory, "Toggle single-owner portal eye history: portal_visual_singleeyehistory <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_singleeyehistory current=%s activeOwner=%s usage='portal_visual_singleeyehistory <0|1>' scope=render-eye-history-only physicsMutation=false.\n",
            PortalTransitionDecision::GetPortalEyeSingleOwnerEnabled()
                ? "true" : "false",
            PortalTransitionDecision::GetPortalEyeSingleOwnerEnabled()
                ? "state-only" : "state-plus-shadow");
        PortalBspPhase1::LogStatus("console-single-eye-history-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    G::G_L4D2Portal.m_PortalTransition.SetPortalEyeSingleOwnerEnabled(
        enabled,
        "console-single-eye-history-ab-edge");
    U::LogInfo("[PortalBsp][Command] command=portal_visual_singleeyehistory result=true enabled=%s activeOwner=%s scope=render-eye-history-only physicsMutation=false.\n",
        enabled ? "true" : "false",
        enabled ? "state-only" : "state-plus-shadow");
    PortalBspPhase1::LogStatus("console-single-eye-history-change");
}

CON_COMMAND(portal_visual_projectedhistoryrebase, "Toggle projected-commit pre-transform eye-history rebase: portal_visual_projectedhistoryrebase <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_projectedhistoryrebase current=%s usage='portal_visual_projectedhistoryrebase <0|1>'.\n",
            PortalTransitionDecision::GetProjectedEyeHistoryRebaseEnabled()
                ? "true"
                : "false");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetProjectedEyeHistoryRebaseEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_projectedhistoryrebase result=true enabled=%s scope=projected-commit-render-eye-history-only physicsMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-projected-history-rebase-change");
}

CON_COMMAND(portal_server_command_anglefix, "Toggle official-style one-shot server command angle transform: portal_server_command_anglefix <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_server_command_anglefix current=%s usage='portal_server_command_anglefix <0|1>'.\n",
            PortalTransitionDecision::GetServerPortalCommandAngleFixEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-server-command-anglefix-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetServerPortalCommandAngleFixEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_server_command_anglefix result=true serverCommandAngleFix=%s.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-server-command-anglefix-change");
}

CON_COMMAND(portal_visual_transactiondiag, "Toggle commit-through-plus-1 portal prediction diagnostics: portal_visual_transactiondiag <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_visual_transactiondiag current=%s usage='portal_visual_transactiondiag <0|1>'.\n",
            PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-transactiondiag-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPortalTransactionDiagnosticsEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_visual_transactiondiag result=true transactionDiagnostics=%s predictionWindow=commit-plus-1 floorWallDiagnosticWindow=commit-through-plus-32 floorWallDiagnosticScope=floor-entry-to-wall-exit-only behaviorMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-transactiondiag-change");
}

CON_COMMAND(portal_ground_livecommit, "Toggle the validated ground-only observed crossing baseline (0 keeps the projected rollback control): portal_ground_livecommit <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_ground_livecommit current=%s usage='portal_ground_livecommit <0|1>'.\n",
            PortalTransitionDecision::GetGroundLiveCommitEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-ground-live-commit-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetGroundLiveCommitEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_ground_livecommit result=true enabled=%s scope=ground-only commitMode=%s airbornePath=unchanged physicsMutation=teleport-timing-only.\n",
        enabled ? "true" : "false",
        enabled ? "observed-live-baseline" : "projected-rollback-control");
    PortalBspPhase1::LogStatus("console-ground-live-commit-change");
}

CON_COMMAND(portal_visual_mark, "Mark a suspected visual flash and dump the most recent portal transaction: portal_visual_mark [reason]")
{
    const char* reason =
        args.ArgC() >= 2 ? args.Arg(1) : "manual-visual-marker";
    const std::uint32_t transactionId =
        G::G_L4D2Portal.m_PortalVisualContinuityDiagnostics.MarkUserEvent(reason);
    U::LogInfo(
        "[PortalBsp][Command] command=portal_visual_mark result=true reason=%s txn=%u.\n",
        reason,
        transactionId);
}

CON_COMMAND(portal_bsp_restore, "Disable Phase 1 and restore every modified BSP brush")
{
    const bool result = PortalBspPhase1::SetEnabled(false, "console-emergency-restore");
    U::LogInfo("[PortalBsp][Command] command=portal_bsp_restore result=%s.\n",
        result ? "true" : "false");
    PortalBspPhase1::LogStatus("console-restore");
}
