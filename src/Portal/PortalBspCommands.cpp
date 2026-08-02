#include "PortalBspPhase1.h"
#include "PortalNearViewRenderPolicy.h"
#include "PortalRenderFixRenderer.h"
#include "PortalTransitionDecision.h"
#include "L4D2_Portal.h"

#include "../SDK/L4D2/Includes/convar.h"
#include "../Util/Logger/Logger.h"

#include <cstdlib>

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

CON_COMMAND(portal_visual_shadowpredictor, "Toggle render-time eye extrapolation experiment: portal_visual_shadowpredictor <0|1>")
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

CON_COMMAND(portal_visual_transactiondiag, "Toggle commit-through-plus-3 portal prediction diagnostics: portal_visual_transactiondiag <0|1>")
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
    U::LogInfo("[PortalBsp][Command] command=portal_visual_transactiondiag result=true transactionDiagnostics=%s predictionWindow=commit-plus-3 behaviorMutation=false.\n",
        enabled ? "true" : "false");
    PortalBspPhase1::LogStatus("console-transactiondiag-change");
}

CON_COMMAND(portal_prediction_commit_sync, "Toggle one-shot committed origin/velocity synchronization at client SetupMove: portal_prediction_commit_sync <0|1>")
{
    if (args.ArgC() < 2)
    {
        U::LogInfo("[PortalBsp][Command] command=portal_prediction_commit_sync current=%s usage='portal_prediction_commit_sync <0|1>'.\n",
            PortalTransitionDecision::GetPredictionCommitMovementSyncEnabled() ? "true" : "false");
        PortalBspPhase1::LogStatus("console-prediction-commit-sync-query");
        return;
    }

    const bool enabled = std::atoi(args.Arg(1)) != 0;
    PortalTransitionDecision::SetPredictionCommitMovementSyncEnabled(enabled);
    U::LogInfo("[PortalBsp][Command] command=portal_prediction_commit_sync result=true predictionCommitSync=%s scope=exact-commit-command setupBoundary=post-original behaviorMutation=%s.\n",
        enabled ? "true" : "false",
        enabled ? "one-shot" : "false");
    PortalBspPhase1::LogStatus("console-prediction-commit-sync-change");
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
