#include "BasePlayer.h"

#include "../../Features/Vars.h"
#pragma warning(push)
#pragma warning(disable: 4819)
#include "../../Portal/L4D2_Portal.h"
#pragma warning(pop)

using namespace Hooks;

void __fastcall BasePlayer::CalcPlayerView::Detour(C_BasePlayer* pThis, void* edx, Vector& eyeOrigin, Vector& eyeAngles, float& fov)
{
	// Preserve the actual CalcPlayerView input for the render-source cadence
	// diagnostic before the native view calculation mutates it.
	const Vector calcViewInputEye = eyeOrigin;

	if (pThis && !pThis->deadflag()) //Thanks Spook for telling me to do it here.
	{
		const Vector vOldPunch = pThis->GetPunchAngle();

		pThis->m_vecPunchAngle().Init();
		Func.Original<FN>()(pThis, edx, eyeOrigin, eyeAngles, fov);
		pThis->m_vecPunchAngle() = vOldPunch;
	}
	else
	{
		Func.Original<FN>()(pThis, edx, eyeOrigin, eyeAngles, fov);
	}
	const Vector originalViewEye = eyeOrigin;

	// Consume the transaction matrix before the geometric entry handoff. This
	// keeps the stored eye history in physical entry space until Teleport commits,
	// matching Portal's DetectAndHandlePortalTeleportation/CalcView ordering.
	G::G_L4D2Portal.m_PortalTransition.ApplyPortalEyeContinuity(
		pThis,
		eyeOrigin,
		eyeAngles);

	// Match Portal's client-side CalcPortalView handoff: once the local eye has
	// crossed the entry plane, render from its linked-space transform while the
	// physical movement command is still waiting to commit Teleport.
	G::G_L4D2Portal.m_PortalTransition.ApplyEntryCameraHandoff(
		pThis,
		eyeOrigin,
		eyeAngles,
		fov);

	// Preserve the CalcPlayerView seam separately. The final CViewSetup origin is
	// sampled later at the main-view submission point, after all view adapters.
	G::G_L4D2Portal.m_PortalTransition.CapturePortalRenderCadenceCalcView(
		pThis,
		calcViewInputEye,
		originalViewEye,
		eyeOrigin);

}

void BasePlayer::Init()
{
	//CalcPlayerView
	{
		using namespace CalcPlayerView;

		const FN pfCalcPlayerView = reinterpret_cast<FN>(U::Offsets.m_dwCalcPlayerView);
		XASSERT(pfCalcPlayerView == nullptr);

		if (pfCalcPlayerView)
			XASSERT(Func.Init(pfCalcPlayerView, &Detour) == false);
	}
}
