#pragma once
#include "PortalNativeCadence.h"
#include <array>
#include <cstddef>
namespace PortalNativeInterpolationPolicy
{
    using Vec3 = PortalNativeCadence::Vec3;
    // Full C_BaseAnimating::Interpolate body in the supported binary. Contains
    // only relative calls, no ASLR-sensitive absolute operands. An update or an
    // existing detour fails closed; these bytes are never executed by the probe.
    inline bool ValidParent(const unsigned char* p,std::size_t size)
    {
        constexpr unsigned char body[] = {0x55,0x8b,0xec,0x83,0xec,0x20,0x56,0x8b,0xf1,0x83,0xbe,0x3c,0x06,0x00,0x00,0x00,0x0f,0x85,0x96,0x00,0x00,0x00,0xf3,0x0f,0x10,0x86,0x5c,0x06,0x00,0x00,0x57,0x33,0xff,0x80,0xbe,0x7c,0x08,0x00,0x00,0x00,0xf3,0x0f,0x11,0x45,0xf8,0x75,0x14,0x8b,0x86,0xa4,0x08,0x00,0x00,0x50,0xe8,0xe5,0xea,0xff,0xff,0x8b,0x8e,0xc8,0x08,0x00,0x00,0x88,0x01,0x8d,0x55,0xfc,0x52,0x8d,0x45,0xec,0x50,0x8d,0x4d,0xe0,0x51,0x8d,0x55,0x08,0x52,0x8b,0xce,0xe8,0x66,0x49,0x01,0x00,0x85,0xc0,0x75,0x17,0x39,0x7d,0xfc,0x74,0x48,0x50,0x8b,0xce,0xe8,0x25,0x92,0x01,0x00,0x5f,0xb0,0x01,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00,0xf3,0x0f,0x10,0x86,0x5c,0x06,0x00,0x00,0x0f,0x2e,0x45,0xf8,0x9f,0xf6,0xc4,0x44,0x7b,0x05,0xbf,0x08,0x00,0x00,0x00,0x83,0x7d,0xfc,0x00,0x74,0x09,0x6a,0x00,0x8b,0xce,0xe8,0xf5,0x91,0x01,0x00,0x57,0x8d,0x45,0xec,0x50,0x8d,0x4d,0xe0,0x51,0x8b,0xce,0xe8,0x25,0xe2,0x00,0x00,0x5f,0xb0,0x01,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00};
        if(!p || size<sizeof(body)) return false;
        for(std::size_t i=0;i<sizeof(body);++i) if(p[i]!=body[i]) return false;
        return true;
    }
    // CTerrorPlayer::Interpolate: forwards one stack float with unchanged ECX,
    // canonicalizes AL to bool and returns with ret 4. Validate rel32 separately.
    inline bool ValidEntry(const unsigned char* p, std::size_t size)
    {
        constexpr unsigned char signature[] = {0x55,0x8b,0xec,0xd9,0x45,0x08,0x51,0xd9,0x1c,0x24,
            0xe8,0,0,0,0,0x84,0xc0,0x0f,0x95,0xc0,0x5d,0xc2,0x04,0x00};
        if (!p || size<sizeof(signature)) return false;
        for (std::size_t i=0;i<sizeof(signature);++i)
            if ((i<11 || i>14) && p[i]!=signature[i]) return false;
        return true;
    }
    inline bool Finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
    inline float Distance(Vec3 a, Vec3 b)
    {
        const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
        return std::sqrt(x*x+y*y+z*z);
    }
    struct Calls
    {
        int count=0, insideRender=0, returnedTrue=0, changed=0, invalid=0;
        float firstTime=0,lastTime=0,lastGlobalTime=0,maxDelta=0;
        Vec3 firstBefore,lastAfter;
        Vec3 lastRenderAfter;
        int validInsideRender=0;
        void Add(float time,float globalTime,Vec3 before,Vec3 after,bool result,bool inRender)
        {
            ++count; if(inRender) ++insideRender; if(result) ++returnedTrue;
            if(!std::isfinite(time)||!std::isfinite(globalTime)||!Finite(before)||!Finite(after)) {++invalid;return;}
            if(count-invalid==1) {firstTime=time;firstBefore=before;}
            lastTime=time;lastGlobalTime=globalTime;lastAfter=after;
            if(inRender) {++validInsideRender;lastRenderAfter=after;}
            const float delta=Distance(before,after);
            if(delta>0) ++changed;
            maxDelta=std::max(maxDelta,delta);
        }
    };
    inline unsigned Classify(const Calls& n,bool installed,bool dispatch,bool observed,bool comparable,float difference)
    {
        return unsigned(installed)|(unsigned(dispatch)<<1)|(unsigned(observed)<<2)
            |(unsigned(std::min(n.count,2))<<3)|(unsigned(std::min(n.changed,2))<<5)
            |(unsigned(std::min(n.insideRender,2))<<7)|(unsigned(n.invalid>0)<<9)
            |(unsigned(comparable)<<10)|(unsigned(comparable&&difference>.002f)<<11);
    }
    struct Window
    {
        unsigned frames=0,noCall=0,calls=0,changed=0,invalid=0,comparable=0,different=0;
        unsigned changedOriginalFrozen=0,changedFinalFrozen=0;
        float maxDifference=0;
        void Add(const Calls& n,bool observed,bool afterComparable,float difference,bool originalFrozen,bool finalFrozen)
        {
            if(observed) {++frames;if(!n.insideRender) ++noCall;}
            calls+=n.count;changed+=n.changed;invalid+=n.invalid;
            if(afterComparable&&std::isfinite(difference))
            {
                ++comparable;if(difference>.002f) ++different;
                maxDifference=std::max(maxDifference,difference);
            }
            if(n.changed&&originalFrozen) ++changedOriginalFrozen;
            if(n.changed&&finalFrozen) ++changedFinalFrozen;
        }
    };
    struct StageKey { std::array<float,19> scalars{}; std::array<int,9> integers{}; bool valid=false; };
    inline bool SameStage(const StageKey& a, const StageKey& b)
    { return a.valid==b.valid && a.scalars==b.scalars && a.integers==b.integers; }
    struct CommandBatch
    {
        int count=0,first=0,last=0;
        float minSpeed=0,maxSpeed=0;
        void Add(int command,float speed)
        {
            if(!count) {first=command;minSpeed=maxSpeed=speed;}
            ++count;last=command;minSpeed=std::min(minSpeed,speed);maxSpeed=std::max(maxSpeed,speed);
        }
        bool Ready() const { return count>=8; }
    };
}
