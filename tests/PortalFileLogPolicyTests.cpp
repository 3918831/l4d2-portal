#include <cstdlib>
#include <iostream>

#include "../src/Util/Logger/PortalFileLog.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using U::PortalFileLog::ShouldCapture;
    using U::PortalFileLog::ShouldSuppressConsole;
    using namespace U::PortalFileLog;
    Expect(!ShouldCollectNativeDiagnostics(true,true), "heavy probes must not run by default");
    Expect(!ShouldCapture("[PortalNativeFallState] phase=server-pre."), "compact mode omits high-rate detail");
    Expect(ShouldSuppressConsole("[PortalNativeFallState] phase=server-pre."), "omitted file detail must not leak to console");
    Expect(ShouldCapture("[PortalFallWriteSummary] applied=10."), "compact mode retains functional evidence");
    Expect(ShouldCapture("[PortalBsp][Binding] resolved=false."), "compact mode retains placement failures");
    Expect(!ShouldEmitFallWriteDetail(0,false), "successful writes do not emit detail in compact mode");
    Expect(ShouldEmitFallWriteDetail(99,true), "write rejection bypasses detail budgets and compact mode");
    SetNativeDiagnosticsEnabled(true);
    Expect(ShouldCollectNativeDiagnostics(true,true), "explicit opt-in runs native probes");
    Expect(!ShouldCollectNativeDiagnostics(false,true)&&!ShouldCollectNativeDiagnostics(true,false), "native capture still requires transaction diagnostics and a clock");
    Expect(ShouldEmitFallWriteDetail(7,false)&&!ShouldEmitFallWriteDetail(8,false), "verbose mode preserves the eight-row success budget");
    Expect(ShouldCapture("[PortalFallWriteHook] ready=true."), "fall write validation must be captured");
    Expect(ShouldCapture("[PortalFallWriteBind] ready=true."), "fall write binding must be captured");
    Expect(ShouldSuppressConsole("[PortalFallWrite] applied=true."), "fall write details must be file-only");
    Expect(ShouldCapture("[PortalFallWriteSummary] applied=10."), "fall write totals must be captured");
    Expect(!ShouldCapture("[PortalFallWriteNoise] applied=true."), "fall write routes must be exact");
    Expect(ShouldCapture("[PortalNativeFallStateProbe] valid=true."), "fall field binding proof must be captured");
    Expect(ShouldSuppressConsole("[PortalNativeFallState] phase=server-pre."), "fall observations must be file-only");
    Expect(ShouldCapture("[PortalNativeFallStateSummary] calls=400."), "fall all-call counters must be captured");
    Expect(!ShouldCapture("[PortalNativeFallStateExtra] value=1."), "unrecognized fall neighbor must not route");
    Expect(ShouldCapture("[PortalNativePredictionFieldHook] installed=true."), "native formatted-field hook proof must be captured");
    Expect(ShouldSuppressConsole("[PortalNativePredictionField] field=m_vecVelocity."), "native field rows must be file-only");
    Expect(ShouldCapture("[PortalNativePredictionFieldSummary] messages=400."), "all native message counters must be captured");
    Expect(ShouldCapture("[PortalNativePredictionFieldAggregate] occurrences=100."), "native field aggregates must be captured");
    Expect(!ShouldCapture("[PortalNativePredictionFieldExtra] field=x."), "neighbor tag must not route to native fields");
    Expect(ShouldCapture("[PortalNativePredictionErrorHook] installed=true."), "prediction comparison hook validation must be captured");
    Expect(ShouldSuppressConsole("[PortalNativePredictionError] errors=1."), "comparison rows must remain file-only");
    Expect(ShouldCapture("[PortalNativePredictionErrorSummary] calls=400."), "all comparison counters must be captured");
    Expect(ShouldCapture("[PortalNativePredictionErrorEntity] errors=25."), "entity aggregates must be captured");
    Expect(!ShouldCapture("[PortalNativePredictionErrorEntityExtra] errors=25."), "unknown neighboring tag must not become a route");
    Expect(ShouldCapture("[PortalNativeResetLatchedHook] installed=true."), "parent hook validation must be captured");
    Expect(ShouldSuppressConsole("[PortalNativeResetLatchedHook] installed=true."), "parent hook validation must be file-only");
    Expect(ShouldCapture("[PortalNativeOriginResetParentCaller] calls=87."), "upstream caller counters must be captured");
    Expect(!ShouldCapture("[PortalNativeOriginResetParentCallerExtra] calls=87."), "nearby unknown parent tag is not an exact route");

    Expect(!ShouldSuppressConsole("[PortalPhysicsMode] active=BspPhase1Causal."),
        "physics-mode summaries must remain visible in the console");
    Expect(!ShouldSuppressConsole("[PortalBsp][Command] command=portal_bsp_phase1."),
        "BSP command acknowledgements must remain visible in the console");
    Expect(!ShouldSuppressConsole("[PortalBsp][Phase1Status] enabled=true."),
        "BSP status output must remain visible in the console");
    Expect(!ShouldSuppressConsole("ordinary initialization message"),
        "unrelated non-diagnostic messages must preserve their existing console behavior");

    Expect(ShouldSuppressConsole("[PortalHookProbe] domain=client."),
        "per-move hook probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalMoveTypeProbe] phase=walk."),
        "move-type probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalBounds] phase=Idle."),
        "portal-bound probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalTeleportCommit] committed entry=Blue."),
        "teleport commit details must be file-only");
    Expect(ShouldSuppressConsole("[PortalPredictionObservation] syncApplied=false."),
        "prediction boundary observations must be file-only");
	Expect(ShouldSuppressConsole("[PortalPredictionAngleSync] angleSyncApplied=true."),
		"prediction angle synchronization diagnostics must be file-only");
	Expect(ShouldSuppressConsole("[PortalMovementCommandTiming] txn=1 path=projected."),
		"movement command timing diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalRenderCadenceFrame] txn=1 frame=100."),
        "render-cadence frames must be file-only");
    Expect(ShouldSuppressConsole("[PortalPredictionFinish] phase=finishmove-post-original."),
        "post-FinishMove prediction observations must be file-only");
    Expect(ShouldSuppressConsole("[PortalTxnBoundary] stage=processmovement-post-original."),
        "transaction prediction boundaries must be file-only");
    Expect(ShouldSuppressConsole("[PortalServerTeleportTxn] stage=post."),
        "server teleport transaction snapshots must be file-only");
    Expect(ShouldSuppressConsole("[PortalServerRunCommandHook] installed=true index=454."),
        "server PlayerRunCommand hook validation must be file-only");
    Expect(ShouldSuppressConsole("[PortalServerRunCommand] stage=pre mutation=false."),
        "server PlayerRunCommand diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeResidualBaseline] phase=Idle."),
        "non-portal eye residual baselines must be file-only");
    Expect(ShouldSuppressConsole("[PortalBsp][Binding] owner=blue."),
        "detailed BSP binding diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEnvironment] phase=main-render."),
        "render environment diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalVisualPlaneProbe] side=Blue."),
        "near-plane visual probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalNearClipFix] entry=Blue."),
        "near-clip fix diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEntryViewHandoff] entry=Blue exit=Orange."),
        "pre-Teleport entry camera handoff diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalVisualTxn] txn=1 stage=main-view-pre-render."),
        "transaction-level visual continuity diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalOfficialRemoteView] entry=Blue exit=Orange."),
        "official remote-view diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalRenderFix] applied=true portal=Blue."),
        "near-plane proxy diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalExitVisibility] exit=Orange."),
        "exit visibility diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalContinuity] entry=Blue exit=Orange."),
        "teleport continuity diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeHistoryPending] txn=1 published=true."),
        "portal eye-history publication diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeHistory] txn=1 consumed=true."),
        "portal eye-history update diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeShadowPredictor] txn=1 applied=true."),
        "portal eye shadow-predictor diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeSingleOwner] event=consume txn=1 physicsMutation=false."),
        "single-owner eye-history diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeLeafHandoffProbe] txn=1 reason=candidate-leaf-mismatch."),
        "the exact runtime delayed leaf-handoff probe tag must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeLeafHandoffDecision] txn=1 decision=allow-safe-cross-leaf."),
        "safe cross-leaf authorization decisions must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeReleaseTailProbe] txn=1 reason=horizon-expired."),
        "release-tail horizon probes must be file-only");
    Expect(ShouldSuppressConsole("[PortalEyeSafety] txn=1 point=applied."),
        "portal eye safety diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalPredictionCommandWindow] txn=1 commandDelta=2."),
        "prediction command-window diagnostics must be file-only");
    Expect(ShouldSuppressConsole("[PortalVisualMarker] reason=flash txn=1."),
        "manual visual markers must be file-only");

    Expect(ShouldCapture("[PortalHookProbe] domain=client."),
        "file-only traversal probes must still be captured");
    Expect(ShouldCapture("[PortalBsp][Mutation] event=write."),
        "file-only BSP diagnostics must still be captured");
    Expect(ShouldCapture("[PortalVisualPlaneProbe] side=Blue."),
        "near-plane visual probes must be captured");
    Expect(ShouldCapture("[PortalNearClipFix] entry=Blue."),
        "near-clip fix diagnostics must be captured");
    Expect(ShouldCapture("[PortalEntryViewHandoff] entry=Blue exit=Orange."),
        "pre-Teleport entry camera handoff diagnostics must be captured");
    Expect(ShouldCapture("[PortalVisualTxn] txn=1 stage=main-view-pre-render."),
        "transaction-level visual continuity diagnostics must be captured");
    Expect(ShouldCapture("[PortalOfficialRemoteView] entry=Blue exit=Orange."),
        "official remote-view diagnostics must be captured");
    Expect(ShouldCapture("[PortalRenderFix] applied=true portal=Blue."),
        "near-plane proxy diagnostics must be captured");
    Expect(ShouldCapture("[PortalExitVisibility] exit=Orange."),
        "exit visibility diagnostics must be captured");
    Expect(ShouldCapture("[PortalContinuity] entry=Blue exit=Orange."),
        "teleport continuity diagnostics must be captured");
    Expect(ShouldCapture("[PortalEyeHistoryPending] txn=1 published=true."),
        "portal eye-history publication diagnostics must be captured");
    Expect(ShouldCapture("[PortalEyeHistory] txn=1 consumed=true."),
        "portal eye-history update diagnostics must be captured");
    Expect(ShouldCapture("[PortalProjectedEyeHistoryRebase] txn=1 applied=true."),
        "projected eye-history rebase diagnostics must be captured");
    Expect(ShouldCapture("[PortalEyeShadowPredictor] txn=1 applied=true."),
        "portal eye shadow-predictor diagnostics must be captured");
    Expect(ShouldCapture("[PortalEyeSingleOwner] event=consume txn=1 physicsMutation=false."),
        "the exact single-owner lifecycle tag must be captured");
    Expect(!ShouldCapture("[PortalEyeConvergentTail] event=start txn=1 physicsMutation=false."),
        "the disproved convergent-tail lifecycle tag must leave the focused policy");
    Expect(ShouldCapture("[PortalEyeLeafHandoffProbe] txn=1 reason=candidate-leaf-mismatch."),
        "the exact runtime delayed leaf-handoff probe tag must be captured");
    Expect(ShouldCapture("[PortalEyeLeafHandoffDecision] txn=1 decision=allow-safe-cross-leaf."),
        "safe cross-leaf authorization decisions must be captured");
    Expect(ShouldCapture("[PortalEyeReleaseTailProbe] txn=1 reason=horizon-expired."),
        "release-tail horizon probes must be captured");
    Expect(ShouldCapture("[PortalEyeSafety] txn=1 point=applied."),
        "portal eye safety diagnostics must be captured");
    Expect(ShouldCapture("[PortalPredictionCommandWindow] txn=1 commandDelta=2."),
        "prediction command-window diagnostics must be captured");
    Expect(ShouldCapture("[PortalVisualMarker] reason=flash txn=1."),
        "manual visual markers must be captured");
    Expect(ShouldCapture("[PortalServerRunCommandHook] installed=true index=454."),
        "server PlayerRunCommand hook validation must be captured");
    Expect(ShouldCapture("[PortalPredictionFinish] phase=finishmove-post-original."),
        "post-FinishMove prediction observations must be captured");
	Expect(ShouldCapture("[PortalPredictionAngleSync] angleSyncApplied=true."),
		"prediction angle synchronization diagnostics must be captured");
	Expect(ShouldCapture("[PortalMovementCommandTiming] txn=1 path=projected."),
		"movement command timing diagnostics must be captured");
    Expect(ShouldCapture("[PortalOfficialCommandEndCandidate] txn=1 matched=true."),
        "official command-end candidate diagnostics must be captured");
    Expect(ShouldCapture("[PortalOfficialActualCommandEnd] txn=1 matched=true."),
        "actual transformed command-end diagnostics must be captured");
    Expect(ShouldCapture("[PortalOfficialCommandEndAuthoritative] txn=1 commandDelta=1."),
        "authoritative next-command comparisons must be captured");
    Expect(ShouldCapture("[PortalServerCommandMovement] txn=1 cmd=10 matched=true."),
        "server command movement diagnostics must be captured");
    Expect(ShouldCapture("[PortalGroundLiveCommit] enabled=true action=defer-projected-candidate-to-observed-live."),
        "ground live-commit A/B diagnostics must be captured");
    Expect(ShouldCapture("[PortalRenderCadenceSummary] txn=1 frames=20."),
        "render-cadence transaction summaries must be captured");
    Expect(ShouldCapture("[PortalPredictionCommandWindowVelocity] stage=finishmove-post-original."),
        "prediction command-window velocities must be captured");
    Expect(ShouldCapture("[PortalServerTeleportTxn] stage=post."),
        "server teleport transaction snapshots must be captured");
    Expect(ShouldCapture("[PortalServerRunCommand] stage=pre mutation=false."),
        "server PlayerRunCommand diagnostics must be captured");
    Expect(ShouldCapture("[PortalEyeResidualBaseline] phase=Idle."),
        "non-portal eye residual baselines must be captured");
    Expect(ShouldCapture("[PortalInputAdjustAnglesHook] signatureValidated=true."),
        "the exact runtime input-angle hook tag must reach the focused file log");
    Expect(ShouldCapture("[PortalInputAdjustAnglesProbe] stage=summary initAttempted=true."),
        "late AdjustAngles probes must reach the focused file log");
    Expect(!ShouldCapture("[PortalInputRollClampHook] signatureValidated=true."),
        "a stale hook-tag spelling must not be captured through the roll-transaction needle");
    Expect(ShouldCapture("[PortalInputRollClamp] txn=4 restored=true."),
        "transaction roll restoration must reach the focused file log");
    Expect(ShouldCapture("[PortalViewReorientationBegin] txn=4 mode=roll."),
        "reorientation begin lifecycle evidence must reach the focused file log");
    Expect(ShouldCapture("[PortalViewReorientationFrame] txn=4 roll=90."),
        "compressed reorientation frames must reach the focused file log");
    Expect(ShouldCapture("[PortalViewReorientationEnd] txn=4 roll=0."),
        "reorientation completion evidence must reach the focused file log");
    Expect(ShouldCapture("[PortalViewReorientationReset] txn=4 reason=map-shutdown."),
        "reorientation reset evidence must reach the focused file log");
    Expect(ShouldCapture("[PortalFloorWallBegin] txn=4."),
        "floor-to-wall diagnostics must not be dropped by the allowlist");
    Expect(ShouldCapture("[PortalFloorWallCommand] txn=4."),
        "command-end floor-to-wall evidence must reach the file");
    Expect(ShouldCapture("[PortalFloorWallServerCommand] txn=4."),
        "server floor-to-wall evidence must reach the file");
    Expect(ShouldCapture("[PortalNativeInterpolationHook] installed=true."), "capture native hook validation");
    Expect(ShouldCapture("[PortalNativeInterpolationFrame] calls=0."), "capture native call evidence");
    Expect(ShouldCapture("[PortalNativePart1Hook] installed=true."), "capture Part1 validation");
    Expect(ShouldCapture("[PortalNativePart1Frame] stop=1."), "capture Part1 branch and time evidence");
    Expect(ShouldCapture("[PortalNativePart1Summary] calls=10."), "capture all-frame Part1 counters");
    Expect(ShouldCapture("[PortalNativeMapHook] installed=true."), "capture independently validated map identity");
    Expect(ShouldCapture("[PortalNativeMapFrame] mapCallObserved=true."), "capture map branch evidence");
    Expect(ShouldCapture("[PortalNativeMapSummary] calls=300."), "capture all accepted map calls");
    Expect(!ShouldCapture("[PortalNativeMapFrameNoise] ignored."), "map allowlist is exact");
    Expect(ShouldCapture("[PortalNativeOriginHook] installed=true."), "capture actual origin ABI validation");
    Expect(ShouldCapture("[PortalNativeOriginFrame] selectionCalled=true."), "capture actual selection evidence");
    Expect(ShouldCapture("[PortalNativeOriginSummary] calls=400."), "capture all-frame origin selection counts");
    Expect(!ShouldCapture("[PortalNativeOriginFrameNoise] ignored."), "origin tags are exact");
    Expect(ShouldCapture("[PortalNativeOriginResetHook] installed=true."), "capture validated native Reset ABI");
    Expect(ShouldCapture("[PortalNativeOriginReset] outsideRender=true."), "capture Reset outside interpolation scope");
    Expect(ShouldCapture("[PortalNativeOriginResetSamples] stage=after."), "capture bounded actual reset samples");
    Expect(ShouldCapture("[PortalNativeOriginResetSummary] calls=300."), "capture all natural reset calls");
    Expect(ShouldCapture("[PortalNativeOriginResetCaller] calls=300."), "capture caller histogram");
    Expect(!ShouldCapture("[PortalNativeOriginResetNoise] ignored."), "reset routes are exact");
    Expect(!ShouldCapture("[PortalNativePart1FrameNoise] ignored."), "Part1 tags remain exact");
    Expect(ShouldCapture("[PortalNativeCadenceCommandBatch] count=8."), "capture command batches");
    Expect(!ShouldCapture("[PortalNativeInterpolationHookNoise] ignored."), "native tags remain exact");
    Expect(ShouldCapture("[PortalNativeCadenceFrame] capture=4."),
        "native cadence samples must reach the file");
    Expect(!ShouldCapture("[PortalNativeCadenceFrameNoise] capture=4."),
        "native cadence routing must match exact tags");
    Expect(!ShouldCapture("ordinary initialization message"),
        "unrelated console messages must not be added to the focused file log");

    std::cout << "PortalFileLog policy tests passed\n";
    return 0;
}
