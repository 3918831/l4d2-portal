#include "ClientPrediction.h"
#include "../ServerPlayer/ServerPlayerRunCommand.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalPhysicsMode.h"
#include "../../Portal/PortalTransform.h"
#include "../../Portal/PortalTransitionDecision.h"
#include "../../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../../Util/Logger/Logger.h"

using namespace Hooks;

namespace
{
	thread_local CUserCmd* g_activePredictionCommand = nullptr;

	struct PredictionSetupSyncObservation
	{
		int commandNumber = 0;
		bool angleSyncApplied = false;
		bool movementSyncApplied = false;
	};

	thread_local PredictionSetupSyncObservation g_predictionSetupSyncObservation;

	PortalVisualContinuity::Vec3 ToContinuityVec3(const Vector& value)
	{
		return { value.x, value.y, value.z };
	}

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

	const char* CoordinateSpaceName(
		bool valid,
		float exitDelta,
		float entryDelta)
	{
		if (!valid)
			return "Unavailable";
		return exitDelta <= entryDelta ? "Exit" : "Entry";
	}

	bool TrySyncCommittedStateAtPredictionSetup(
		CUserCmd* ucmd,
		CMoveData* move)
	{
		g_predictionSetupSyncObservation = {};
		if (ucmd)
			g_predictionSetupSyncObservation.commandNumber = ucmd->command_number;

		if (!ucmd || !move)
			return false;

		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		Vector committedOrigin;
		Vector committedVelocity;
		QAngle committedAngles;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForCommand(
			ucmd->command_number,
			&committedOrigin,
			&committedVelocity,
			&committedAngles))
		{
			return false;
		}

		const Vector originBefore = move->GetAbsOrigin();
		const Vector velocityBefore = move->m_vecVelocity;
		const Vector commandAnglesBefore = ucmd->viewangles;
		const bool angleSyncApplied =
			PortalTransitionDecision::ShouldSyncCommittedViewAnglesAtPredictionSetup(
				context.phase,
				ucmd->command_number,
				context.teleportCommandNumber);
		if (angleSyncApplied)
		{
			move->m_vecViewAngles = committedAngles;
			move->m_vecAbsViewAngles = committedAngles;
			move->m_vecAngles = committedAngles;
			ucmd->viewangles =
				Vector(committedAngles.x, committedAngles.y, committedAngles.z);
			if (I::EngineClient)
			{
				Vector engineAngles(
					committedAngles.x,
					committedAngles.y,
					committedAngles.z);
				I::EngineClient->SetViewAngles(engineAngles);
			}
		}

		const bool movementSyncEnabled =
			PortalTransitionDecision::GetPredictionCommitMovementSyncEnabled();
		const bool movementSyncModeAllowed =
			PortalPhysicsMode::ShouldSynchronizeCommittedTeleportPrediction();
		const bool movementSyncApplied =
			movementSyncModeAllowed
			&& PortalTransitionDecision::ShouldSyncCommittedMovementAtPredictionSetup(
				movementSyncEnabled,
				context.phase,
				ucmd->command_number,
				context.teleportCommandNumber);
		if (movementSyncApplied)
		{
			// The authoritative server Teleport already placed this exact
			// command in exit space. Start the matching client prediction pass
			// from the same one-shot position and velocity, then let the
			// original ProcessMovement advance it normally.
			move->SetAbsOrigin(committedOrigin);
			move->m_vecVelocity = committedVelocity;
		}

		g_predictionSetupSyncObservation.angleSyncApplied = angleSyncApplied;
		g_predictionSetupSyncObservation.movementSyncApplied = movementSyncApplied;

		const Vector originAfter = move->GetAbsOrigin();
		const Vector velocityAfter = move->m_vecVelocity;
		const Vector commandAnglesAfter = ucmd->viewangles;
		U::LogInfo("[PortalPredictionCommitSync] phase=setupmove cmd=%d committedCmd=%d ctxPhase=%d angleSyncApplied=%s movementSyncEnabled=%s movementSyncModeAllowed=%s movementSyncApplied=%s behaviorMutation=%s origin=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) committedOrigin=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) committedVelocity=(%.3f %.3f %.3f) cmdAngles=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) committedAngles=(%.1f %.1f %.1f).\n",
			ucmd->command_number,
			context.teleportCommandNumber,
			static_cast<int>(context.phase),
			angleSyncApplied ? "true" : "false",
			movementSyncEnabled ? "true" : "false",
			movementSyncModeAllowed ? "true" : "false",
			movementSyncApplied ? "true" : "false",
			movementSyncApplied ? "true" : "false",
			originBefore.x, originBefore.y, originBefore.z,
			originAfter.x, originAfter.y, originAfter.z,
			committedOrigin.x, committedOrigin.y, committedOrigin.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			velocityAfter.x, velocityAfter.y, velocityAfter.z,
			committedVelocity.x, committedVelocity.y, committedVelocity.z,
			commandAnglesBefore.x, commandAnglesBefore.y, commandAnglesBefore.z,
			commandAnglesAfter.x, commandAnglesAfter.y, commandAnglesAfter.z,
			committedAngles.x, committedAngles.y, committedAngles.z);
		return angleSyncApplied || movementSyncApplied;
	}

	std::uint32_t CaptureCommittedMoveAtPredictionFinish(
		CUserCmd* ucmd,
		CMoveData* move)
	{
		if (!ucmd || !move)
			return 0;

		const PortalTransitionContext& context = G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext();
		Vector committedOrigin;
		Vector committedVelocity;
		QAngle committedAngles;
		if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForCommand(
			ucmd->command_number,
			&committedOrigin,
			&committedVelocity,
			&committedAngles))
		{
			return 0;
		}

		const Vector originBefore = move->GetAbsOrigin();
		const Vector velocityBefore = move->m_vecVelocity;
		const Vector commandAnglesBefore = ucmd->viewangles;
		const Vector originAfter = originBefore;
		const Vector velocityAfter = velocityBefore;
		const Vector commandAnglesAfter = ucmd->viewangles;
		const float originDeltaSqr = (originBefore - committedOrigin).LenghtSqr();
		const bool movementSyncApplied =
			g_predictionSetupSyncObservation.commandNumber == ucmd->command_number
			&& g_predictionSetupSyncObservation.movementSyncApplied;

		PortalVisualContinuity::Sample continuitySample;
		continuitySample.playerOrigin = ToContinuityVec3(originBefore);
		continuitySample.velocity = ToContinuityVec3(velocityBefore);
		continuitySample.eyeAngles = ToContinuityVec3(commandAnglesAfter);
		continuitySample.hasPlayerOrigin = true;
		continuitySample.hasVelocity = true;
		continuitySample.hasEyeAngles = true;
		continuitySample.angleSemantics =
			PortalVisualContinuity::SampleSemantics::ExitViewSpace;
		continuitySample.playerSemantics =
			PortalVisualContinuity::SampleSemantics::ExitPhysical;
		continuitySample.velocitySemantics =
			PortalVisualContinuity::SampleSemantics::ExitPhysical;
		continuitySample.engineTime =
			I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
		continuitySample.frameCount =
			I::GlobalVars ? I::GlobalVars->framecount : -1;
		continuitySample.traversalEpoch = context.traversalEpoch;
		continuitySample.commandNumber = ucmd->command_number;
		continuitySample.transitionPhase = static_cast<int>(context.phase);

		PortalVisualContinuity::PredictionBoundaryDetails continuityDetails;
		continuityDetails.syncApplied = movementSyncApplied;
		continuityDetails.expectedOrigin = ToContinuityVec3(committedOrigin);
		continuityDetails.expectedVelocity = ToContinuityVec3(committedVelocity);
		continuityDetails.expectedAngles = ToContinuityVec3(
			Vector(committedAngles.x, committedAngles.y, committedAngles.z));
		continuityDetails.beforeOrigin = ToContinuityVec3(originBefore);
		continuityDetails.afterOrigin = ToContinuityVec3(originAfter);
		continuityDetails.beforeVelocity = ToContinuityVec3(velocityBefore);
		continuityDetails.afterVelocity = ToContinuityVec3(velocityAfter);
		continuityDetails.beforeCommandAngles =
			ToContinuityVec3(commandAnglesBefore);
		continuityDetails.afterCommandAngles =
			ToContinuityVec3(commandAnglesAfter);
		const std::uint32_t continuityTransactionId =
			G::G_L4D2Portal.m_PortalVisualContinuityDiagnostics.CapturePredictionBoundary(
				ucmd->command_number,
				continuitySample,
				continuityDetails);

		U::LogInfo("[PortalPredictionObservation] txn=%u phase=finishmove-pre-original cmd=%d committedCmd=%d ctxPhase=%d syncApplied=%s movementMutation=false expectedOrigin=(%.1f %.1f %.1f) moveOrigin=(%.1f %.1f %.1f) expectedVelocity=(%.1f %.1f %.1f) moveVelocity=(%.1f %.1f %.1f) delta=%.1f expectedAngles=(%.1f %.1f %.1f) cmdAngles=(%.1f %.1f %.1f).\n",
			continuityTransactionId,
			ucmd->command_number,
			context.teleportCommandNumber,
			static_cast<int>(context.phase),
			BoolText(movementSyncApplied),
			committedOrigin.x, committedOrigin.y, committedOrigin.z,
			originBefore.x, originBefore.y, originBefore.z,
			committedVelocity.x, committedVelocity.y, committedVelocity.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			std::sqrt(originDeltaSqr),
			committedAngles.x, committedAngles.y, committedAngles.z,
			commandAnglesBefore.x, commandAnglesBefore.y, commandAnglesBefore.z);
		return continuityTransactionId;
	}
}

CUserCmd* ClientPrediction::GetActiveCommand()
{
	return g_activePredictionCommand;
}

void ClientPrediction::LogTransactionBoundary(
	const char* stage,
	C_BasePlayer* player,
	CUserCmd* ucmd,
	CMoveData* move)
{
	if (!PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled()
		|| !ucmd)
	{
		return;
	}

	Vector committedOrigin;
	Vector committedVelocity;
	QAngle committedAngles;
	int commandDelta = 0;
	std::uint32_t transactionId = 0;
	if (!G::G_L4D2Portal.m_PortalTransitionSimulator.TryGetCommittedMovementForDiagnostic(
		ucmd->command_number,
		&committedOrigin,
		&committedVelocity,
		&committedAngles,
		&commandDelta,
		&transactionId))
	{
		return;
	}

	PortalServerRunCommandObservation observation;
	G::G_L4D2Portal.m_PortalTransitionSimulator.ObserveServerRunCommand(
		ucmd,
		&observation);

	Vector expectedEntryOrigin;
	Vector expectedEntryVelocity;
	bool inverseValid = false;
	if (observation.hasEntryToExitMatrix)
	{
		matrix3x4_t exitToEntry;
		inverseValid =
			U::Math.MatrixInverse(observation.entryToExit, exitToEntry);
		if (inverseValid)
		{
			expectedEntryOrigin =
				PortalTransform::TransformPoint(exitToEntry, committedOrigin);
			expectedEntryVelocity =
				PortalTransform::TransformVector(exitToEntry, committedVelocity);
		}
	}

	const bool playerValid = player != nullptr;
	const bool moveValid = move != nullptr;
	const Vector playerAbsOrigin =
		playerValid ? player->GetAbsOrigin() : Vector();
	const Vector playerNetOrigin =
		playerValid ? player->m_vecOrigin() : Vector();
	const Vector playerOldOrigin =
		playerValid ? player->GetOldOrigin() : Vector();
	const Vector playerRenderOrigin =
		playerValid ? player->GetRenderOrigin() : Vector();
	const Vector playerVelocity =
		playerValid ? player->m_vecVelocity() : Vector();
	const Vector playerBaseVelocity =
		playerValid ? player->m_vecBaseVelocity() : Vector();
	const Vector moveOrigin =
		moveValid ? move->GetAbsOrigin() : Vector();
	const Vector moveVelocity =
		moveValid ? move->m_vecVelocity : Vector();

	const float playerExitOriginDelta = playerValid
		? VectorLength(playerAbsOrigin - committedOrigin)
		: -1.0f;
	const float playerEntryOriginDelta = playerValid && inverseValid
		? VectorLength(playerAbsOrigin - expectedEntryOrigin)
		: -1.0f;
	const float playerExitVelocityDelta = playerValid
		? VectorLength(playerVelocity - committedVelocity)
		: -1.0f;
	const float playerEntryVelocityDelta = playerValid && inverseValid
		? VectorLength(playerVelocity - expectedEntryVelocity)
		: -1.0f;
	const float moveExitOriginDelta = moveValid
		? VectorLength(moveOrigin - committedOrigin)
		: -1.0f;
	const float moveEntryOriginDelta = moveValid && inverseValid
		? VectorLength(moveOrigin - expectedEntryOrigin)
		: -1.0f;
	const float moveExitVelocityDelta = moveValid
		? VectorLength(moveVelocity - committedVelocity)
		: -1.0f;
	const float moveEntryVelocityDelta = moveValid && inverseValid
		? VectorLength(moveVelocity - expectedEntryVelocity)
		: -1.0f;

	const bool inPrediction = I::Prediction && I::Prediction->InPrediction();
	const bool firstTimePredicted =
		I::Prediction && I::Prediction->IsFirstTimePredicted();
	const int frameCount = I::GlobalVars ? I::GlobalVars->framecount : -1;
	const int tickCount = I::GlobalVars ? I::GlobalVars->tickcount : -1;
	const float currentTime = I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
	const float frameTime = I::GlobalVars ? I::GlobalVars->frametime : 0.0f;
	const float interval = I::GlobalVars ? I::GlobalVars->interval_per_tick : 0.0f;

	U::LogInfo("[PortalPredictionCommandWindow] txn=%u epoch=%u committedCmd=%d cmd=%d commandDelta=%d cmdTick=%d stage=%s diagnosticMutation=false inPrediction=%s firstTimePredicted=%s cmdPredicted=%s frame=%d tick=%d curtime=%.6f frameTime=%.6f interval=%.6f playerValid=%s tickBase=%d simulationTime=%.6f moveType=%u flags=0x%X ground=%d moveValid=%s moveFirstRun=%s gameCodeMoved=%s cmdAngles=(%.3f %.3f %.3f) moveAngles=(%.3f %.3f %.3f) committedExitAngles=(%.3f %.3f %.3f) inverseValid=%s.\n",
		transactionId,
		G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext().traversalEpoch,
		observation.committedCommandNumber,
		ucmd->command_number,
		commandDelta,
		ucmd->tick_count,
		stage ? stage : "unknown",
		BoolText(inPrediction),
		BoolText(firstTimePredicted),
		BoolText(ucmd->hasbeenpredicted),
		frameCount,
		tickCount,
		currentTime,
		frameTime,
		interval,
		BoolText(playerValid),
		playerValid ? player->m_nTickBase() : -1,
		playerValid ? player->m_flSimulationTime() : 0.0f,
		playerValid ? static_cast<unsigned int>(player->m_MoveType()) : 0u,
		playerValid ? player->m_fFlags() : 0,
		playerValid ? player->m_hGroundEntity().ToInt() : -1,
		BoolText(moveValid),
		BoolText(moveValid && move->m_bFirstRunOfFunctions),
		BoolText(moveValid && move->m_bGameCodeMovedPlayer),
		ucmd->viewangles.x, ucmd->viewangles.y, ucmd->viewangles.z,
		moveValid ? move->m_vecAngles.x : 0.0f,
		moveValid ? move->m_vecAngles.y : 0.0f,
		moveValid ? move->m_vecAngles.z : 0.0f,
		committedAngles.x, committedAngles.y, committedAngles.z,
		BoolText(inverseValid));

	U::LogInfo("[PortalPredictionCommandWindowPosition] txn=%u epoch=%u committedCmd=%d cmd=%d commandDelta=%d stage=%s absOrigin=(%.3f %.3f %.3f) netOrigin=(%.3f %.3f %.3f) oldOrigin=(%.3f %.3f %.3f) renderOrigin=(%.3f %.3f %.3f) playerOriginSpace=%s playerExitDelta=%.3f playerEntryDelta=%.3f moveOrigin=(%.3f %.3f %.3f) moveOriginSpace=%s moveExitDelta=%.3f moveEntryDelta=%.3f commitExitReference=(%.3f %.3f %.3f) commitEntryReference=(%.3f %.3f %.3f).\n",
		transactionId,
		G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext().traversalEpoch,
		observation.committedCommandNumber,
		ucmd->command_number,
		commandDelta,
		stage ? stage : "unknown",
		playerAbsOrigin.x, playerAbsOrigin.y, playerAbsOrigin.z,
		playerNetOrigin.x, playerNetOrigin.y, playerNetOrigin.z,
		playerOldOrigin.x, playerOldOrigin.y, playerOldOrigin.z,
		playerRenderOrigin.x, playerRenderOrigin.y, playerRenderOrigin.z,
		CoordinateSpaceName(
			playerValid && inverseValid,
			playerExitOriginDelta,
			playerEntryOriginDelta),
		playerExitOriginDelta,
		playerEntryOriginDelta,
		moveOrigin.x, moveOrigin.y, moveOrigin.z,
		CoordinateSpaceName(
			moveValid && inverseValid,
			moveExitOriginDelta,
			moveEntryOriginDelta),
		moveExitOriginDelta,
		moveEntryOriginDelta,
		committedOrigin.x, committedOrigin.y, committedOrigin.z,
		expectedEntryOrigin.x, expectedEntryOrigin.y, expectedEntryOrigin.z);

	U::LogInfo("[PortalPredictionCommandWindowVelocity] txn=%u epoch=%u committedCmd=%d cmd=%d commandDelta=%d stage=%s playerVelocity=(%.3f %.3f %.3f) playerVelocitySpace=%s playerExitDelta=%.3f playerEntryDelta=%.3f playerSpeed2D=%.3f playerSpeed3D=%.3f playerVelocityZ=%.3f baseVelocity=(%.3f %.3f %.3f) moveVelocity=(%.3f %.3f %.3f) moveVelocitySpace=%s moveExitDelta=%.3f moveEntryDelta=%.3f moveSpeed2D=%.3f moveSpeed3D=%.3f moveVelocityZ=%.3f commitExitReference=(%.3f %.3f %.3f) commitEntryReference=(%.3f %.3f %.3f).\n",
		transactionId,
		G::G_L4D2Portal.m_PortalTransitionSimulator.GetContext().traversalEpoch,
		observation.committedCommandNumber,
		ucmd->command_number,
		commandDelta,
		stage ? stage : "unknown",
		playerVelocity.x, playerVelocity.y, playerVelocity.z,
		CoordinateSpaceName(
			playerValid && inverseValid,
			playerExitVelocityDelta,
			playerEntryVelocityDelta),
		playerExitVelocityDelta,
		playerEntryVelocityDelta,
		playerValid ? Speed2D(playerVelocity) : -1.0f,
		playerValid ? VectorLength(playerVelocity) : -1.0f,
		playerValid ? playerVelocity.z : 0.0f,
		playerBaseVelocity.x, playerBaseVelocity.y, playerBaseVelocity.z,
		moveVelocity.x, moveVelocity.y, moveVelocity.z,
		CoordinateSpaceName(
			moveValid && inverseValid,
			moveExitVelocityDelta,
			moveEntryVelocityDelta),
		moveExitVelocityDelta,
		moveEntryVelocityDelta,
		moveValid ? Speed2D(moveVelocity) : -1.0f,
		moveValid ? VectorLength(moveVelocity) : -1.0f,
		moveValid ? moveVelocity.z : 0.0f,
		committedVelocity.x, committedVelocity.y, committedVelocity.z,
		expectedEntryVelocity.x, expectedEntryVelocity.y, expectedEntryVelocity.z);
}

void __fastcall ClientPrediction::RunCommand::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper)
{
	ServerPlayerRunCommand::EnsureInstalled(
		player ? static_cast<C_TerrorPlayer*>(player) : nullptr);
	CUserCmd* previousActiveCommand = g_activePredictionCommand;
	g_activePredictionCommand = ucmd;
	ClientPrediction::LogTransactionBoundary(
		"runcommand-pre-original", player, ucmd, nullptr);
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, moveHelper);
	ClientPrediction::LogTransactionBoundary(
		"runcommand-post-original", player, ucmd, nullptr);
	g_activePredictionCommand = previousActiveCommand;
}

void __fastcall ClientPrediction::SetupMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move)
{
	ClientPrediction::LogTransactionBoundary(
		"setupmove-pre-original", player, ucmd, move);
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, pHelper, move);
	ClientPrediction::LogTransactionBoundary(
		"setupmove-post-original", player, ucmd, move);
	TrySyncCommittedStateAtPredictionSetup(ucmd, move);
	ClientPrediction::LogTransactionBoundary(
		"setupmove-post-commit-handoff", player, ucmd, move);
}

void __fastcall ClientPrediction::FinishMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, CMoveData* move)
{
	ClientPrediction::LogTransactionBoundary(
		"finishmove-pre-original", player, ucmd, move);
	const PortalMoveFrameDiagnostics before = G::G_L4D2Portal.m_PortalStage1Probe.CaptureMoveFrame(
		player,
		ucmd,
		move,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	const std::uint32_t continuityTransactionId =
		CaptureCommittedMoveAtPredictionFinish(ucmd, move);
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, move);
	ClientPrediction::LogTransactionBoundary(
		"finishmove-post-original", player, ucmd, move);
	if (continuityTransactionId != 0 && player && move)
	{
		const Vector playerOrigin = player->GetAbsOrigin();
		const Vector playerVelocity = player->m_vecVelocity();
		const Vector moveOrigin = move->GetAbsOrigin();
		const Vector moveVelocity = move->m_vecVelocity;
		U::LogInfo("[PortalPredictionFinish] txn=%u phase=finishmove-post-original cmd=%d playerOrigin=(%.1f %.1f %.1f) moveOrigin=(%.1f %.1f %.1f) playerVelocity=(%.1f %.1f %.1f) moveVelocity=(%.1f %.1f %.1f).\n",
			continuityTransactionId,
			ucmd ? ucmd->command_number : 0,
			playerOrigin.x, playerOrigin.y, playerOrigin.z,
			moveOrigin.x, moveOrigin.y, moveOrigin.z,
			playerVelocity.x, playerVelocity.y, playerVelocity.z,
			moveVelocity.x, moveVelocity.y, moveVelocity.z);
	}
	static float nextMoveTypeProbeTime = 0.0f;
	const float currentTime = I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
	if (currentTime >= nextMoveTypeProbeTime)
	{
		nextMoveTypeProbeTime = currentTime + 0.35f;
		G::G_L4D2Portal.m_PortalTransition.LogMoveTypeProbe("client-prediction-finishmove", player, move);
	}
	const PortalMoveFrameDiagnostics after = G::G_L4D2Portal.m_PortalStage1Probe.CaptureMoveFrame(
		player,
		ucmd,
		move,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	G::G_L4D2Portal.m_PortalStage1Probe.LogFinishMoveDiagnostics(before, after);
	G::G_L4D2Portal.m_PortalStage1Probe.LogFrameTraceDiagnostics(
		G::G_L4D2Portal.m_PortalCollisionBridge,
		after);
}

void ClientPrediction::Init()
{
	XASSERT(Table.Init(I::Prediction) == false);
	XASSERT(Table.Hook(&RunCommand::Detour, RunCommand::Index) == false);
	XASSERT(Table.Hook(&SetupMove::Detour, SetupMove::Index) == false);
	XASSERT(Table.Hook(&FinishMove::Detour, FinishMove::Index) == false);
}
