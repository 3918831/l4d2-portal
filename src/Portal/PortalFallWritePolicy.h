#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace PortalFallWritePolicy
{
    struct Lifetime
    {
        std::uintptr_t player=0;unsigned epoch=0;int committedCmd=0;bool active=false;
        void Arm(std::uintptr_t p,unsigned e,int cmd){player=p;epoch=e;committedCmd=cmd;active=p&&e&&cmd>0;}
        bool Continue(std::uintptr_t p,int cmd,bool valid,bool alive,int moveType,bool onGround)
        {
            if(!active)return false;
            if(p!=player||!valid||!alive||moveType!=2||onGround){active=false;return false;}
            return cmd>0&&cmd>=committedCmd;
        }
    };
    struct Result {bool candidate=false,changed=false,notified=false,written=false,readback=false;float value=NAN;};
    template<class Notify,class Store,class Readback>
    Result Apply(bool enabled,bool ready,float old,float z,Notify notify,Store store,Readback readback)
    {
        Result r;
        // Signed network fall range; reject unsupported speeds, never clamp.
        if(!ready||!std::isfinite(old)||!std::isfinite(z)||std::fabs(z)>4096.f)return r;
        r.candidate=true;r.value=-z;r.changed=std::memcmp(&old,&r.value,4)!=0;
        if(!enabled||!r.changed)return r;
        r.notified=notify();if(!r.notified)return r;
        r.written=store(r.value);if(r.written)r.readback=readback(r.value);
        return r;
    }
    struct Runtime
    {
        bool enabled=true,ready=false;
        Lifetime lifetime;
        const char* reason="awaiting-commit";
        void Reset(const char* why) { lifetime={};ready=false;reason=why?why:"reset"; }
        template<class Notify,class Store,class Readback>
        Result Apply(bool writable,float old,float z,Notify notify,Store store,Readback readback) const
        { return PortalFallWritePolicy::Apply(enabled,ready&&writable,old,z,notify,store,readback); }
    };
}
