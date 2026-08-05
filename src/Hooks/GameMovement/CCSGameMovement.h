#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace CCSGameMovement
	{

		inline Hook::CTable ServerTable;
		inline Hook::CTable ClientTable;

		namespace TracePlayerBBox
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(void*, void*, const Vector&, const Vector&, unsigned int, int, trace_t*);
			constexpr uint32_t Index = 16u;

			void __fastcall Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm);
		}

		namespace ServerTracePlayerBBox
		{
			using FN = TracePlayerBBox::FN;
			constexpr uint32_t Index = TracePlayerBBox::Index;

			void __fastcall Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm);
		}

		namespace ClientTracePlayerBBox
		{
			using FN = TracePlayerBBox::FN;
			constexpr uint32_t Index = TracePlayerBBox::Index;

			void __fastcall Detour(void* ecx, void* edx, const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t* pm);
		}

		namespace TryPlayerMove
		{
			using FN = int(__fastcall*)(void*, void*, Vector*, trace_t*);
			constexpr uint32_t Index = 38u;

			int __fastcall Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace);
		}

		namespace StepMove
		{
			using FN = void(__fastcall*)(void*, void*, Vector&, trace_t&);
			constexpr uint32_t Index = 64u;

			void __fastcall Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace);
		}

		namespace ClientTryPlayerMove
		{
			using FN = TryPlayerMove::FN;
			constexpr uint32_t Index = TryPlayerMove::Index;

			int __fastcall Detour(void* ecx, void* edx, Vector* pFirstDest, trace_t* pFirstTrace);
		}

		namespace ClientStepMove
		{
			using FN = StepMove::FN;
			constexpr uint32_t Index = StepMove::Index;

			void __fastcall Detour(void* ecx, void* edx, Vector& vecDestination, trace_t& trace);
		}

		namespace PlayerMove
		{
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 18u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace WalkMove
		{
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 28u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace FullWalkMove
		{
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 30u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace CategorizePosition
		{
			inline Hook::CFunction Func;
			using FN = void(__fastcall*)(void*, void*);
			constexpr uint32_t Index = 52u;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace ClientPlayerMove
		{
			using FN = PlayerMove::FN;
			constexpr uint32_t Index = PlayerMove::Index;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace ClientProcessMovement
		{
			using FN = void(__fastcall*)(void*, void*, C_BasePlayer*, CMoveData*);
			constexpr uint32_t Index = 1u;

			void __fastcall Detour(
				void* ecx,
				void* edx,
				C_BasePlayer* player,
				CMoveData* move);
		}

		namespace ClientWalkMove
		{
			using FN = WalkMove::FN;
			constexpr uint32_t Index = WalkMove::Index;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace ClientFullWalkMove
		{
			using FN = FullWalkMove::FN;
			constexpr uint32_t Index = FullWalkMove::Index;

			void __fastcall Detour(void* ecx, void* edx);
		}

		namespace ClientCategorizePosition
		{
			using FN = CategorizePosition::FN;
			constexpr uint32_t Index = CategorizePosition::Index;

			void __fastcall Detour(void* ecx, void* edx);
		}

		void Init();
	}
}
