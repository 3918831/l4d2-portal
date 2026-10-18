#include "PortalNativeDiagnostics.h"
#include "PortalNativeCadence.h"
#include "PortalNativeInterpolationHook.h"
#include "PortalNativePredictionErrorHook.h"
#include "PortalNativeFallProbe.h"
#include "PortalNativeInterpolationPolicy.h"
#include "PortalNativePart1Policy.h"
#include "PortalTransitionDecision.h"
#include "../SDK/SDK.h"
#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/convar.h"
#include "../SDK/L4D2/Interfaces/ICvar.h"
#include "../Util/Logger/Logger.h"
#include <array>
#include <cstring>

namespace PortalNativeDiagnostics
{
    namespace
    {
        enum Stage { BeforeRender, AfterRender, OriginalView, AdaptedView, SubmittedView, StageCount };
        const char* const kStageNames[] = { "render-start-pre", "render-start-post", "calc-original", "calc-adapted", "main-view" };
        struct Snapshot
        {
            Vector origin, render, recv, eye, velocity;
            float curtime = 0, interpolation = 0, simulation = 0, frameTime = 0;
            int frame = -1, tick = -1, tickBase = 0, effects = 0, flags = 0, moveType = 0;
            bool valid = false, predictionValid = false, inPrediction = false, firstPrediction = false;
        };
        struct Frame
        {
            std::array<Snapshot, StageCount> stages;
            int frame = -1, inputCommand = 0, inputFrame = -1;
            float roll = 0;
            bool eyeStateValid = false, eyeHistory = false, eyeReleased = false, shadow = false;
            PortalNativeInterpolationHook::Status nativeStatus;
            PortalNativePredictionErrorHook::Status errorStatus;
            PortalNativeInterpolationPolicy::Calls nativeCalls;
            PortalNativePart1Policy::Calls part1Calls;
            unsigned part1TimeRelation=0;
        };
        PortalNativeCadence::Tracker g_tracker;
        Frame g_frame;
        std::array<Frame, 4> g_recent;
        unsigned g_recentCount = 0, g_recentNext = 0;
        std::uint32_t g_capture = 0, g_transaction = 0;
        int g_committedCommand = 0;
        PortalNativeCadence::RowBudget g_commandBudget, g_floorClientBudget, g_floorServerBudget;
        int g_lastMovementCommand = 0, g_processCommand = 0;
        Snapshot g_processSnapshot, g_lastFinish;
        PortalNativeInterpolationPolicy::CommandBatch g_commandBatch;
        unsigned g_commandSamples=0,g_commandAnomalies=0,g_commandBatched=0;
        PortalNativeInterpolationPolicy::Window g_nativeWindow;
        PortalNativePart1Policy::Window g_part1Window;
        PortalNativePart1Policy::DetailEdges g_part1Details;
        namespace ResetPolicy=PortalNativeResetPolicy;
        ResetPolicy::Window g_resetWindow;
        std::array<ResetPolicy::Observation,64> g_resetRecent;
        unsigned g_resetRecentCount=0,g_resetRecentNext=0,g_resetSequence=0;
        unsigned g_resetPreludeMatched=0,g_resetPreludeShown=0;
        thread_local ResetPolicy::Scope g_predictionScope;
        namespace ErrorPolicy=PortalNativePredictionErrorPolicy;
        ErrorPolicy::Window g_errorWindow;
        std::array<ErrorPolicy::Observation,128> g_errorRecent;
        unsigned g_errorRecentCount=0,g_errorRecentNext=0,g_errorSequence=0;
        unsigned g_errorPreludeMatched=0,g_errorPreludeShown=0;
        std::uint64_t g_errorEpoch=1;
        namespace FieldPolicy=PortalNativeFieldPolicy;
        struct FieldEvent {ErrorPolicy::Observation owner;FieldPolicy::Message message;};
        FieldPolicy::Window g_fieldWindow;
        const char* const kFallPhases[]={"server-pre","server-post","client-command-pre","client-command-post",
            "server-player-pre","server-walk-pre","client-player-pre","client-walk-pre"};
        std::array<PortalNativeFallPolicy::Window,sizeof(kFallPhases)/sizeof(*kFallPhases)> g_fallWindows;
        std::array<FieldEvent,128> g_fieldRecent;
        unsigned g_fieldRecentCount=0,g_fieldRecentNext=0,g_fieldPreludeMatched=0,g_fieldPreludeShown=0;
        C_BasePlayer* g_observedPlayer=nullptr;
        bool g_insideRender=false;
        bool g_waitingFall = false;
        float g_waitStart = 0;
        const char* g_kind = "none";

        const char* Yes(bool value) { return value ? "true" : "false"; }
        C_BasePlayer* Local()
        {
            if (!I::EngineClient || !I::ClientEntityList || !I::EngineClient->IsInGame()) return nullptr;
            auto* entity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
            auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
            return player && !player->deadflag() ? player : nullptr;
        }
        bool Enabled() { return U::PortalFileLog::ShouldCollectNativeDiagnostics(
            PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled(),I::GlobalVars!=nullptr); }
        Snapshot Read(C_BasePlayer* player, const Vector* eye = nullptr)
        {
            Snapshot s;
            if (!player || !I::GlobalVars) return s;
            s.origin = player->GetAbsOrigin(); s.render = player->GetRenderOrigin();
            s.recv = player->m_vecOrigin(); s.eye = eye ? *eye : player->EyePosition();
            s.velocity = player->m_vecVelocity();
            s.curtime = I::GlobalVars->curtime; s.frameTime = I::GlobalVars->frametime;
            s.interpolation = I::GlobalVars->interpolation_amount;
            s.simulation = player->m_flSimulationTime();
            s.frame = I::GlobalVars->framecount; s.tick = I::GlobalVars->tickcount;
            s.tickBase = player->m_nTickBase(); s.effects = player->m_fEffects(); s.flags = player->m_fFlags();
            s.moveType = player->m_MoveType();
            s.predictionValid = I::Prediction != nullptr;
            if (I::Prediction) { s.inPrediction = I::Prediction->InPrediction(); s.firstPrediction = I::Prediction->IsFirstTimePredicted(); }
            s.valid = true;
            return s;
        }
        void PrepareFrame()
        {
            if (g_frame.frame == I::GlobalVars->framecount) return;
            g_frame = {}; g_frame.frame = I::GlobalVars->framecount;
        }
        PortalNativeInterpolationPolicy::StageKey Key(const Snapshot& s)
        {
            return {{s.origin.x,s.origin.y,s.origin.z,s.render.x,s.render.y,s.render.z,
                s.recv.x,s.recv.y,s.recv.z,s.eye.x,s.eye.y,s.eye.z,s.velocity.x,s.velocity.y,s.velocity.z,
                s.curtime,s.interpolation,s.simulation,s.frameTime},
                {s.frame,s.tick,s.tickBase,s.effects,s.flags,s.moveType,s.predictionValid,s.inPrediction,s.firstPrediction},s.valid};
        }
        PortalNativeInterpolationPolicy::Vec3 Point(const Vector& v) {return {v.x,v.y,v.z};}
        const char* ResetPhase(ResetPolicy::Phase phase)
        {
            switch(phase) {case ResetPolicy::Phase::RunCommand:return "runcommand";
            case ResetPolicy::Phase::SetupMove:return "setupmove";
            case ResetPolicy::Phase::FinishMove:return "finishmove";default:return "unscoped";}
        }
        void PrintError(const ErrorPolicy::Observation& o,bool prelude)
        {
            U::LogInfo("[PortalNativePredictionError] capture=%u txn=%u seq=%u sourceCapture=%u sourceTxn=%u epoch=%llu prelude=%s packet=%u frame=%d tick=%d time=%.9g rootAck=%d entityAck=%d entity=0x%X local=%s typeKnown=%s type=%s indexKnown=%s index=%d map=0x%X contextValid=%s callerMatched=%s outcome=%u comparisonCalls=%u countKnown=%s errorCount=%d hadErrors=%s nativeMessages=%u nativeFields=%u scope=validated-network-root slot=unknown fields=native-formatted-text-only passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,o.sequence,o.capture,o.transaction,static_cast<unsigned long long>(o.epoch),Yes(prelude),o.packet,o.frame,o.tick,o.time,o.packetAck,o.ack,
                static_cast<unsigned>(o.entity),Yes(o.entity==o.localPlayer),Yes(o.typeKnown),o.type,Yes(o.indexKnown),o.index,static_cast<unsigned>(o.map),
                Yes(o.contextValid),Yes(o.callerMatched),static_cast<unsigned>(ErrorPolicy::Classify(o)),o.comparisonCalls,Yes(o.countKnown),o.errors,Yes(o.hadErrors),o.nativeMessages,o.nativeFields);
        }
        void PrintField(const FieldEvent& e,bool prelude)
        {
            const auto& o=e.owner;const auto& m=e.message;
            U::LogInfo("[PortalNativePredictionField] capture=%u txn=%u parentSeq=%u sourceCapture=%u epoch=%llu prelude=%s packet=%u frame=%d tick=%d entity=0x%X local=%s valid=%s readable=%s terminated=%s reportOrdinal=%d index=%d class=%s field=%s value='%s' source=natural-LogDirect-during-validated-error-comparison fields=formatted-text-only typedValues=not-read passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,o.sequence,o.capture,static_cast<unsigned long long>(o.epoch),Yes(prelude),o.packet,o.frame,o.tick,static_cast<unsigned>(o.entity),Yes(o.entity==o.localPlayer),Yes(m.valid),Yes(m.readable),Yes(m.terminated),m.ordinal,m.index,m.className,m.fieldName,m.value);
        }
        void PrintResetSamples(const ResetPolicy::Observation& o,const ResetPolicy::Snapshot& s,const char* stage,bool prelude)
        {
            const auto& a=s.samples[0];const auto& b=s.samples[1];const auto& c=s.samples[2];
            U::LogInfo("[PortalNativeOriginResetSamples] capture=%u txn=%u seq=%u frame=%d tick=%d stage=%s prelude=%s valid=%s s0(valid=%s time=%.9g value=(%.9g %.9g %.9g)) s1(valid=%s time=%.9g value=(%.9g %.9g %.9g)) s2(valid=%s time=%.9g value=(%.9g %.9g %.9g)) sampled=min(count,3) passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,o.sequence,o.frame,o.tick,stage,Yes(prelude),Yes(ResetPolicy::Safe(s)),
                Yes(a.valid),a.time,a.position.x,a.position.y,a.position.z,Yes(b.valid),b.time,b.position.x,b.position.y,b.position.z,
                Yes(c.valid),c.time,c.position.x,c.position.y,c.position.z);
        }
        void PrintReset(const ResetPolicy::Observation& o,bool prelude)
        {
            U::LogInfo("[PortalNativeOriginReset] capture=%u txn=%u seq=%u sourceCapture=%u sourceTxn=%u prelude=%s frame=%d tick=%d cmd=%d cmdKnown=%s latestInput=%d phase=%s insideRender=%s prediction(valid=%s in=%s first=%s) valid=%s failure=%u identity=%s caller(valid=%s module=%s rva=0x%X) parent(installed=%s state=%u active=%s identity=%s returnMatched=%s callerValid=%s module=%s rva=0x%X) resetLatch(modeKnown=%s mode=%d) clock(valid=%s globalsMatched=%s nativeBefore=%.9g nativeAfter=%.9g sdk=%.9g sim=%.9g tickBase=%d) flags=0x%X effects=0x%X moveType=%d before(valid=%s capacity=%u head=%u count=%u interval=%s clones=%s value=(%.9g %.9g %.9g)) after(valid=%s capacity=%u head=%u count=%u interval=%s clones=%s atNativeTime=%s value=(%.9g %.9g %.9g)) passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,o.sequence,o.capture,o.transaction,Yes(prelude),o.frame,o.tick,o.command,Yes(o.command>=0),o.latestInput,
                ResetPhase(o.phase),Yes(o.insideRender),Yes(o.predictionValid),Yes(o.inPrediction),Yes(o.firstPrediction),Yes(ResetPolicy::Valid(o)),o.failure,Yes(o.identity),
                Yes(o.callerValid),o.moduleName,o.callerRva,Yes(o.parentInstalled),static_cast<unsigned>(ResetPolicy::ParentState(o)),Yes(o.parent.active),Yes(o.parent.identity),
                Yes(o.parentReturnMatched),Yes(o.parent.callerValid),o.parent.moduleName,o.parent.callerRva,Yes(o.resetLatchModeKnown),o.resetLatchMode,
                Yes(o.clockValid),Yes(o.globalsMatched),o.nativeTime,o.nativeAfter,o.sdkTime,o.simulation,o.tickBase,o.flags,o.effects,o.moveType,
                Yes(ResetPolicy::Safe(o.before)),o.before.ring.capacity,o.before.ring.head,o.before.ring.count,Yes(ResetPolicy::FirstInterval(o.before)),Yes(ResetPolicy::ThreeClones(o.before)),
                o.before.value.x,o.before.value.y,o.before.value.z,Yes(ResetPolicy::Safe(o.after)),o.after.ring.capacity,o.after.ring.head,o.after.ring.count,
                Yes(ResetPolicy::FirstInterval(o.after)),Yes(ResetPolicy::ThreeClones(o.after)),Yes(ResetPolicy::ThreeClones(o.after)&&o.after.samples[0].time==o.nativeTime),
                o.after.value.x,o.after.value.y,o.after.value.z);
            PrintResetSamples(o,o.before,"before",prelude);PrintResetSamples(o,o.after,"after",prelude);
        }
        void PrintStage(const Snapshot& s, const char* name, bool prelude)
        {
            U::LogInfo("[PortalNativeCadenceStage] capture=%u txn=%u stage=%s prelude=%s valid=%s frame=%d tick=%d time(cur=%.6f dt=%.6f interp=%.6f sim=%.6f tickBase=%d) effects=0x%X flags=0x%X moveType=%d prediction(valid=%s in=%s first=%s) origin(abs=(%.3f %.3f %.3f) render=(%.3f %.3f %.3f) recvField=(%.3f %.3f %.3f)) eye=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f) behaviorMutation=false.\n",
                g_capture,g_transaction,name,Yes(prelude),Yes(s.valid),s.frame,s.tick,
                s.curtime,s.frameTime,s.interpolation,s.simulation,s.tickBase,s.effects,s.flags,s.moveType,
                Yes(s.predictionValid),Yes(s.inPrediction),Yes(s.firstPrediction),
                s.origin.x,s.origin.y,s.origin.z,s.render.x,s.render.y,s.render.z,s.recv.x,s.recv.y,s.recv.z,
                s.eye.x,s.eye.y,s.eye.z,s.velocity.x,s.velocity.y,s.velocity.z);
        }
        void PrintFrame(const Frame& f, const PortalNativeCadence::Result& result, bool prelude)
        {
            U::LogInfo("[PortalNativeCadenceFrame] capture=%u txn=%u kind=%s prelude=%s frame=%d inputCmd=%d inputValid=%s inputFrame=%d inputAge=%d committedCmd=%d roll=%.3f eyeState(valid=%s active=%s released=%s shadow=%s) frozen(original=%s final=%s originalComparable=%s) delta(original=%.4f final=%.4f) historyEvidence=OriginFrame+OriginReset behaviorMutation=false.\n",
                g_capture,g_transaction,g_kind,Yes(prelude),f.frame,f.inputCommand,Yes(f.inputCommand>0),f.inputFrame,
                f.inputFrame>=0 ? f.frame-f.inputFrame : -1,g_committedCommand,f.roll,
                Yes(f.eyeStateValid),Yes(f.eyeHistory),Yes(f.eyeReleased),Yes(f.shadow),
                Yes(result.originalFrozen),Yes(result.finalFrozen),Yes(result.originalComparable),result.originalDelta,result.finalDelta);
            for (int i=0; i<StageCount; ++i)
            {
                const auto& s=f.stages[i];
                if(i && PortalNativeInterpolationPolicy::SameStage(Key(s),Key(f.stages[i-1])))
                    U::LogInfo("[PortalNativeCadenceStage] capture=%u txn=%u frame=%d stage=%s prelude=%s valid=%s sameAs=%s exact=true delta(abs=0 render=0 recvField=0 eye=0 velocity=0 time=0).\n",
                        g_capture,g_transaction,f.frame,kStageNames[i],Yes(prelude),Yes(s.valid),kStageNames[i-1]);
                else PrintStage(s,kStageNames[i],prelude);
            }
            const auto& n=f.nativeCalls;
            const bool observed=f.nativeStatus.installed&&f.nativeStatus.dispatchValid
                &&f.stages[BeforeRender].valid&&f.stages[AfterRender].valid;
            const bool values=n.count>n.invalid;
            const bool afterComparable=n.validInsideRender>0&&f.stages[AfterRender].valid;
            const float afterDelta=afterComparable
                ? PortalNativeInterpolationPolicy::Distance(n.lastRenderAfter,Point(f.stages[AfterRender].origin)) : 0;
            const auto& p=f.part1Calls;
            const bool parentDetail=g_part1Details.Take(p,prelude)||f.part1TimeRelation>=2;
            if(parentDetail||n.invalid||!observed||n.count!=1||afterDelta>0)
            U::LogInfo("[PortalNativeInterpolationFrame] capture=%u txn=%u frame=%d prelude=%s installed=%s dispatchValid=%s renderWindowObserved=%s calls=%d insideRender=%d returnedTrue=%d changed=%d invalid=%d valuesValid=%s inputTime(first=%.6f last=%.6f global=%.6f) abs(firstBefore=(%.3f %.3f %.3f) lastAfter=(%.3f %.3f %.3f)) maxDelta=%.4f afterRenderComparable=%s afterRenderDifference=%.4f selectedTimeSource=Part1Frame historyEvidence=OriginFrame detail=edge-or-anomaly returnIsActivity=false behaviorMutation=false.\n",
                g_capture,g_transaction,f.frame,Yes(prelude),Yes(f.nativeStatus.installed),Yes(f.nativeStatus.dispatchValid),Yes(observed),
                n.count,n.insideRender,n.returnedTrue,n.changed,n.invalid,Yes(values),n.firstTime,n.lastTime,n.lastGlobalTime,
                n.firstBefore.x,n.firstBefore.y,n.firstBefore.z,n.lastAfter.x,n.lastAfter.y,n.lastAfter.z,n.maxDelta,Yes(afterComparable),afterDelta);
            if(parentDetail)
            U::LogInfo("[PortalNativePart1Frame] capture=%u txn=%u frame=%d prelude=%s observed=%s outerCalls=%d calls=%u callerMatched=%u stop=%u continued=%u unknownResult=%u invalid=%u nativeOriginChanged=%u noMore(true=%u false=%u) valuesValid=%s time(inputFirst=%.6f inputLast=%.6f selectedLast=%.6f offsetMin=%.6f offsetMax=%.6f adjusted=%u relation=%u) resultLast=%d noMoreLast=%d nativeLocalOrigin(firstBefore=(%.3f %.3f %.3f) lastAfter=(%.3f %.3f %.3f) maxDelta=%.4f) nativeLocalAngles(before=(%.3f %.3f %.3f) after=(%.3f %.3f %.3f)) oldOutputs(count=%u origin=(%.3f %.3f %.3f) rotation=(%.3f %.3f %.3f)) localSource=validated-Part1-and-Part2 oldOutputsMeaning=last-CONTINUE STOPoutputs=unread noMoreIsActivity=false historyEvidence=OriginFrame behaviorMutation=false.\n",
                g_capture,g_transaction,f.frame,Yes(prelude),Yes(observed&&f.nativeStatus.part1Installed),n.count,
                p.count,p.matched,p.stopped,p.continued,p.unknownResult,p.invalid,p.changed,p.noMoreTrue,p.noMoreFalse,
                Yes(p.count>p.invalid),p.firstInput,p.lastInput,p.lastSelected,p.minOffset,p.maxOffset,p.timeAdjusted,f.part1TimeRelation,p.lastResult,p.lastNoMore,
                p.firstBefore.x,p.firstBefore.y,p.firstBefore.z,p.lastAfter.x,p.lastAfter.y,p.lastAfter.z,p.maxDelta,
                p.lastBeforeAngles.x,p.lastBeforeAngles.y,p.lastBeforeAngles.z,p.lastAfterAngles.x,p.lastAfterAngles.y,p.lastAfterAngles.z,
                p.oldOutputsValid,p.lastOldOrigin.x,p.lastOldOrigin.y,p.lastOldOrigin.z,p.lastOldAngles.x,p.lastOldAngles.y,p.lastOldAngles.z);
            const auto& m=p.lastMap;
            U::LogInfo("[PortalNativeMapFrame] capture=%u txn=%u frame=%d prelude=%s mapHookInstalled=%s mapCallObserved=%s mapValid=%s mapCalls=%u mapInvalid=%u total=%d participatingCount=%d time(before=%.6f selected=%.6f after=%.6f) origin(index=%d needsBefore=%d needsAfter=%d branch=%u) angles(index=%d needsBefore=%d needsAfter=%d branch=%u) nativeOriginChanged=%u nativeAnglesChanged=%u recvOriginChanged=%u entryLocalOldOutputs(matched=%u mismatched=%u) nativeOriginOutcome=%u nativeAnglesOutcome=%u watcherCallObserved=%s branchIsInvocation=false selectionEvidence=OriginFrame behaviorMutation=false.\n",
                g_capture,g_transaction,f.frame,Yes(prelude),Yes(f.nativeStatus.mapInstalled),Yes(m.called),Yes(m.valid),p.mapCalls,p.mapInvalid,
                m.total,m.count,m.beforeTime,m.selected,m.afterTime,m.originIndex,m.originNeeds,m.originAfter,m.OriginState(),
                m.anglesIndex,m.anglesNeeds,m.anglesAfter,m.AnglesState(),p.changed,p.anglesChanged,p.recvChanged,
                p.corroborated,p.corroborationMismatch,PortalNativePart1Policy::Outcome(p,observed&&f.nativeStatus.part1Installed&&f.nativeStatus.localIdentity),
                PortalNativePart1Policy::AnglesOutcome(p,observed&&f.nativeStatus.part1Installed&&f.nativeStatus.localIdentity),Yes(p.origin.calls>0));
            const auto& o=p.origin.last;const auto& sel=o.selection;
            U::LogInfo("[PortalNativeOriginFrame] capture=%u txn=%u frame=%d prelude=%s installed=%s calls=%u invalid=%u callerMatched=%s identity=%s watcherValid=%s watcherFailure=%u time=%.9g noMore=%d changed=%u delta=%.9g value(before=(%.9g %.9g %.9g) after=(%.9g %.9g %.9g)) selection(called=%s callerMatched=%s outputsValid=%s returned=%s failure=%u noMore=%d hermite=%s fraction=%.9g current=%.9g amount=%.9g effective=%.9g state=%u) ring(valid=%s capacity=%u head=%u count=%u samplesValid=%s) newest(index=%d valid=%s time=%.9g value=(%.9g %.9g %.9g)) newer(index=%d valid=%s time=%.9g value=(%.9g %.9g %.9g)) older(index=%d valid=%s time=%.9g value=(%.9g %.9g %.9g)) oldest(index=%d valid=%s time=%.9g value=(%.9g %.9g %.9g)) passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,f.frame,Yes(prelude),Yes(f.nativeStatus.originInstalled),p.origin.calls,p.origin.invalid,
                Yes(o.callerMatched),Yes(o.identity),Yes(o.valid),o.failure,o.time,o.noMore,p.origin.changed,
                PortalNativeOriginPolicy::Distance(o.before,o.after),o.before.x,o.before.y,o.before.z,o.after.x,o.after.y,o.after.z,
                Yes(sel.called),Yes(sel.callerMatched),Yes(sel.outputsValid),Yes(sel.returned),sel.failure,sel.noMore,Yes(sel.hermite),sel.fraction,sel.time,sel.amount,sel.effective,
                unsigned(PortalNativeOriginPolicy::State(sel)),Yes(sel.ringValid),sel.ring.capacity,sel.ring.head,sel.ring.count,Yes(sel.samplesValid),
                sel.newest.index,Yes(sel.newest.valid),sel.newest.time,sel.newest.position.x,sel.newest.position.y,sel.newest.position.z,
                sel.newerIndex,Yes(sel.newer.valid),sel.newer.time,sel.newer.position.x,sel.newer.position.y,sel.newer.position.z,
                sel.olderIndex,Yes(sel.older.valid),sel.older.time,sel.older.position.x,sel.older.position.y,sel.older.position.z,
                sel.oldestIndex,Yes(sel.oldest.valid),sel.oldest.time,sel.oldest.position.x,sel.oldest.position.y,sel.oldest.position.z);
        }
        void FlushCommands()
        {
            const auto& b=g_commandBatch;
            if(!b.count) return;
            U::LogInfo("[PortalNativeCadenceCommandBatch] capture=%u txn=%u count=%d firstCmd=%d lastCmd=%d speed(min=%.3f max=%.3f) processAndFinishObserved=true finishMoveErrorBelow=0.01 replay=false behaviorMutation=false.\n",
                g_capture,g_transaction,b.count,b.first,b.last,b.minSpeed,b.maxSpeed);
            g_commandBatch={};
        }
        void Summary(const char* reason)
        {
            FlushCommands();
            const auto& s = g_tracker.GetSummary();
            const auto& n=g_nativeWindow;
            U::LogInfo("[PortalNativeCadenceSummary] capture=%u txn=%u kind=%s reason=%s frames=%d emitted=%d suppressed=%d originalComparableFrames=%d freezeMs(originalTotal=%.3f originalMax=%.3f finalTotal=%.3f finalMax=%.3f) freezeTimebase=engine-frametime maxStep(original=%.3f final=%.3f) commandRows=%d commandRowsDropped=%d commandSamples=%u commandAnomalies=%u commandBatched=%u native(observedFrames=%u noCallFrames=%u calls=%u changed=%u invalid=%u afterComparable=%u afterDifferent=%u afterMax=%.4f changedOriginalFrozen=%u changedFinalFrozen=%u) floorClientRows=%d floorClientDropped=%d floorServerRows=%d floorServerDropped=%d behaviorMutation=false.\n",
                g_capture,g_transaction,g_kind,reason,s.frames,s.emitted,s.suppressed,s.originalComparableFrames,
                s.totalOriginalFreezeMs,s.maxOriginalFreezeMs,s.totalFinalFreezeMs,s.maxFinalFreezeMs,
                s.maxOriginalStep,s.maxFinalStep,g_commandBudget.used,g_commandBudget.dropped,
                g_commandSamples,g_commandAnomalies,g_commandBatched,n.frames,n.noCall,n.calls,n.changed,n.invalid,
                n.comparable,n.different,n.maxDifference,n.changedOriginalFrozen,n.changedFinalFrozen,
                g_floorClientBudget.used,g_floorClientBudget.dropped,g_floorServerBudget.used,g_floorServerBudget.dropped);
            const auto& p=g_part1Window;
            U::LogInfo("[PortalNativePart1Summary] capture=%u txn=%u frames=%u observedFrames=%u notObservedFrames=%u noCallFrames=%u outerWithoutPart1=%u calls=%u callerMatched=%u stop=%u continued=%u unknownResult=%u invalid=%u nativeOriginChanged=%u noMore(true=%u false=%u) time(adjusted=%u offsetValid=%s offsetMin=%.6f offsetMax=%.6f selectedStalled=%u selectedBackwards=%u) timeComparison=consecutive-valid-frames-with-input-advance noMoreIsActivity=false behaviorMutation=false.\n",
                g_capture,g_transaction,p.frames,p.observedFrames,p.notObservedFrames,p.noCallFrames,p.outerWithoutPart1,
                p.calls,p.matched,p.stopped,p.continued,p.unknownResult,p.invalid,p.changed,p.noMoreTrue,p.noMoreFalse,
                p.timeAdjusted,Yes(p.hasOffset),p.minOffset,p.maxOffset,p.selectedStalled,p.selectedBackwards);
            U::LogInfo("[PortalNativeMapSummary] capture=%u txn=%u frames=%u mapCalls=%u mapInvalid=%u mapUnavailable=%u nativeOriginChanged=%u nativeAnglesChanged=%u recvOriginChanged=%u corroborated=%u corroborationMismatch=%u origin(notCalled=%u invalid=%u outsideRange=%u needsFalse=%u eligible=%u rewindEligible=%u missingBinding=%u) angles(notCalled=%u invalid=%u outsideRange=%u needsFalse=%u eligible=%u rewindEligible=%u missingBinding=%u) counters=all-accepted-frames invocationEvidence=OriginSummary branchIsInvocation=false behaviorMutation=false.\n",
                g_capture,g_transaction,p.frames,p.mapCalls,p.mapInvalid,p.mapUnavailable,p.changed,p.anglesChanged,p.recvChanged,p.corroborated,p.corroborationMismatch,
                p.originBranches[0],p.originBranches[1],p.originBranches[2],p.originBranches[3],p.originBranches[4],p.originBranches[5],p.originBranches[6],
                p.anglesBranches[0],p.anglesBranches[1],p.anglesBranches[2],p.anglesBranches[3],p.anglesBranches[4],p.anglesBranches[5],p.anglesBranches[6]);
            const auto& o=p.origin;
            U::LogInfo("[PortalNativeOriginSummary] capture=%u txn=%u frames=%u hookUnavailable=%u eligibleNoCall=%u calls=%u invalid=%u changed=%u noChange=%u noMore(true=%u false=%u) selection(calls=%u invalid=%u returnedTrue=%u returnedFalse=%u missing=%u unknown=%u empty=%u noSelection=%u clamped=%u repeated=%u interval=%u hermite=%u) amount(valid=%s min=%.6f max=%.6f) historyCount(valid=%s min=%u max=%u) counters=all-accepted-frames returnIsActivity=false passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,p.frames,p.originUnavailable,p.originEligibleNoCall,o.calls,o.invalid,o.changed,o.noChange,o.noMoreTrue,o.noMoreFalse,
                o.selectionCalls,o.selectionInvalid,o.selectionTrue,o.selectionFalse,o.missingSelection,o.states[0],o.states[2],o.states[3],o.states[4],o.states[5],o.states[6],o.states[7],
                Yes(o.hasAmount),o.minAmount,o.maxAmount,Yes(o.hasCount),o.minCount,o.maxCount);
            const auto& resets=g_resetWindow;
            U::LogInfo("[PortalNativeOriginResetSummary] capture=%u txn=%u installed=%s calls=%u invalid=%u clonesAfter=%u lostFirstInterval=%u valueChangedByOriginal=%u unknownCaller=%u callerOverflow=%u frameGroups=%u tickGroups=%u commandGroups=%u commandUnknown=%u phases(unscoped=%u runcommand=%u setupmove=%u finishmove=%u invalid=%u) parent(unavailable=%u unscoped=%u mismatch=%u invalidCaller=%u matched=%u callerOverflow=%u) resetLatch(unknown=%u mode0=%u mode1=%u mode2=%u other=%u) detail(emitted=%u dropped=%u max=48) prelude(matched=%u shown=%u max=8) counters=all-observed-reset-entry-calls-during-capture frameGroupsMeaning=consecutive-groups passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,Yes(g_frame.nativeStatus.resetInstalled),resets.calls,resets.invalid,resets.clones,resets.lostInterval,resets.valueChanged,
                resets.unknownCaller,resets.callerOverflow,resets.frameGroups,resets.tickGroups,resets.commandGroups,resets.commandUnknown,
                resets.phases[0],resets.phases[1],resets.phases[2],resets.phases[3],resets.phaseInvalid,
                resets.parentStates[0],resets.parentStates[1],resets.parentStates[2],resets.parentStates[3],resets.parentStates[4],resets.parentOverflow,
                resets.resetModes[0],resets.resetModes[1],resets.resetModes[2],resets.resetModes[3],resets.resetModes[4],
                resets.emitted,resets.dropped,g_resetPreludeMatched,g_resetPreludeShown);
            for(const auto& caller:resets.callers) if(caller.calls)
                U::LogInfo("[PortalNativeOriginResetCaller] capture=%u txn=%u module=%s moduleBase=0x%X rva=0x%X calls=%u invalid=%u clonesAfter=%u phases(unscoped=%u runcommand=%u setupmove=%u finishmove=%u) passive=true behaviorMutation=false.\n",
                    g_capture,g_transaction,caller.name,static_cast<unsigned>(caller.module),caller.rva,caller.calls,caller.invalid,caller.clones,
                    caller.phases[0],caller.phases[1],caller.phases[2],caller.phases[3]);
            for(const auto& caller:resets.parentCallers) if(caller.calls)
                U::LogInfo("[PortalNativeOriginResetParentCaller] capture=%u txn=%u module=%s moduleBase=0x%X rva=0x%X calls=%u invalidChild=%u clonesAfter=%u phases(unscoped=%u runcommand=%u setupmove=%u finishmove=%u) attribution=validated-resetlatched-base-entry-return-address passive=true behaviorMutation=false.\n",
                    g_capture,g_transaction,caller.name,static_cast<unsigned>(caller.module),caller.rva,caller.calls,caller.invalid,caller.clones,
                    caller.phases[0],caller.phases[1],caller.phases[2],caller.phases[3]);
            const auto& errors=g_errorWindow;
            U::LogInfo("[PortalNativePredictionErrorSummary] capture=%u txn=%u installed=%s identityValidated=%s calls=%u outcome(invalid=%u notChecked=%u noErrors=%u errors=%u) hadErrorsObserved=%u localCalls=%u localErrors=%u errorSum=%llu unknownType=%u packetGroups=%u entityOverflow=%u nativeMessages=%u nativeFields=%u positiveWithoutNativeField=%u detail(emitted=%u dropped=%u max=48) prelude(matched=%u shown=%u max=8 recent=128 priority=errors-invalid-clean chronological=true) counters=all-observed-valid-network-root-entity-calls-during-capture slot=unknown passive=true behaviorMutation=false.\n",
                g_capture,g_transaction,Yes(g_frame.errorStatus.installed),Yes(g_frame.errorStatus.identityValidated),errors.calls,
                errors.outcomes[0],errors.outcomes[1],errors.outcomes[2],errors.outcomes[3],errors.hadErrorsObserved,errors.localCalls,errors.localErrors,
                static_cast<unsigned long long>(errors.errorSum),errors.unknownType,errors.packetGroups,errors.entityOverflow,errors.nativeMessages,errors.nativeFields,errors.positiveWithoutNativeField,
                errors.emitted,errors.dropped,g_errorPreludeMatched,g_errorPreludeShown);
            for(const auto& entity:errors.entities) if(entity.calls)
                U::LogInfo("[PortalNativePredictionErrorEntity] capture=%u txn=%u entity=0x%X indexKnown=%s index=%d typeKnown=%s type=%s calls=%u outcome(invalid=%u notChecked=%u noErrors=%u errors=%u) errorSum=%llu grouping=native-address-during-window passive=true behaviorMutation=false.\n",
                    g_capture,g_transaction,static_cast<unsigned>(entity.address),Yes(entity.indexKnown),entity.index,Yes(entity.typeKnown),entity.type,entity.calls,
                    entity.outcomes[0],entity.outcomes[1],entity.outcomes[2],entity.outcomes[3],static_cast<unsigned long long>(entity.errorSum));
            const auto& fields=g_fieldWindow;
            U::LogInfo("[PortalNativePredictionFieldSummary] capture=%u txn=%u installed=%s messages=%u parsed=%u unknown=%u unreadable=%u unterminated=%u fieldOverflow=%u detail(emitted=%u dropped=%u max=48) prelude(matched=%u shown=%u max=8 recent=128) coverage=all-observed-formatted-message-callbacks-during-valid-comparison fields=formatted-text-only requiresNativeReport=cl_showerror-2 behaviorMutation=false.\n",
                g_capture,g_transaction,Yes(g_frame.errorStatus.fieldInstalled),fields.messages,fields.parsed,fields.unknown,fields.unreadable,fields.unterminated,fields.overflow,fields.emitted,fields.dropped,g_fieldPreludeMatched,g_fieldPreludeShown);
            for(const auto& field:fields.fields)if(field.occurrences)
                U::LogInfo("[PortalNativePredictionFieldAggregate] capture=%u txn=%u index=%d class=%s field=%s occurrences=%u firstValue='%s' lastValue='%s' grouping=native-index-class-field-during-window values=original-native-text typedValues=not-read behaviorMutation=false.\n",
                    g_capture,g_transaction,field.first.index,field.first.className,field.first.fieldName,field.occurrences,field.first.value,field.last.value);
            for(unsigned i=0;i<g_fallWindows.size();++i)
            {
                const auto& f=g_fallWindows[i];
                U::LogInfo("[PortalNativeFallStateSummary] capture=%u txn=%u phase=%s calls=%u valid=%u unavailable=%u ground(null=%u nonNull=%u unknown=%u) fall(first=%.9g last=%.9g min=%.9g max=%.9g changed=%u held=%u) instantaneousRelation(valid=%u fallPlusZOverHalf=%u isPredictionError=false) detail(emitted=%u dropped=%u max=48 suppressedUnavailable=%u) movement(matched=%u moveVelocityZValid=%u) counters=all-observed-stage-calls fieldWrites=false behaviorMutation=false.\n",
                    g_capture,g_transaction,kFallPhases[i],f.calls,f.valid,f.unavailable,f.groundNull,f.groundNonNull,f.groundUnknown,
                    f.first,f.last,f.min,f.max,f.changed,f.held,f.relationValid,f.mismatch,f.emitted,f.dropped,f.suppressedUnavailable,f.movementMatched,f.moveZValid);
            }
            PortalNativeFallProbe::Report(g_capture,g_transaction);
        }
        void Begin(std::uint32_t txn, int cmd, const char* kind)
        {
            if (g_tracker.Active()) Summary("superseded");
            ++g_capture; g_transaction=txn; g_committedCommand=cmd; g_kind=kind;
            g_waitingFall=false; g_commandBudget={}; g_floorClientBudget={}; g_floorServerBudget={}; g_lastMovementCommand=0;
            g_processCommand=0;g_processSnapshot={};g_lastFinish={};g_commandBatch={};
            g_commandSamples=g_commandAnomalies=g_commandBatched=0;
            g_nativeWindow={};
            g_part1Window={};
            g_part1Details={};
            g_resetWindow={};g_resetPreludeMatched=g_resetPreludeShown=0;
            g_errorWindow={};g_errorPreludeMatched=g_errorPreludeShown=0;
            g_fieldWindow={};g_fieldPreludeMatched=g_fieldPreludeShown=0;
            g_fallWindows={}; // metadata binding belongs to identity, not capture
            g_tracker.Begin(txn,cmd,I::GlobalVars->tickcount,I::GlobalVars->realtime);
            U::LogInfo("[PortalNativeCadenceBegin] version=13 capture=%u txn=%u kind=%s committedCmd=%d frame=%d tick=%d interval=%.6f maxTicks=40 maxFrames=2048 maxRealSeconds=5 part1Probe=true mapProbe=true watcherHook=%s selectionHook=%s originResetHook=%s resetLatchedHook=%s predictionErrorHook=%s predictionIdentityValidated=%s predictionFieldHook=%s fallStateProbe=read-only-command-stages fallBindingCache=identity-success-and-failure fallMetadataText=bounded-readable fallUpdatePath=command-player-walk-command behaviorMutation=false.\n",
                g_capture,txn,kind,cmd,I::GlobalVars->framecount,I::GlobalVars->tickcount,I::GlobalVars->interval_per_tick,Yes(g_frame.nativeStatus.originInstalled),Yes(g_frame.nativeStatus.selectionInstalled),Yes(g_frame.nativeStatus.resetInstalled),Yes(g_frame.nativeStatus.resetLatchedInstalled),Yes(g_frame.errorStatus.installed),Yes(g_frame.errorStatus.identityValidated),Yes(g_frame.errorStatus.fieldInstalled));
            std::array<ResetPolicy::Observation,8> preludes;
            const auto player=reinterpret_cast<std::uintptr_t>(Local());
            for(unsigned i=0;i<g_resetRecentCount;++i)
            {
                const auto& event=g_resetRecent[(g_resetRecentNext+g_resetRecent.size()-g_resetRecentCount+i)%g_resetRecent.size()];
                if(!ResetPolicy::PreludeMatches(event,player,I::GlobalVars->framecount,I::GlobalVars->tickcount)) continue;
                ++g_resetPreludeMatched;
                if(g_resetPreludeShown<preludes.size()) preludes[g_resetPreludeShown++]=event;
                else {for(unsigned j=1;j<preludes.size();++j) preludes[j-1]=preludes[j];preludes.back()=event;}
            }
            for(unsigned i=0;i<g_resetPreludeShown;++i) PrintReset(preludes[i],true);
            ErrorPolicy::Prelude errorPreludes;
            for(unsigned i=0;i<g_errorRecentCount;++i)
            {
                const auto& event=g_errorRecent[(g_errorRecentNext+g_errorRecent.size()-g_errorRecentCount+i)%g_errorRecent.size()];
                if(!ErrorPolicy::InLifecycle(event,g_errorEpoch)||!ErrorPolicy::PreludeMatches(event,player,I::GlobalVars->framecount,I::GlobalVars->tickcount)) continue;
                ++g_errorPreludeMatched;
                errorPreludes.Add(event);
            }
            g_errorPreludeShown=errorPreludes.count;
            for(unsigned i=0;i<g_errorPreludeShown;++i) PrintError(errorPreludes.rows[i],true);
            std::array<FieldEvent,8> fieldPreludes;
            for(unsigned i=0;i<g_fieldRecentCount;++i)
            {
                const auto& e=g_fieldRecent[(g_fieldRecentNext+g_fieldRecent.size()-g_fieldRecentCount+i)%g_fieldRecent.size()];
                if(!ErrorPolicy::InLifecycle(e.owner,g_errorEpoch)||!ErrorPolicy::PreludeMatches(e.owner,player,I::GlobalVars->framecount,I::GlobalVars->tickcount))continue;
                ++g_fieldPreludeMatched;
                if(g_fieldPreludeShown<fieldPreludes.size())fieldPreludes[g_fieldPreludeShown++]=e;
                else {for(unsigned j=1;j<fieldPreludes.size();++j)fieldPreludes[j-1]=fieldPreludes[j];fieldPreludes.back()=e;}
            }
            for(unsigned i=0;i<g_fieldPreludeShown;++i)PrintField(fieldPreludes[i],true);
            const char* names[] = { "cl_predict", "cl_interpolate", "cl_interp", "cl_interp_ratio", "cl_smooth", "cl_smoothtime", "host_timescale", "fps_max", "cl_pred_doresetlatch", "cl_pred_optimize", "cl_showerror" };
            for (const char* name : names)
            {
                auto* value = I::Cvar ? I::Cvar->FindVar(name) : nullptr;
                U::LogInfo("[PortalNativeCadenceCvar] capture=%u name=%s valid=%s value=%.6f intValue=%d behaviorMutation=false.\n",g_capture,name,Yes(value!=nullptr),value ? value->GetFloat() : 0,value ? value->GetInt() : 0);
            }
            for (unsigned i=0; i<g_recentCount; ++i)
            {
                const Frame& f=g_recent[(g_recentNext+g_recent.size()-g_recentCount+i)%g_recent.size()];
                const int age=I::GlobalVars->tickcount-f.stages[SubmittedView].tick;
                if (age>=0 && age<=4) PrintFrame(f,{},true);
            }
        }
    }

    void Reset(const char* reason)
    {
        if (g_tracker.Active()) Summary(reason);
        g_fallWindows={};PortalNativeFallProbe::Reset();
        g_tracker.Reset(); g_frame={}; g_recent={}; g_recentCount=g_recentNext=0;
        g_waitingFall=false; g_transaction=0; g_committedCommand=0;
        g_observedPlayer=nullptr;g_insideRender=false;g_commandBatch={};
        g_resetRecent={};g_resetRecentCount=g_resetRecentNext=0;g_predictionScope={};
        g_errorRecent={};g_errorRecentCount=g_errorRecentNext=0;
        g_fieldRecent={};g_fieldRecentCount=g_fieldRecentNext=0;
        if(!++g_errorEpoch) ++g_errorEpoch;
    }
    void SetEnabled(bool enabled)
    {
        if(U::PortalFileLog::NativeDiagnosticsEnabled()!=enabled)
        {
            Reset("native-diagnostics-toggle");
            U::PortalFileLog::SetNativeDiagnosticsEnabled(enabled);
        }
        Report();
    }
    void Report()
    {
        U::LogInfo("[PortalBsp][Command] command=portal_diag_native enabled=%s collecting=%s transactionDiagnostics=%s fallFixIndependent=true usage='portal_diag_native <0|1>'.\n",
            Yes(U::PortalFileLog::NativeDiagnosticsEnabled()),Yes(Enabled()),Yes(PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled()));
    }
    void ObserveInput(const CUserCmd* cmd)
    {
        if (cmd && I::GlobalVars) g_tracker.ObserveCommand(cmd->command_number,I::GlobalVars->framecount);
    }
    int LatestInputCommand() { return g_tracker.LastCommand(); }
    ResetPolicy::Scope EnterPredictionScope(C_BasePlayer* player,const CUserCmd* cmd,ResetPolicy::Phase phase)
    {
        const auto previous=g_predictionScope;
        g_predictionScope={reinterpret_cast<std::uintptr_t>(player),cmd?cmd->command_number:-1,phase};
        return previous;
    }
    void LeavePredictionScope(const ResetPolicy::Scope& previous) {g_predictionScope=previous;}
    C_BasePlayer* OriginResetPlayer() {return Enabled()?Local():nullptr;}
    bool OriginResetBegin(C_BasePlayer* player,ResetPolicy::Observation& o)
    {
        if(!Enabled()||!player||player!=Local()) return false;
        o.player=reinterpret_cast<std::uintptr_t>(player);o.sequence=++g_resetSequence;
        o.frame=I::GlobalVars->framecount;o.tick=I::GlobalVars->tickcount;o.sdkTime=I::GlobalVars->curtime;
        o.simulation=player->m_flSimulationTime();o.tickBase=player->m_nTickBase();
        o.flags=player->m_fFlags();o.effects=player->m_fEffects();o.moveType=player->m_MoveType();
        o.active=g_tracker.Active();o.capture=o.active?g_capture:0;o.transaction=o.active?g_transaction:0;
        o.contextValid=true;o.insideRender=g_insideRender;o.latestInput=g_tracker.LastCommand();
        if(g_predictionScope.player==o.player) {o.phase=g_predictionScope.phase;o.command=g_predictionScope.command;}
        o.predictionValid=I::Prediction!=nullptr;
        auto* latch=I::Cvar?I::Cvar->FindVar("cl_pred_doresetlatch"):nullptr;
        o.resetLatchModeKnown=latch!=nullptr;o.resetLatchMode=latch?latch->GetInt():-1;
        if(I::Prediction) {o.inPrediction=I::Prediction->InPrediction();o.firstPrediction=I::Prediction->IsFirstTimePredicted();}
        return true;
    }
    void OriginResetEnd(C_BasePlayer* player,ResetPolicy::Observation& o)
    {
        if(!Enabled()||player!=Local()) return;
        if(ResetPolicy::InCapture(o,g_tracker.Active(),g_capture))
        {
            if(g_resetWindow.Add(o)) PrintReset(o,false); // counters first, detail budget second
        }
        g_resetRecent[g_resetRecentNext]=o;g_resetRecentNext=(g_resetRecentNext+1)%g_resetRecent.size();
        g_resetRecentCount=std::min<unsigned>(g_resetRecentCount+1,static_cast<unsigned>(g_resetRecent.size()));
    }
    bool PredictionErrorBegin(const ErrorPolicy::PacketScope& packet,void* entity,int ack,ErrorPolicy::Observation& o)
    {
        auto* local=Local();
        if(!Enabled()||!local||!entity||!ErrorPolicy::InLifecycle(packet,g_errorEpoch)||packet.localPlayer!=reinterpret_cast<std::uintptr_t>(local)) return false;
        o.localPlayer=packet.localPlayer;o.entity=reinterpret_cast<std::uintptr_t>(entity);o.packet=packet.sequence;o.packetAck=packet.ack;o.ack=ack;
        o.sequence=++g_errorSequence;o.frame=I::GlobalVars->framecount;o.tick=I::GlobalVars->tickcount;o.time=I::GlobalVars->curtime;
        o.epoch=packet.epoch;
        o.contextValid=packet.active&&packet.identity;o.active=g_tracker.Active();o.capture=o.active?g_capture:0;o.transaction=o.active?g_transaction:0;
        return true;
    }
    void PredictionErrorEnd(ErrorPolicy::Observation& o)
    {
        if(!Enabled()||!ErrorPolicy::InLifecycle(o,g_errorEpoch)||o.localPlayer!=reinterpret_cast<std::uintptr_t>(Local())) return;
        if(ErrorPolicy::InCapture(o,g_tracker.Active(),g_capture)&&g_errorWindow.Add(o)) PrintError(o,false);
        g_errorRecent[g_errorRecentNext]=o;g_errorRecentNext=(g_errorRecentNext+1)%g_errorRecent.size();
        g_errorRecentCount=std::min<unsigned>(g_errorRecentCount+1,static_cast<unsigned>(g_errorRecent.size()));
    }
    std::uint64_t PredictionErrorEpoch() {return g_errorEpoch;}
    void PredictionFieldMessage(const ErrorPolicy::Observation& owner,const FieldPolicy::Message& message)
    {
        if(!Enabled()||!FieldPolicy::Eligible(owner,true,g_errorEpoch,reinterpret_cast<std::uintptr_t>(Local())))return;
        FieldEvent e{owner,message};
        if(ErrorPolicy::InCapture(owner,g_tracker.Active(),g_capture)&&g_fieldWindow.Add(message))PrintField(e,false);
        g_fieldRecent[g_fieldRecentNext]=e;g_fieldRecentNext=(g_fieldRecentNext+1)%g_fieldRecent.size();
        g_fieldRecentCount=std::min<unsigned>(g_fieldRecentCount+1,static_cast<unsigned>(g_fieldRecent.size()));
    }
    void BeginPortal(std::uint32_t txn, int cmd, bool floorWall)
    {
        if (Enabled() && txn && cmd>0) Begin(txn,cmd,floorWall ? "floor-wall" : "portal-other");
    }
    void ArmNormalFall()
    {
        if (!Enabled() || !Local())
        {
            U::LogInfo("[PortalBsp][Command] command=portal_diag_nativefall armed=false reason=requires-live-local-player-transactiondiag-and-native-diag.\n");
            return;
        }
        if (g_tracker.Active()) Summary("normal-fall-armed");
        // End the old window without losing the last observed input command.
        const int cmd=g_tracker.LastCommand(), frame=g_tracker.LastCommandFrame();
        g_tracker.Reset(); g_tracker.ObserveCommand(cmd,frame);
        g_recentCount=g_recentNext=0;
        g_waitingFall=true; g_waitStart=I::GlobalVars->realtime;
        U::LogInfo("[PortalBsp][Command] command=portal_diag_nativefall armed=true trigger=airborne-descending timeoutSeconds=30 mutation=false.\n");
    }
    void RenderStart(bool beforeOriginal)
    {
        if (!Enabled())
        {
            const bool retained=ErrorPolicy::NeedsLifecycleReset(false,reinterpret_cast<std::uintptr_t>(g_observedPlayer),0,g_errorRecentCount!=0);
            g_observedPlayer=nullptr;g_insideRender=false;
            if (g_tracker.Active() || g_waitingFall || retained) Reset("diagnostics-disabled");
            return;
        }
        auto* player=Local();
        if (!player)
        {
            const bool retained=ErrorPolicy::NeedsLifecycleReset(true,reinterpret_cast<std::uintptr_t>(g_observedPlayer),0,g_errorRecentCount!=0);
            g_observedPlayer=nullptr;g_insideRender=false;
            if (g_tracker.Active() || g_waitingFall || retained) Reset("local-unavailable");
            return;
        }
        if(ErrorPolicy::NeedsLifecycleReset(true,reinterpret_cast<std::uintptr_t>(g_observedPlayer),reinterpret_cast<std::uintptr_t>(player),g_errorRecentCount!=0)) Reset("local-identity-changed");
        PrepareFrame();
        g_observedPlayer=player;
        g_insideRender=beforeOriginal;
        if(beforeOriginal)
        {
            g_frame.nativeStatus=PortalNativeInterpolationHook::Inspect(player);
            g_frame.errorStatus=PortalNativePredictionErrorHook::Inspect();
        }
        g_frame.stages[beforeOriginal ? BeforeRender : AfterRender]=Read(player);
        if (!beforeOriginal && g_waitingFall)
        {
            if (I::GlobalVars->realtime-g_waitStart>30.0f)
            {
                g_waitingFall=false;
                U::LogInfo("[PortalBsp][Command] command=portal_diag_nativefall armed=false reason=wait-timeout.\n");
            }
            else if (player->m_MoveType()==MOVETYPE_WALK
                && !(player->m_fFlags() & FL_ONGROUND) && player->m_vecVelocity().z < -1.0f)
                Begin(0,g_tracker.LastCommand(),"normal-fall");
        }
    }
    int InterpolationBegin(C_BasePlayer* player,float,Vector& before)
    {
        if(!player||player!=g_observedPlayer||!Enabled()) return -1;
        PrepareFrame();
        if(g_frame.stages[SubmittedView].valid) return -1;
        before=player->GetAbsOrigin();return g_frame.frame;
    }
    void InterpolationEnd(C_BasePlayer* player,float time,const Vector& before,bool result,int frame)
    {
        if(!Enabled()||player!=g_observedPlayer||frame!=I::GlobalVars->framecount||frame!=g_frame.frame) return;
        const Vector after=player->GetAbsOrigin();
        g_frame.nativeCalls.Add(time,I::GlobalVars->curtime,Point(before),Point(after),result,g_insideRender);
    }
    bool Part1Begin(C_BasePlayer* player,int frame,PortalNativePart1Policy::Observation& sample)
    {
        if(!Enabled() || !player || player!=g_observedPlayer || frame!=g_frame.frame
            || frame!=I::GlobalVars->framecount || g_frame.stages[SubmittedView].valid) return false;
        sample.recvBeforeOrigin=Point(player->m_vecOrigin());
        sample.recvBeforeAngles=Point(player->m_angRotation());
        return true;
    }
    void Part1End(C_BasePlayer* player,int frame,PortalNativePart1Policy::Observation& sample)
    {
        if(!Enabled() || player!=g_observedPlayer || frame!=g_frame.frame || frame!=I::GlobalVars->framecount) return;
        // Recv accessors are NOT the native-local Part1/Part2 fields.
        sample.recvAfterOrigin=Point(player->m_vecOrigin());
        sample.recvAfterAngles=Point(player->m_angRotation());
        g_frame.part1Calls.Add(sample);
    }
    void CalcView(C_BasePlayer* player, const Vector& eye, bool beforeAdapter)
    {
        if (!Enabled() || !player || player!=Local()) return;
        PrepareFrame();
        // First main-frame call wins; never substitute a later recursive call.
        auto& target=g_frame.stages[beforeAdapter ? OriginalView : AdaptedView];
        if (!target.valid) target=Read(player,&eye);
    }
    void FinalView(const CViewSetup& view)
    {
        if (!Enabled()) return;
        auto* player=Local(); if (!player) return;
        PrepareFrame();
        if (g_frame.stages[SubmittedView].valid) return;
        g_frame.stages[SubmittedView]=Read(player,&view.origin);
        g_frame.inputCommand=g_tracker.LastCommand(); g_frame.inputFrame=g_tracker.LastCommandFrame();
        g_frame.roll=view.angles.z;
        const auto& final=g_frame.stages[SubmittedView];
        const auto& original=g_frame.stages[OriginalView];
        PortalNativeCadence::Sample sample;
        sample.frame=final.frame; sample.tick=final.tick; sample.realTime=I::GlobalVars->realtime;
        sample.frameTime=final.frameTime; sample.speed=final.velocity.Lenght();
        sample.originalValid=original.valid && original.frame==final.frame;
        sample.originalEye={original.eye.x,original.eye.y,original.eye.z};
        sample.finalEye={view.origin.x,view.origin.y,view.origin.z};
        sample.onGround=(final.flags & FL_ONGROUND)!=0; sample.rollActive=std::fabs(view.angles.z)>.001f;
        sample.eyeStateValid=g_frame.eyeStateValid; sample.eyeHistory=g_frame.eyeHistory;
        sample.eyeReleased=g_frame.eyeReleased; sample.shadow=g_frame.shadow;
        const auto& native=g_frame.nativeCalls;
        const bool observed=g_frame.nativeStatus.installed&&g_frame.nativeStatus.dispatchValid
            &&g_frame.stages[BeforeRender].valid&&g_frame.stages[AfterRender].valid;
        const bool afterComparable=native.validInsideRender>0&&g_frame.stages[AfterRender].valid;
        const float difference=afterComparable
            ? PortalNativeInterpolationPolicy::Distance(native.lastRenderAfter,Point(g_frame.stages[AfterRender].origin)) : 0;
        sample.nativeClass=PortalNativeInterpolationPolicy::Classify(native,g_frame.nativeStatus.installed,
            g_frame.nativeStatus.dispatchValid,observed,afterComparable,difference);
        sample.nativeClass |= PortalNativePart1Policy::Classify(g_frame.part1Calls,g_frame.nativeStatus.part1Installed)<<12;
        sample.mapClass=PortalNativePart1Policy::MapClass(g_frame.part1Calls);
        g_frame.part1TimeRelation=g_tracker.Active()
            ? g_part1Window.CompareTime(g_frame.part1Calls,observed&&g_frame.nativeStatus.part1Installed) : 0;
        sample.nativeClass |= g_frame.part1TimeRelation<<23;
        const auto result=g_tracker.Record(sample);
        if(result.accepted)
        {
            g_nativeWindow.Add(native,observed,afterComparable,difference,result.originalFrozen,result.finalFrozen);
            g_part1Window.Add(g_frame.part1Calls,observed&&g_frame.nativeStatus.part1Installed,native.count);
        }
        if (result.emit) PrintFrame(g_frame,result,false);
        if (result.completed) Summary("capture-complete");
        // Four tick-boundary frames, not every render frame, form the prelude.
        const auto& last=g_recent[(g_recentNext+g_recent.size()-1)%g_recent.size()];
        if (!g_recentCount || last.stages[SubmittedView].tick!=final.tick)
        {
            g_recent[g_recentNext]=g_frame; g_recentNext=(g_recentNext+1)%g_recent.size();
            g_recentCount=std::min<unsigned>(g_recentCount+1,static_cast<unsigned>(g_recent.size()));
        }
    }
    void Movement(const char* stage, C_BasePlayer* player, CUserCmd* cmd, CMoveData* move)
    {
        if (!Enabled() || !g_tracker.Active() || !player || !cmd || player!=Local() || !stage) return;
        const auto fallPhase=PortalNativeFallPolicy::ClientCommandPhase(stage);
        if(fallPhase!=UINT32_MAX)FallState(fallPhase,PortalNativeFallProbe::Client(player),cmd,g_capture);
        if (std::strcmp(stage,"processmovement-post-original") && std::strcmp(stage,"finishmove-post-original")) return;
        const auto s=Read(player);
        if(!std::strcmp(stage,"processmovement-post-original"))
        {g_processCommand=cmd->command_number;g_processSnapshot=s;return;}
        ++g_commandSamples;
        const bool replay=cmd->command_number<=g_lastMovementCommand;
        const Vector moveOrigin=move ? move->GetAbsOrigin() : Vector();
        const Vector moveVelocity=move ? move->m_vecVelocity : Vector();
        const float error=move ? PortalNativeInterpolationPolicy::Distance(Point(s.origin),Point(moveOrigin)) : 0;
        const float velocityError=move ? PortalNativeInterpolationPolicy::Distance(Point(s.velocity),Point(moveVelocity)) : 0;
        const bool anomaly=!g_lastFinish.valid||!move||g_processCommand!=cmd->command_number||replay
            ||cmd->command_number!=g_lastMovementCommand+1||s.flags!=g_lastFinish.flags||s.moveType!=g_lastFinish.moveType
            ||s.effects!=g_lastFinish.effects||!std::isfinite(error)||!std::isfinite(velocityError)||error>.01f||velocityError>.01f
            ||!PortalNativeInterpolationPolicy::Finite(Point(s.velocity))
            ||PortalNativeInterpolationPolicy::Distance(Point(s.velocity),Point(g_lastFinish.velocity))>150;
        if(cmd->command_number>g_lastMovementCommand) g_lastMovementCommand=cmd->command_number;
        g_lastFinish=s;
        if(!anomaly)
        {
            ++g_commandBatched;g_commandBatch.Add(cmd->command_number,s.velocity.Lenght());
            if(g_commandBatch.Ready()) FlushCommands();
            return;
        }
        ++g_commandAnomalies;FlushCommands();
        if(!g_commandBudget.Take()) return;
        U::LogInfo("[PortalNativeCadenceCommand] capture=%u txn=%u stage=%s cmd=%d cmdTick=%d replayOrSameCmd=%s hasBeenPredicted=%s moveValid=%s moveOrigin=(%.3f %.3f %.3f) moveVelocity=(%.3f %.3f %.3f) behaviorMutation=false.\n",
            g_capture,g_transaction,stage,cmd->command_number,cmd->tick_count,Yes(replay),Yes(cmd->hasbeenpredicted),Yes(move!=nullptr),
            moveOrigin.x,moveOrigin.y,moveOrigin.z,moveVelocity.x,moveVelocity.y,moveVelocity.z);
        PrintStage(s,stage,false);
        if(g_processCommand==cmd->command_number) PrintStage(g_processSnapshot,"processmovement-post-original",false);
    }
    void EyeState(bool valid, bool active, bool released, bool shadow)
    {
        if (!Enabled()) return;
        PrepareFrame();
        // The caller is the main-view submission, after the guarded CalcView.
        if (g_frame.eyeStateValid || !g_frame.stages[OriginalView].valid) return;
        g_frame.eyeStateValid=valid; g_frame.eyeHistory=active;
        g_frame.eyeReleased=released; g_frame.shadow=shadow;
    }
    bool AllowFloorWallCommand(std::uint32_t transaction, bool server)
    {
        if (!Enabled() || !g_tracker.Active() || !transaction || transaction!=g_transaction) return false;
        return (server ? g_floorServerBudget : g_floorClientBudget).Take();
    }
    std::uint32_t FallCapture(){return Enabled()&&g_tracker.Active()&&Local()?g_capture:0;}
    void FallMovement(bool server,bool fullWalk,void* gameMovement,const CUserCmd* cmd)
    {
        const auto capture=FallCapture();
        if(!capture||!cmd)return;
        FallState((server?4u:6u)+(fullWalk?1u:0u),PortalNativeFallProbe::Movement(server,gameMovement,Local()),cmd,capture);
    }
    void FallState(unsigned phase,const PortalNativeFallPolicy::Sample& s,const CUserCmd* cmd,std::uint32_t capture)
    {
        if(!capture||capture!=FallCapture()||phase>=g_fallWindows.size()||!cmd)return;
        if(!g_fallWindows[phase].Add(s))return; // all counters precede detail budget
        U::LogInfo("[PortalNativeFallState] capture=%u txn=%u phase=%s cmd=%d cmdTick=%d clientClock(frame=%d tick=%d) state(fallValid=%s fall=%.9g velocityZValid=%s velocityZ=%.9g groundValid=%s ground=0x%X flagsValid=%s flags=0x%X moveTypeValid=%s moveType=%d tickBaseValid=%s tickBase=%d) movementMatched=%s moveVelocityZValid=%s moveVelocityZ=%.9g instantaneousFallPlusZ=%.9g instantaneousIsPredictionError=false nativeComparisonSlot=not-read fieldWrites=false behaviorMutation=false.\n",
            capture,g_transaction,kFallPhases[phase],cmd->command_number,cmd->tick_count,I::GlobalVars->framecount,I::GlobalVars->tickcount,
            Yes(s.fallValid),s.fall,Yes(s.zValid),s.z,Yes(s.groundValid),s.ground,Yes(s.flagsValid),s.flags,Yes(s.moveTypeValid),s.moveType,Yes(s.tickBaseValid),s.tickBase,
            Yes(s.movementMatched),Yes(s.moveZValid),s.moveZ,s.fallValid&&s.zValid?s.fall+s.z:NAN);
    }
}
