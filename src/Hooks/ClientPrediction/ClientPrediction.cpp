#include "ClientPrediction.h"
#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalTransitionDecision.h"
#include "../../Util/Logger/Logger.h"

using namespace Hooks;

namespace
{
	PortalVisualContinuity::Vec3 ToContinuityVec3(const Vector& value)
	{
		return { value.x, value.y, value.z };
	}

	bool TrySyncCommittedMoveBeforeControlledPrediction(
		const char* phaseName,
		CUserCmd* ucmd,
		CMoveData* move)
	{
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
		const float originDeltaSqr = (originBefore - committedOrigin).LenghtSqr();
		const bool syncApplied =
			PortalTransitionDecision::ShouldSyncCommittedMoveBeforeControlledMove(
				context.phase,
				originDeltaSqr);
		if (syncApplied)
		{
			move->SetAbsOrigin(committedOrigin);
			move->m_vecVelocity = committedVelocity;
			move->m_vecViewAngles = committedAngles;
			move->m_vecAbsViewAngles = committedAngles;
			move->m_vecAngles = committedAngles;
			if (PortalTransitionDecision::ShouldSyncCommittedViewAnglesWithMove(
				context.phase,
				true))
			{
				ucmd->viewangles =
					Vector(committedAngles.x, committedAngles.y, committedAngles.z);
			}
			if (I::EngineClient)
			{
				Vector engineAngles(
					committedAngles.x,
					committedAngles.y,
					committedAngles.z);
				I::EngineClient->SetViewAngles(engineAngles);
			}
		}

		const Vector originAfter = move->GetAbsOrigin();
		const Vector velocityAfter = move->m_vecVelocity;
		const Vector commandAnglesAfter = ucmd->viewangles;

		PortalVisualContinuity::Sample continuitySample;
		continuitySample.playerOrigin = ToContinuityVec3(originAfter);
		continuitySample.velocity = ToContinuityVec3(velocityAfter);
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
		continuityDetails.syncApplied = syncApplied;
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

		U::LogInfo("[PortalPredictionSync] txn=%u phase=%s cmd=%d ctxPhase=%d syncApplied=%s expectedOrigin=(%.1f %.1f %.1f) moveOrigin=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) expectedVelocity=(%.1f %.1f %.1f) moveVelocity=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) delta=%.1f expectedAngles=(%.1f %.1f %.1f) cmdAngles=(%.1f %.1f %.1f)->(%.1f %.1f %.1f).\n",
			continuityTransactionId,
			phaseName ? phaseName : "unknown",
			ucmd->command_number,
			static_cast<int>(context.phase),
			syncApplied ? "true" : "false",
			committedOrigin.x, committedOrigin.y, committedOrigin.z,
			originBefore.x, originBefore.y, originBefore.z,
			originAfter.x, originAfter.y, originAfter.z,
			committedVelocity.x, committedVelocity.y, committedVelocity.z,
			velocityBefore.x, velocityBefore.y, velocityBefore.z,
			velocityAfter.x, velocityAfter.y, velocityAfter.z,
			std::sqrt(originDeltaSqr),
			committedAngles.x, committedAngles.y, committedAngles.z,
			commandAnglesBefore.x, commandAnglesBefore.y, commandAnglesBefore.z,
			commandAnglesAfter.x, commandAnglesAfter.y, commandAnglesAfter.z);
		return syncApplied;
	}
}

void __fastcall ClientPrediction::RunCommand::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper)
{
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, moveHelper);
}

void __fastcall ClientPrediction::SetupMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move)
{
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, pHelper, move);
	TrySyncCommittedMoveBeforeControlledPrediction("setupmove", ucmd, move);
}

void __fastcall ClientPrediction::FinishMove::Detour(void* ecx, void* edx, C_BasePlayer* player, CUserCmd* ucmd, CMoveData* move)
{
	const PortalMoveFrameDiagnostics before = G::G_L4D2Portal.m_PortalStage1Probe.CaptureMoveFrame(
		player,
		ucmd,
		move,
		G::G_L4D2Portal.m_PortalTransitionSimulator);
	Table.Original<FN>(Index)(ecx, edx, player, ucmd, move);
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
