#include "CCSGameMovement.h"
#include "../ClientPrediction/ClientPrediction.h"
#include "../ServerPlayer/ServerPlayerRunCommand.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalPlayerTeleport.h"
#include "../../Portal/PortalTransitionDecision.h"
#include "../../Portal/PortalNativeDiagnostics.h"
#include "../../Portal/PortalFallWrite.h"
#include "../../Util/Logger/Logger.h"
#include "../../Util/Logger/PortalFileLog.h"
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Hooks;

namespace
{
	const char* PortalPhaseName(PortalTransitionPhase phase)
	{
		switch (phase)
		{
		case PortalTransitionPhase::Idle: return "Idle";
		case PortalTransitionPhase::ApproachingPortal: return "ApproachingPortal";
		case PortalTransitionPhase::IntersectingPortal: return "IntersectingPortal";
		case PortalTransitionPhase::CommittingTeleport: return "CommittingTeleport";
		case PortalTransitionPhase::ExitingPortal: return "ExitingPortal";
		case PortalTransitionPhase::Cooldown: return "Cooldown";
		default: return "Unknown";
		}
	}

	const char* PortalSideName(PortalTransitionSide side)
	{
		switch (side)
		{
		case PortalTransitionSide::Blue: return "Blue";
		case PortalTransitionSide::Orange: return "Orange";
		case PortalTransitionSide::None:
		default:
			return "None";
		}
	}

	const char* BoolText(bool value)
	{
		return value ? "true" : "false";
	}

	uintptr_t ModuleRelativeAddress(const char* moduleName, const void* address)
	{
		const HMODULE module = GetModuleHandleA(moduleName);
		if (!module || !address)
			return 0u;

		const uintptr_t absolute = reinterpret_cast<uintptr_t>(address);
		const uintptr_t base = reinterpret_cast<uintptr_t>(module);
		return absolute >= base ? absolute - base : 0u;
	}

	CMoveData* TryGetMoveDataFromGameMovement(void* gameMovement);

	struct PredictionCommitMovementDiagnostics
	{
		bool active = false;
		int commandNumber = 0;
		std::uint32_t transactionId = 0;
		PortalTransitionSide entrySide = PortalTransitionSide::None;
		PortalTransitionSide exitSide = PortalTransitionSide::None;
		C_BasePlayer* player = nullptr;
		PortalMovementCommandEndDiagnostic commandEnd;
		bool hasCommandEnd = false;
		unsigned int traceCount = 0;
		unsigned int hitCount = 0;
		unsigned int startSolidCount = 0;
		unsigned int allSolidCount = 0;
		unsigned int zeroLengthCount = 0;
		unsigned int horizontalCount = 0;
		unsigned int verticalGroundCount = 0;
		unsigned int stepCount = 0;
		unsigned int otherCount = 0;
		float minimumHitFraction = 1.0f;
	};

	thread_local PredictionCommitMovementDiagnostics g_predictionCommitMovementDiagnostics;

	CMoveData* TryGetMoveDataFromGameMovement(void* gameMovement)
	{
		if (!gameMovement)
			return nullptr;

		// Official CGameMovement layout has the vptr, then player, then mv.
		// Treat this as diagnostic-only until verified against L4D2.
		return *reinterpret_cast<CMoveData**>(reinterpret_cast<uintptr_t>(gameMovement) + (sizeof(void*) * 2u));
	}

	void LogServerMoveData(const char* tag, void* gameMovement, const CMoveData* move)
	{
		if (!move)
		{
			U::LogWarning("[PortalBridge][ServerMove][%s] gm=%p mv=null.\n", tag, gameMovement);
			return;
		}

		const Vector& origin = move->GetAbsOrigin();
		const Vector& velocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][ServerMove][%s] gm=%p mv=%p origin=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f) buttons=0x%X fmove=%.1f smove=%.1f gameCodeMoved=%s.\n",
			tag,
			gameMovement,
			move,
			origin.x, origin.y, origin.z,
			velocity.x, velocity.y, velocity.z,
			move->m_nButtons,
			move->m_flForwardMove,
			move->m_flSideMove,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

	bool ShouldLogServerMovement()
	{
		return false;
	}

	bool ShouldLogMovementHeartbeat(uint32_t counter)
	{
		(void)counter;
		return false;
	}

	bool ShouldLogFocusedHookProbe(uint32_t heartbeat)
	{
		return heartbeat <= 3u
			|| (heartbeat % 180u) == 0u
			|| G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()
			|| G::G_L4D2Portal.m_PortalTransition.IsControlledNoclipActive();
	}

	void LogFocusedHookProbe(const char* domain, const char* stage, const char* point, uint32_t heartbeat, void* gameMovement)
	{
		if (!ShouldLogFocusedHookProbe(heartbeat))
			return;

		G::G_L4D2Portal.m_PortalTransition.LogHookProbe(
			domain,
			stage,
			point,
			gameMovement,
			TryGetMoveDataFromGameMovement(gameMovement));
	}

	void LogTryPlayerMoveEnter(const char* domain, uint32_t heartbeat, void* gameMovement, Vector* pFirstDest, trace_t* pFirstTrace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		U::LogWarning("[PortalBridge][%s][TryPlayerMove][Enter] heartbeat=%u detailed=%s cmd=%d gm=%p firstDest=%p firstTrace=%p dest=(%.1f %.1f %.1f).\n",
			domain,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			gameMovement,
			pFirstDest,
			pFirstTrace,
			pFirstDest ? pFirstDest->x : 0.0f,
			pFirstDest ? pFirstDest->y : 0.0f,
			pFirstDest ? pFirstDest->z : 0.0f);
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogTryPlayerMoveExit(const char* domain, uint32_t heartbeat, int result, void* gameMovement, Vector* pFirstDest, trace_t* pFirstTrace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		U::LogWarning("[PortalBridge][%s][TryPlayerMove][Exit] heartbeat=%u detailed=%s result=%d firstDest=%p firstTrace=%p traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			heartbeat,
			BoolText(detailed),
			result,
			pFirstDest,
			pFirstTrace,
			pFirstTrace ? pFirstTrace->endpos.x : 0.0f,
			pFirstTrace ? pFirstTrace->endpos.y : 0.0f,
			pFirstTrace ? pFirstTrace->endpos.z : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.x : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.y : 0.0f,
			pFirstTrace ? pFirstTrace->plane.normal.z : 0.0f,
			pFirstTrace ? pFirstTrace->fraction : 0.0f,
			BoolText(pFirstTrace ? pFirstTrace->startsolid : false),
			BoolText(pFirstTrace ? pFirstTrace->allsolid : false));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogStepMoveEnter(const char* domain, uint32_t heartbeat, void* gameMovement, Vector& vecDestination, trace_t& trace, CMoveData* move, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		U::LogWarning("[PortalBridge][%s][StepMove][Enter] heartbeat=%u detailed=%s cmd=%d gm=%p dest=(%.1f %.1f %.1f) traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			gameMovement,
			vecDestination.x, vecDestination.y, vecDestination.z,
			trace.endpos.x, trace.endpos.y, trace.endpos.z,
			trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z,
			trace.fraction,
			BoolText(trace.startsolid),
			BoolText(trace.allsolid));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogStepMoveExit(const char* domain, void* gameMovement, Vector& vecDestination, trace_t& trace, CMoveData* move, bool detailed)
	{
		if (!detailed)
			return;

		U::LogWarning("[PortalBridge][%s][StepMove][Exit] gm=%p dest=(%.1f %.1f %.1f) traceEnd=(%.1f %.1f %.1f) tracePlane=(%.2f %.2f %.2f) traceFrac=%.3f startsolid=%s allsolid=%s.\n",
			domain,
			gameMovement,
			vecDestination.x, vecDestination.y, vecDestination.z,
			trace.endpos.x, trace.endpos.y, trace.endpos.z,
			trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z,
			trace.fraction,
			BoolText(trace.startsolid),
			BoolText(trace.allsolid));
		LogServerMoveData(domain, gameMovement, move);
	}

	void LogMovementStageSnapshot(const char* domain, const char* stage, const char* point, uint32_t heartbeat, void* gameMovement, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
		{
			U::LogWarning("[PortalBridge][StageProbe][%s][%s][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s gm=%p mv=null.\n",
				domain,
				stage,
				point,
				heartbeat,
				BoolText(detailed),
				diag.frameCommandNumber,
				PortalPhaseName(diag.lastPhase),
				PortalSideName(diag.lastEntrySide),
				gameMovement);
			return;
		}

		const Vector& origin = move->GetAbsOrigin();
		const Vector& velocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][StageProbe][%s][%s][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s bridge=%s gm=%p mv=%p origin=(%.1f %.1f %.1f) vel=(%.1f %.1f %.1f) buttons=0x%X fmove=%.1f smove=%.1f step=%.2f gameMoved=%s.\n",
			domain,
			stage,
			point,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			PortalPhaseName(diag.lastPhase),
			PortalSideName(diag.lastEntrySide),
			BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
			gameMovement,
			move,
			origin.x, origin.y, origin.z,
			velocity.x, velocity.y, velocity.z,
			move->m_nButtons,
			move->m_flForwardMove,
			move->m_flSideMove,
			move->m_outStepHeight,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

	PortalTraceClass ClassifyPredictionMovementTrace(
		const Vector& start,
		const Vector& end)
	{
		constexpr float kMaxVerticalTraceZ = 8.0f;
		const Vector delta = end - start;
		const float horizontalSqr = delta.x * delta.x + delta.y * delta.y;
		const float verticalAbs = std::fabs(delta.z);

		if (horizontalSqr < 0.0001f && verticalAbs < 0.0001f)
			return PortalTraceClass::ZeroLengthPositionTest;
		if (horizontalSqr < 0.25f && verticalAbs > 0.5f
			&& delta.z < 0.0f && verticalAbs <= 4.0f)
		{
			return PortalTraceClass::VerticalGroundProbe;
		}
		if (horizontalSqr < 16.0f && verticalAbs > kMaxVerticalTraceZ)
			return PortalTraceClass::StepUpDownProbe;
		if (horizontalSqr >= 0.25f && verticalAbs <= kMaxVerticalTraceZ)
			return PortalTraceClass::HorizontalMove;
		return PortalTraceClass::Other;
	}

	bool BeginPredictionCommitMovementDiagnostics(
		CUserCmd* command,
		C_BasePlayer* player)
	{
		g_predictionCommitMovementDiagnostics = {};
		if (!PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled()
			|| !command)
		{
			return false;
		}

		Vector committedOrigin;
		Vector committedVelocity;
		QAngle committedAngles;
		int commandDelta = 0;
		std::uint32_t transactionId = 0;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForDiagnostic(
			command->command_number,
			&committedOrigin,
			&committedVelocity,
			&committedAngles,
			&commandDelta,
			&transactionId)
			|| commandDelta != 0)
		{
			return false;
		}

		const PortalTransitionContext& context =
			G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		g_predictionCommitMovementDiagnostics.active = true;
		g_predictionCommitMovementDiagnostics.commandNumber = command->command_number;
		g_predictionCommitMovementDiagnostics.transactionId = transactionId;
		g_predictionCommitMovementDiagnostics.entrySide = context.entrySide;
		g_predictionCommitMovementDiagnostics.exitSide = context.exitSide;
		g_predictionCommitMovementDiagnostics.player = player;
		g_predictionCommitMovementDiagnostics.hasCommandEnd =
			G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetMovementCommandEndDiagnostic(
				command->command_number,
				&g_predictionCommitMovementDiagnostics.commandEnd)
			&& g_predictionCommitMovementDiagnostics.commandEnd.transactionId == transactionId;
		return true;
	}

	struct HullOccupancyProbe
	{
		float fraction = 1.0f;
		bool startSolid = false;
		bool allSolid = false;
		int contents = 0;
	};

	void InitDiagnosticHullRay(
		Ray_t& ray,
		const Vector& origin,
		const Vector& mins,
		const Vector& maxs)
	{
		ray.m_Delta.Init();
		ray.m_IsSwept = false;
		ray.m_Extents = (maxs - mins) * 0.5f;
		ray.m_IsRay = ray.m_Extents.LenghtSqr() < 1e-6f;
		const Vector center = (mins + maxs) * 0.5f;
		ray.m_Start = origin + center;
		ray.m_StartOffset = center * -1.0f;
		ray.m_pWorldAxisTransform = nullptr;
	}

	HullOccupancyProbe ProbeWorldHull(
		const Vector& origin,
		const Vector& mins,
		const Vector& maxs)
	{
		HullOccupancyProbe result;
		if (!I::EngineTrace)
			return result;

		Ray_t ray;
		InitDiagnosticHullRay(ray, origin, mins, maxs);
		CTraceFilterWorldAndPropsOnly filter;
		trace_t trace;
		std::memset(&trace, 0, sizeof(trace));
		trace.fraction = 1.0f;
		I::EngineTrace->TraceRay(
			ray,
			MASK_PLAYERSOLID,
			&filter,
			&trace);
		result.fraction = trace.fraction;
		result.startSolid = trace.startsolid;
		result.allSolid = trace.allsolid;
		result.contents = trace.contents;
		return result;
	}

	void LogPredictionCommitHullOccupancy(
		C_BasePlayer* player,
		CMoveData* move)
	{
		const PredictionCommitMovementDiagnostics& diagnostics =
			g_predictionCommitMovementDiagnostics;
		if (!diagnostics.active || !player || !move)
			return;

		const Vector origin = move->GetAbsOrigin();
		const Vector mins = player->GetPlayerMins();
		const Vector maxs = player->GetPlayerMaxs();
		const HullOccupancyProbe exact = ProbeWorldHull(origin, mins, maxs);

		U::LogInfo("[PortalPredictionMovementHull] txn=%u cmd=%d entry=%s exit=%s behaviorMutation=false origin=(%.3f %.3f %.3f) hullMins=(%.2f %.2f %.2f) hullMaxs=(%.2f %.2f %.2f) exact=(frac=%.3f startsolid=%s allsolid=%s contents=0x%X).\n",
			diagnostics.transactionId,
			diagnostics.commandNumber,
			PortalSideName(diagnostics.entrySide),
			PortalSideName(diagnostics.exitSide),
			origin.x, origin.y, origin.z,
			mins.x, mins.y, mins.z,
			maxs.x, maxs.y, maxs.z,
			exact.fraction, BoolText(exact.startSolid), BoolText(exact.allSolid), exact.contents);
	}

	void RecordPredictionCommitMovementTrace(
		const Vector& start,
		const Vector& end,
		const trace_t* trace)
	{
		PredictionCommitMovementDiagnostics& diagnostics =
			g_predictionCommitMovementDiagnostics;
		if (!diagnostics.active || !trace)
			return;

		const PortalTraceClass traceClass =
			ClassifyPredictionMovementTrace(start, end);
		++diagnostics.traceCount;
		switch (traceClass)
		{
		case PortalTraceClass::ZeroLengthPositionTest: ++diagnostics.zeroLengthCount; break;
		case PortalTraceClass::HorizontalMove: ++diagnostics.horizontalCount; break;
		case PortalTraceClass::VerticalGroundProbe: ++diagnostics.verticalGroundCount; break;
		case PortalTraceClass::StepUpDownProbe: ++diagnostics.stepCount; break;
		case PortalTraceClass::Other:
		default: ++diagnostics.otherCount; break;
		}

		const bool hit = trace->fraction < 1.0f || trace->startsolid || trace->allsolid;
		if (hit)
		{
			++diagnostics.hitCount;
			diagnostics.minimumHitFraction =
				std::min(diagnostics.minimumHitFraction, trace->fraction);
		}
		if (trace->startsolid)
			++diagnostics.startSolidCount;
		if (trace->allsolid)
			++diagnostics.allSolidCount;
	}

	void LogPredictionCommitMovementSummary(
		const Vector& originBefore,
		const Vector& velocityBefore,
		CMoveData* move)
	{
		const PredictionCommitMovementDiagnostics& diagnostics =
			g_predictionCommitMovementDiagnostics;
		if (!diagnostics.active || !move)
			return;

		const Vector originAfter = move->GetAbsOrigin();
		const Vector velocityAfter = move->m_vecVelocity;
		const float speedBefore = std::sqrt(
			velocityBefore.x * velocityBefore.x
			+ velocityBefore.y * velocityBefore.y);
		const float speedAfter = std::sqrt(
			velocityAfter.x * velocityAfter.x
			+ velocityAfter.y * velocityAfter.y);
		U::LogInfo("[PortalPredictionMovementSummary] txn=%u cmd=%d entry=%s exit=%s behaviorMutation=false origin=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) originDelta=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) speed2D=%.3f->%.3f speedDrop=%.3f traces=(total=%u hit=%u startsolid=%u allsolid=%u zero=%u horizontal=%u verticalGround=%u step=%u other=%u minHitFraction=%.6f).\n",
			diagnostics.transactionId,
			diagnostics.commandNumber,
			PortalSideName(diagnostics.entrySide),
			PortalSideName(diagnostics.exitSide),
			originBefore.x, originBefore.y, originBefore.z,
			originAfter.x, originAfter.y, originAfter.z,
			originAfter.x - originBefore.x,
			originAfter.y - originBefore.y,
			originAfter.z - originBefore.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			velocityAfter.x, velocityAfter.y, velocityAfter.z,
			speedBefore,
			speedAfter,
			speedBefore - speedAfter,
			diagnostics.traceCount,
			diagnostics.hitCount,
			diagnostics.startSolidCount,
			diagnostics.allSolidCount,
			diagnostics.zeroLengthCount,
			diagnostics.horizontalCount,
			diagnostics.verticalGroundCount,
			diagnostics.stepCount,
			diagnostics.otherCount,
			diagnostics.minimumHitFraction);
	}

	float SignedPlaneDistance(
		const Vector& point,
		const Vector& planeOrigin,
		const Vector& planeNormal)
	{
		return (point - planeOrigin).Dot(planeNormal);
	}

	void LogOfficialCommandEndCandidate(
		const Vector& processOriginBefore,
		const Vector& processVelocityBefore,
		CMoveData* move)
	{
		const PredictionCommitMovementDiagnostics& diagnostics =
			g_predictionCommitMovementDiagnostics;
		if (!diagnostics.active || !move)
			return;

		if (!diagnostics.hasCommandEnd)
		{
			U::LogInfo("[PortalOfficialCommandEndCandidate] txn=%u cmd=%d entry=%s exit=%s matched=false reason=missing-transaction-snapshot behaviorMutation=false.\n",
				diagnostics.transactionId,
				diagnostics.commandNumber,
				PortalSideName(diagnostics.entrySide),
				PortalSideName(diagnostics.exitSide));
			return;
		}

		const PortalMovementCommandEndDiagnostic& candidate = diagnostics.commandEnd;
		const Vector processOriginAfter = move->GetAbsOrigin();
		const Vector processVelocityAfter = move->m_vecVelocity;
		const Vector processDelta = processOriginAfter - processOriginBefore;
		const float remainingRatio = candidate.commandInterval > 0.0f
			&& candidate.postPortalCommandTime >= 0.0f
			? std::max(0.0f, std::min(
				candidate.postPortalCommandTime / candidate.commandInterval,
				1.0f))
			: 0.0f;
		const Vector remainingTimeEstimate =
			processOriginBefore + processDelta * remainingRatio;
		const Vector processCenterBefore =
			processOriginBefore - candidate.originFromCenter;
		const Vector processCenterAfter =
			processOriginAfter - candidate.originFromCenter;
		const Vector officialExitCenter =
			candidate.officialExitCommandEndOrigin - candidate.originFromCenter;
		const Vector committedToOfficial =
			candidate.committedExitOrigin - candidate.officialExitCommandEndOrigin;
		const Vector processBeforeToOfficial =
			processOriginBefore - candidate.officialExitCommandEndOrigin;
		const Vector processAfterToOfficial =
			processOriginAfter - candidate.officialExitCommandEndOrigin;
		const Vector remainingEstimateToOfficial =
			remainingTimeEstimate - candidate.officialExitCommandEndOrigin;

		U::LogInfo("[PortalOfficialCommandEndCandidate] txn=%u cmd=%d entry=%s exit=%s matched=true path=%s diagnosticModel=official-handleportalling-command-end behaviorMutation=false onGround=%s crossesInsideCommand=%s interval=%.6f projectedLeadTime=%.6f planeCrossingTime=%.6f postPortalCommandTime=%.6f remainingRatio=%.6f observedEntryOrigin=(%.3f %.3f %.3f) observedEntryCenter=(%.3f %.3f %.3f) linearEntryCommandEndOrigin=(%.3f %.3f %.3f) linearEntryCommandEndCenter=(%.3f %.3f %.3f) entryDepth(observed=%.3f commandEnd=%.3f) committedExitOrigin=(%.3f %.3f %.3f) officialExitCommandEndOrigin=(%.3f %.3f %.3f) processOrigin=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) processDelta=(%.3f %.3f %.3f) exitDepth(committed=%.3f official=%.3f processBefore=%.3f processAfter=%.3f) distanceToOfficial(committed=%.3f processBefore=%.3f processAfter=%.3f remainingEstimate=%.3f) remainingTimeEstimate=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) processStartsAtCommitted=%s.\n",
			candidate.transactionId,
			candidate.commandNumber,
			PortalSideName(candidate.entrySide),
			PortalSideName(candidate.exitSide),
			candidate.projectedPath ? "projected" : "live",
			BoolText(candidate.wasOnGround),
			BoolText(candidate.crossesInsideCommand),
			candidate.commandInterval,
			candidate.projectedLeadTime,
			candidate.planeCrossingTime,
			candidate.postPortalCommandTime,
			remainingRatio,
			candidate.observedEntryOrigin.x, candidate.observedEntryOrigin.y, candidate.observedEntryOrigin.z,
			candidate.observedEntryCenter.x, candidate.observedEntryCenter.y, candidate.observedEntryCenter.z,
			candidate.linearEntryCommandEndOrigin.x, candidate.linearEntryCommandEndOrigin.y, candidate.linearEntryCommandEndOrigin.z,
			candidate.linearEntryCommandEndCenter.x, candidate.linearEntryCommandEndCenter.y, candidate.linearEntryCommandEndCenter.z,
			SignedPlaneDistance(candidate.observedEntryCenter, candidate.entryPlaneOrigin, candidate.entryPlaneNormal),
			SignedPlaneDistance(candidate.linearEntryCommandEndCenter, candidate.entryPlaneOrigin, candidate.entryPlaneNormal),
			candidate.committedExitOrigin.x, candidate.committedExitOrigin.y, candidate.committedExitOrigin.z,
			candidate.officialExitCommandEndOrigin.x, candidate.officialExitCommandEndOrigin.y, candidate.officialExitCommandEndOrigin.z,
			processOriginBefore.x, processOriginBefore.y, processOriginBefore.z,
			processOriginAfter.x, processOriginAfter.y, processOriginAfter.z,
			processDelta.x, processDelta.y, processDelta.z,
			SignedPlaneDistance(candidate.committedExitOrigin - candidate.originFromCenter, candidate.exitPlaneOrigin, candidate.exitPlaneNormal),
			SignedPlaneDistance(officialExitCenter, candidate.exitPlaneOrigin, candidate.exitPlaneNormal),
			SignedPlaneDistance(processCenterBefore, candidate.exitPlaneOrigin, candidate.exitPlaneNormal),
			SignedPlaneDistance(processCenterAfter, candidate.exitPlaneOrigin, candidate.exitPlaneNormal),
			std::sqrt(committedToOfficial.LenghtSqr()),
			std::sqrt(processBeforeToOfficial.LenghtSqr()),
			std::sqrt(processAfterToOfficial.LenghtSqr()),
			std::sqrt(remainingEstimateToOfficial.LenghtSqr()),
			remainingTimeEstimate.x, remainingTimeEstimate.y, remainingTimeEstimate.z,
			processVelocityBefore.x, processVelocityBefore.y, processVelocityBefore.z,
			processVelocityAfter.x, processVelocityAfter.y, processVelocityAfter.z,
			BoolText((processOriginBefore - candidate.committedExitOrigin).LenghtSqr() <= 0.0625f));
	}

	void RecordAndLogOfficialActualCommandEnd(CMoveData* move)
	{
		const PredictionCommitMovementDiagnostics& diagnostics =
			g_predictionCommitMovementDiagnostics;
		if (!diagnostics.active || !diagnostics.hasCommandEnd || !move)
			return;

		PortalMovementCommandEndDiagnostic actual;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.RecordClientMovementCommandEndDiagnostic(
			diagnostics.commandNumber,
			move->GetAbsOrigin(),
			move->m_vecVelocity,
			move->m_vecViewAngles,
			&actual))
		{
			U::LogInfo("[PortalOfficialActualCommandEnd] txn=%u cmd=%d matched=false reason=record-rejected behaviorMutation=false.\n",
				diagnostics.transactionId,
				diagnostics.commandNumber);
			return;
		}

		const Vector entryLinearDelta =
			actual.actualEntryCommandEndOrigin - actual.linearEntryCommandEndOrigin;
		const Vector exitOfficialDelta =
			actual.actualExitCommandEndOrigin - actual.officialExitCommandEndOrigin;
		const Vector exitCommittedDelta =
			actual.actualExitCommandEndOrigin - actual.committedExitOrigin;
		const Vector exitCenter =
			actual.actualExitCommandEndOrigin - actual.originFromCenter;

		U::LogInfo("[PortalOfficialActualCommandEnd] txn=%u cmd=%d entry=%s exit=%s matched=true path=%s onGround=%s diagnosticModel=transform-client-processmovement-result behaviorMutation=false actualEntryOrigin=(%.3f %.3f %.3f) linearEntryOrigin=(%.3f %.3f %.3f) entryLinearDelta=(%.3f %.3f %.3f) entryLinearDistance=%.3f actualExitOrigin=(%.3f %.3f %.3f) officialExitOrigin=(%.3f %.3f %.3f) exitOfficialDelta=(%.3f %.3f %.3f) exitOfficialDistance=%.3f committedExitOrigin=(%.3f %.3f %.3f) exitCommittedDelta=(%.3f %.3f %.3f) exitCommittedDistance=%.3f exitDepth=%.3f velocityEntry=(%.3f %.3f %.3f) velocityExit=(%.3f %.3f %.3f) anglesEntry=(%.3f %.3f %.3f) anglesExit=(%.3f %.3f %.3f).\n",
			actual.transactionId,
			actual.commandNumber,
			PortalSideName(actual.entrySide),
			PortalSideName(actual.exitSide),
			actual.projectedPath ? "projected" : "live",
			BoolText(actual.wasOnGround),
			actual.actualEntryCommandEndOrigin.x, actual.actualEntryCommandEndOrigin.y, actual.actualEntryCommandEndOrigin.z,
			actual.linearEntryCommandEndOrigin.x, actual.linearEntryCommandEndOrigin.y, actual.linearEntryCommandEndOrigin.z,
			entryLinearDelta.x, entryLinearDelta.y, entryLinearDelta.z,
			std::sqrt(entryLinearDelta.LenghtSqr()),
			actual.actualExitCommandEndOrigin.x, actual.actualExitCommandEndOrigin.y, actual.actualExitCommandEndOrigin.z,
			actual.officialExitCommandEndOrigin.x, actual.officialExitCommandEndOrigin.y, actual.officialExitCommandEndOrigin.z,
			exitOfficialDelta.x, exitOfficialDelta.y, exitOfficialDelta.z,
			std::sqrt(exitOfficialDelta.LenghtSqr()),
			actual.committedExitOrigin.x, actual.committedExitOrigin.y, actual.committedExitOrigin.z,
			exitCommittedDelta.x, exitCommittedDelta.y, exitCommittedDelta.z,
			std::sqrt(exitCommittedDelta.LenghtSqr()),
			SignedPlaneDistance(exitCenter, actual.exitPlaneOrigin, actual.exitPlaneNormal),
			actual.actualEntryCommandEndVelocity.x, actual.actualEntryCommandEndVelocity.y, actual.actualEntryCommandEndVelocity.z,
			actual.actualExitCommandEndVelocity.x, actual.actualExitCommandEndVelocity.y, actual.actualExitCommandEndVelocity.z,
			actual.actualEntryCommandEndAngles.x, actual.actualEntryCommandEndAngles.y, actual.actualEntryCommandEndAngles.z,
			actual.actualExitCommandEndAngles.x, actual.actualExitCommandEndAngles.y, actual.actualExitCommandEndAngles.z);
	}

	void EndPredictionCommitMovementDiagnostics()
	{
		g_predictionCommitMovementDiagnostics = {};
	}

void LogCategorizeSnapshot(const char* domain, const char* point, uint32_t heartbeat, void* gameMovement, bool detailed)
	{
		if (!detailed && !ShouldLogMovementHeartbeat(heartbeat))
			return;

		const PortalCollisionBridgeDiagnostics& diag = G::G_L4D2Portal.m_PortalCollisionBridge.GetDiagnostics();
		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		CMoveData* move = TryGetMoveDataFromGameMovement(gameMovement);
		if (!move)
		{
			U::LogWarning("[PortalBridge][PositionProbe][%s][CategorizePosition][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s ctxDepth=%.2f inside=%s moving=%s gm=%p mv=null.\n",
				domain,
				point,
				heartbeat,
				BoolText(detailed),
				diag.frameCommandNumber,
				PortalPhaseName(diag.lastPhase),
				PortalSideName(diag.lastEntrySide),
				context.signedDepth,
				BoolText(context.insideAperture),
				BoolText(context.movingIntoPortal),
				gameMovement);
			return;
		}

		const Vector& moveOrigin = move->GetAbsOrigin();
		const Vector& moveVelocity = move->m_vecVelocity;
		U::LogWarning("[PortalBridge][PositionProbe][%s][CategorizePosition][%s] heartbeat=%u detailed=%s cmd=%d phase=%s entry=%s bridge=%s ctxDepth=%.2f inside=%s moving=%s gm=%p mv=%p moveOrigin=(%.1f %.1f %.1f) moveVel=(%.1f %.1f %.1f) step=%.2f gameMoved=%s.\n",
			domain,
			point,
			heartbeat,
			BoolText(detailed),
			diag.frameCommandNumber,
			PortalPhaseName(diag.lastPhase),
			PortalSideName(diag.lastEntrySide),
			BoolText(G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase()),
			context.signedDepth,
			BoolText(context.insideAperture),
			BoolText(context.movingIntoPortal),
			gameMovement,
			move,
			moveOrigin.x, moveOrigin.y, moveOrigin.z,
			moveVelocity.x, moveVelocity.y, moveVelocity.z,
			move->m_outStepHeight,
			BoolText(move->m_bGameCodeMovedPlayer));
	}

}

static void HandleTracePlayerBBox(const char* domain, CCSGameMovement::TracePlayerBBox::FN original, void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	// 打印调试信息
	//U::LogDebug("[GameMovement] TracePlayerBBox called!\n");
	//U::LogDebug("[GameMovement] Parameters: start=(%f,%f,%f), end=(%f,%f,%f)\n",
	//	start.x, start.y, start.z,
	//	start.x, start.y, start.z);
	//U::LogDebug("[GameMovement] pTrace: %p\n", &pm);

	const bool shouldInspectMoveData = G::G_L4D2Portal.m_PortalTransitionSimulator.IsInCollisionBridgePhase();

	original(ecx, edx, start, end, fMask, collisionGroup, pm);
	if (domain && std::strcmp(domain, "client") == 0)
	{
		RecordPredictionCommitMovementTrace(
			start,
			end,
			pm);
	}
	if (!shouldInspectMoveData)
		return;

	PortalTraceRequest request;
	request.start = start;
	request.end = end;
	request.mask = fMask;
	request.collisionGroup = collisionGroup;
	request.trace = pm;
	(void)G::G_L4D2Portal.m_PortalCollisionBridge.TryBypassPlayerBBoxTrace(
		request,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	//pm->fraction = 1.0f;  // 设置�?.0表示射线到达终点，没有发生碰�?	//pm->allsolid = true;     // 不是完全固体
	//pm->startsolid = true;   // 起始点不在固体中
	//pm->contents = 0;         // 无特殊内容标�?	//pm->endpos = end;         // 结束位置设为目标位置

	//// 如果想更真实，可以保留原始起�?	//pm->startpos = start;

	//// 清除命中实体信息
	//pm->m_pEnt = NULL;

	(void)start;
	(void)end;
	(void)fMask;
	(void)collisionGroup;
	(void)pm;
	return;
}

void __fastcall CCSGameMovement::TracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("server-signature", Func.Original<FN>(), ecx, edx, start, end, fMask, collisionGroup, pm);
}

void __fastcall CCSGameMovement::ServerTracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("server", ServerTable.Original<FN>(Index), ecx, edx, start, end, fMask, collisionGroup, pm);
}

void __fastcall CCSGameMovement::ClientTracePlayerBBox::Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm)
{
	HandleTracePlayerBBox("client", ClientTable.Original<FN>(Index), ecx, edx, start, end, fMask, collisionGroup, pm);
}

int __fastcall CCSGameMovement::TryPlayerMove::Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = (log || ShouldLogMovementHeartbeat(heartbeat)) ? TryGetMoveDataFromGameMovement(ecx) : nullptr;
	LogTryPlayerMoveEnter("server", heartbeat, ecx, pFirstDest, pFirstTrace, move, log);

	const int result = ServerTable.Original<FN>(Index)(ecx, edx, pFirstDest, pFirstTrace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogTryPlayerMoveExit("server", heartbeat, result, ecx, pFirstDest, pFirstTrace, move, log);
	}

	return result;
}

void __fastcall CCSGameMovement::StepMove::Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = (log || ShouldLogMovementHeartbeat(heartbeat)) ? TryGetMoveDataFromGameMovement(ecx) : nullptr;
	LogStepMoveEnter("server", heartbeat, ecx, vecDestination, trace, move, log);

	ServerTable.Original<FN>(Index)(ecx, edx, vecDestination, trace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogStepMoveExit("server", ecx, vecDestination, trace, move, log);
	}
}

int __fastcall CCSGameMovement::ClientTryPlayerMove::Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
	LogTryPlayerMoveEnter("client", heartbeat, ecx, pFirstDest, pFirstTrace, move, log);

	const int result = ClientTable.Original<FN>(Index)(ecx, edx, pFirstDest, pFirstTrace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogTryPlayerMoveExit("client", heartbeat, result, ecx, pFirstDest, pFirstTrace, move, log);
	}

	return result;
}

void __fastcall CCSGameMovement::ClientStepMove::Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CMoveData* move = TryGetMoveDataFromGameMovement(ecx);
	LogStepMoveEnter("client", heartbeat, ecx, vecDestination, trace, move, log);

	ClientTable.Original<FN>(Index)(ecx, edx, vecDestination, trace);

	if (log)
	{
		move = TryGetMoveDataFromGameMovement(ecx);
		LogStepMoveExit("client", ecx, vecDestination, trace, move, log);
	}
}

void __fastcall CCSGameMovement::PlayerMove::Detour(void* ecx, void* edx)
{
    const auto* fallCommand=ServerPlayerRunCommand::GetActiveCommand();
    PortalFallWrite::PlayerMoveScope fallScope(ecx,fallCommand?fallCommand->command_number:0);
    PortalNativeDiagnostics::FallMovement(true,false,ecx,ServerPlayerRunCommand::GetActiveCommand());
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	CUserCmd* activeCommand = ServerPlayerRunCommand::GetActiveCommand();
	PortalMovementCommandEndDiagnostic commandEnd;
	const bool commandEndMatched = activeCommand
		&& G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetMovementCommandEndDiagnostic(
			activeCommand->command_number,
			&commandEnd);
	CMoveData* commandMove = commandEndMatched
		? TryGetMoveDataFromGameMovement(ecx)
		: nullptr;
	const Vector commandOriginBefore = commandMove
		? commandMove->GetAbsOrigin()
		: Vector();
	const Vector commandVelocityBefore = commandMove
		? commandMove->m_vecVelocity
		: Vector();
	const QAngle commandAnglesBefore = commandMove
		? commandMove->m_vecViewAngles
		: QAngle();
	LogMovementStageSnapshot("server", "PlayerMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("server", "PlayerMove", "Enter", heartbeat, ecx);
	ServerTable.Original<FN>(Index)(ecx, edx);
	constexpr bool controlledMoveApplied = false;
	if (commandEndMatched)
	{
		commandMove = TryGetMoveDataFromGameMovement(ecx);
		if (!commandMove)
		{
			U::LogInfo("[PortalServerCommandMovement] txn=%u cmd=%d matched=false reason=move-data-null behaviorMutation=%s.\n",
				commandEnd.transactionId,
				activeCommand ? activeCommand->command_number : 0,
				BoolText(controlledMoveApplied));
		}
		else
		{
			const Vector commandOriginAfter = commandMove->GetAbsOrigin();
			const Vector commandVelocityAfter = commandMove->m_vecVelocity;
			const QAngle commandAnglesAfter = commandMove->m_vecViewAngles;
			const Vector originDelta = commandOriginAfter - commandOriginBefore;
			const Vector beforeCommittedDelta =
				commandOriginBefore - commandEnd.committedExitOrigin;
			const Vector afterOfficialDelta =
				commandOriginAfter - commandEnd.officialExitCommandEndOrigin;
			const Vector beforeCenter =
				commandOriginBefore - commandEnd.originFromCenter;
			const Vector afterCenter =
				commandOriginAfter - commandEnd.originFromCenter;
			U::LogInfo("[PortalServerCommandMovement] txn=%u cmd=%d matched=true path=%s onGround=%s behaviorMutation=%s commandAngles=(%.3f %.3f %.3f) moveAngles=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) origin=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) originDelta=(%.3f %.3f %.3f) committedExitOrigin=(%.3f %.3f %.3f) beforeCommittedDelta=(%.3f %.3f %.3f) beforeCommittedDistance=%.3f officialExitCommandEndOrigin=(%.3f %.3f %.3f) afterOfficialDelta=(%.3f %.3f %.3f) afterOfficialDistance=%.3f exitDepth=(before=%.3f after=%.3f) velocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) speed2D=%.3f->%.3f startsAtCommitted=%s.\n",
				commandEnd.transactionId,
				activeCommand->command_number,
				commandEnd.projectedPath ? "projected" : "live",
				BoolText(commandEnd.wasOnGround),
				BoolText(controlledMoveApplied),
				activeCommand->viewangles.x, activeCommand->viewangles.y, activeCommand->viewangles.z,
				commandAnglesBefore.x, commandAnglesBefore.y, commandAnglesBefore.z,
				commandAnglesAfter.x, commandAnglesAfter.y, commandAnglesAfter.z,
				commandOriginBefore.x, commandOriginBefore.y, commandOriginBefore.z,
				commandOriginAfter.x, commandOriginAfter.y, commandOriginAfter.z,
				originDelta.x, originDelta.y, originDelta.z,
				commandEnd.committedExitOrigin.x, commandEnd.committedExitOrigin.y, commandEnd.committedExitOrigin.z,
				beforeCommittedDelta.x, beforeCommittedDelta.y, beforeCommittedDelta.z,
				std::sqrt(beforeCommittedDelta.LenghtSqr()),
				commandEnd.officialExitCommandEndOrigin.x, commandEnd.officialExitCommandEndOrigin.y, commandEnd.officialExitCommandEndOrigin.z,
				afterOfficialDelta.x, afterOfficialDelta.y, afterOfficialDelta.z,
				std::sqrt(afterOfficialDelta.LenghtSqr()),
				SignedPlaneDistance(beforeCenter, commandEnd.exitPlaneOrigin, commandEnd.exitPlaneNormal),
				SignedPlaneDistance(afterCenter, commandEnd.exitPlaneOrigin, commandEnd.exitPlaneNormal),
				commandVelocityBefore.x, commandVelocityBefore.y, commandVelocityBefore.z,
				commandVelocityAfter.x, commandVelocityAfter.y, commandVelocityAfter.z,
				std::sqrt(commandVelocityBefore.x * commandVelocityBefore.x + commandVelocityBefore.y * commandVelocityBefore.y),
				std::sqrt(commandVelocityAfter.x * commandVelocityAfter.x + commandVelocityAfter.y * commandVelocityAfter.y),
				BoolText(beforeCommittedDelta.LenghtSqr() <= 0.0625f));
		}
	}
	LogFocusedHookProbe("server", "PlayerMove", "Exit", heartbeat, ecx);
	LogMovementStageSnapshot("server", "PlayerMove", "Exit", heartbeat, ecx, log);
}
void __fastcall CCSGameMovement::WalkMove::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server", "WalkMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("server", "WalkMove", "Enter", heartbeat, ecx);
	ServerTable.Original<FN>(Index)(ecx, edx);
	LogFocusedHookProbe("server", "WalkMove", "Exit", heartbeat, ecx);
    LogMovementStageSnapshot("server", "WalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::FullWalkMove::Detour(void* ecx, void* edx)
{
    PortalNativeDiagnostics::FallMovement(true,true,ecx,ServerPlayerRunCommand::GetActiveCommand());
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("server", "FullWalkMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("server", "FullWalkMove", "Enter", heartbeat, ecx);
	ServerTable.Original<FN>(Index)(ecx, edx);
	LogFocusedHookProbe("server", "FullWalkMove", "Exit", heartbeat, ecx);
	LogMovementStageSnapshot("server", "FullWalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::CategorizePosition::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogCategorizeSnapshot("server-signature", "Enter", heartbeat, ecx, log);

	const FN original = Func.Original<FN>();
	if (original)
		original(ecx, edx);
	else
		U::LogError("[PortalBridge][PositionProbe][server-signature][CategorizePosition] original function is null.\n");

	LogCategorizeSnapshot("server-signature", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientPlayerMove::Detour(void* ecx, void* edx)
{
    PortalNativeDiagnostics::FallMovement(false,false,ecx,ClientPrediction::GetActiveCommand());
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "PlayerMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("client", "PlayerMove", "Enter", heartbeat, ecx);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogFocusedHookProbe("client", "PlayerMove", "Exit", heartbeat, ecx);
	LogMovementStageSnapshot("client", "PlayerMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientProcessMovement::Detour(
	void* ecx,
	void* edx,
	C_BasePlayer* player,
	CMoveData* move)
{
	CUserCmd* activeCommand = ClientPrediction::GetActiveCommand();
	const bool diagnosticActive =
		BeginPredictionCommitMovementDiagnostics(activeCommand, player);
	const Vector originBefore = move ? move->GetAbsOrigin() : Vector();
	const Vector velocityBefore = move ? move->m_vecVelocity : Vector();
	ClientPrediction::LogTransactionBoundary(
		"processmovement-pre-original",
		player,
		activeCommand,
		move);
	if (diagnosticActive)
		LogPredictionCommitHullOccupancy(player, move);
	ClientTable.Original<FN>(Index)(ecx, edx, player, move);
	if (diagnosticActive)
	{
		LogPredictionCommitMovementSummary(
			originBefore, velocityBefore, move);
		LogOfficialCommandEndCandidate(
			originBefore, velocityBefore, move);
		RecordAndLogOfficialActualCommandEnd(move);
	}
	ClientPrediction::LogTransactionBoundary(
		"processmovement-post-original",
		player,
		activeCommand,
		move);
	EndPredictionCommitMovementDiagnostics();
}

void __fastcall CCSGameMovement::ClientWalkMove::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "WalkMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("client", "WalkMove", "Enter", heartbeat, ecx);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogFocusedHookProbe("client", "WalkMove", "Exit", heartbeat, ecx);
    LogMovementStageSnapshot("client", "WalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientFullWalkMove::Detour(void* ecx, void* edx)
{
    PortalNativeDiagnostics::FallMovement(false,true,ecx,ClientPrediction::GetActiveCommand());
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogMovementStageSnapshot("client", "FullWalkMove", "Enter", heartbeat, ecx, log);
	LogFocusedHookProbe("client", "FullWalkMove", "Enter", heartbeat, ecx);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogFocusedHookProbe("client", "FullWalkMove", "Exit", heartbeat, ecx);
	LogMovementStageSnapshot("client", "FullWalkMove", "Exit", heartbeat, ecx, log);
}

void __fastcall CCSGameMovement::ClientCategorizePosition::Detour(void* ecx, void* edx)
{
	const bool log = ShouldLogServerMovement();
	static uint32_t heartbeat = 0u;
	++heartbeat;
	LogCategorizeSnapshot("client", "Enter", heartbeat, ecx, log);
	ClientTable.Original<FN>(Index)(ecx, edx);
	LogCategorizeSnapshot("client", "Exit", heartbeat, ecx, log);
}

void CCSGameMovement::Init()
{
	const TracePlayerBBox::FN ctracePlayerBBox = reinterpret_cast<TracePlayerBBox::FN>(U::Offsets.m_dwTracePlayerBBox);
	U::LogInfo("[PortalBridge] server TracePlayerBBox signature target=%p.\n", ctracePlayerBBox);
	XASSERT(ctracePlayerBBox == nullptr);

	bool hookedSignatureTracePlayerBBox = false;
	if (ctracePlayerBBox)
		hookedSignatureTracePlayerBBox = TracePlayerBBox::Func.Init(ctracePlayerBBox, &TracePlayerBBox::Detour);
	U::LogInfo("[PortalBridge] server TracePlayerBBox signature hook=%s.\n", BoolText(hookedSignatureTracePlayerBBox));

	const CategorizePosition::FN categorizePosition = reinterpret_cast<CategorizePosition::FN>(U::Offsets.m_dwCategorizePosition);
	U::LogInfo("[PortalBridge] server CategorizePosition(void) signature target=%p.\n", categorizePosition);

	bool hookedSignatureCategorizePosition = false;
	if (categorizePosition)
		hookedSignatureCategorizePosition = CategorizePosition::Func.Init(categorizePosition, &CategorizePosition::Detour);
	U::LogInfo("[PortalBridge] server CategorizePosition(void) signature hook=%s.\n", BoolText(hookedSignatureCategorizePosition));

	if (!I::ServerGameMovement)
	{
		U::LogWarning("[PortalBridge] server GameMovement001 is null; skipping StepMove/TryPlayerMove diagnostics.\n");
		return;
	}

	if (ServerTable.Init(I::ServerGameMovement) == false)
	{
		U::LogError("[PortalBridge] failed to initialize server GameMovement vtable diagnostics table.\n");
		return;
	}


	void** serverVTable = *reinterpret_cast<void***>(I::ServerGameMovement);
    PortalFallWrite::Initialize(serverVTable?serverVTable[PlayerMove::Index]:nullptr);
	void* serverCategorizeVTableEntry = serverVTable ? serverVTable[CategorizePosition::Index] : nullptr;
	void* serverTryPlayerMoveVTableEntry = serverVTable ? serverVTable[TryPlayerMove::Index] : nullptr;
	void* serverTryPlayerMoveRejected40Entry = serverVTable ? serverVTable[40u] : nullptr;
	const bool serverCategorizeMatchesVTable = categorizePosition && serverCategorizeVTableEntry == reinterpret_cast<void*>(categorizePosition);

	const bool hookedPlayerMove = ServerTable.Hook(&PlayerMove::Detour, PlayerMove::Index);
	const bool hookedWalkMove = ServerTable.Hook(&WalkMove::Detour, WalkMove::Index);
	const bool hookedFullWalkMove = ServerTable.Hook(&FullWalkMove::Detour, FullWalkMove::Index);
	const bool hookedCategorizePosition = hookedSignatureCategorizePosition;
	const bool hookedTryPlayerMove = ServerTable.Hook(&TryPlayerMove::Detour, TryPlayerMove::Index);
	const bool hookedStepMove = ServerTable.Hook(&StepMove::Detour, StepMove::Index);
	U::LogInfo("[PortalBridge] server GameMovement diagnostics gm=%p TracePlayerBBox=signature-hook(%s) PlayerMove[%u]=%s WalkMove[%u]=%s FullWalkMove[%u]=%s CategorizePosition(void)=%p vtable[%u]=%p match=%s hook=%s TryPlayerMove[%u]=%p hook=%s TryPlayerMoveRejected[40]=%p(skipped: crashed with TryPlayerMove signature) StepMove[%u]=%s(validating server index 64).\n",
		I::ServerGameMovement,
		BoolText(hookedSignatureTracePlayerBBox),
		PlayerMove::Index,
		BoolText(hookedPlayerMove),
		WalkMove::Index,
		BoolText(hookedWalkMove),
		FullWalkMove::Index,
		BoolText(hookedFullWalkMove),
		reinterpret_cast<void*>(categorizePosition),
		CategorizePosition::Index,
		serverCategorizeVTableEntry,
		BoolText(serverCategorizeMatchesVTable),
		BoolText(hookedCategorizePosition),
		TryPlayerMove::Index,
		serverTryPlayerMoveVTableEntry,
		BoolText(hookedTryPlayerMove),
		serverTryPlayerMoveRejected40Entry,
		StepMove::Index,
		BoolText(hookedStepMove));

	if (!I::GameMovement)
	{
		U::LogWarning("[PortalBridge] client GameMovement001 is null; skipping client StepMove/TryPlayerMove diagnostics.\n");
		return;
	}

	if (ClientTable.Init(I::GameMovement) == false)
	{
		U::LogError("[PortalBridge] failed to initialize client GameMovement vtable diagnostics table.\n");
		return;
	}

	void** clientVTable = *reinterpret_cast<void***>(I::GameMovement);
	const bool hookedClientProcessMovement = ClientTable.Hook(&ClientProcessMovement::Detour, ClientProcessMovement::Index);
	const bool hookedClientTracePlayerBBox = ClientTable.Hook(&ClientTracePlayerBBox::Detour, ClientTracePlayerBBox::Index);
	const bool hookedClientPlayerMove = ClientTable.Hook(&ClientPlayerMove::Detour, ClientPlayerMove::Index);
	const bool hookedClientWalkMove = ClientTable.Hook(&ClientWalkMove::Detour, ClientWalkMove::Index);
	const bool hookedClientFullWalkMove = ClientTable.Hook(&ClientFullWalkMove::Detour, ClientFullWalkMove::Index);
	const bool hookedClientCategorizePosition = false;
	const bool hookedClientTryPlayerMove = false;
	const bool hookedClientStepMove = false;
	U::LogInfo("[PortalBridge] client GameMovement diagnostics gm=%p ProcessMovement[%u]=%s TracePlayerBBox[%u]=%s PlayerMove[%u]=%s WalkMove[%u]=%s FullWalkMove[%u]=%s CategorizePosition[%u]=%s(skipped: not yet hooked) TryPlayerMove[%u]=%s(skipped: index/signature unverified) StepMove[%u]=%s(skipped: index/signature unverified).\n",
		I::GameMovement,
		ClientProcessMovement::Index,
		BoolText(hookedClientProcessMovement),
		ClientTracePlayerBBox::Index,
		BoolText(hookedClientTracePlayerBBox),
		ClientPlayerMove::Index,
		BoolText(hookedClientPlayerMove),
		ClientWalkMove::Index,
		BoolText(hookedClientWalkMove),
		ClientFullWalkMove::Index,
		BoolText(hookedClientFullWalkMove),
		ClientCategorizePosition::Index,
		BoolText(hookedClientCategorizePosition),
		ClientTryPlayerMove::Index,
		BoolText(hookedClientTryPlayerMove),
		ClientStepMove::Index,
		BoolText(hookedClientStepMove));
}
