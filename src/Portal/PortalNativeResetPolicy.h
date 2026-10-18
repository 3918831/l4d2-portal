#pragma once
#include "PortalNativeOriginPolicy.h"
#include <array>
#include <cstring>

namespace PortalNativeResetPolicy
{
    using PortalNativeOriginPolicy::Ring;
    using PortalNativeOriginPolicy::Sample;
    using PortalNativeOriginPolicy::Vec3;
    using PortalNativeOriginPolicy::Finite;
    enum class Phase : unsigned {Unscoped,RunCommand,SetupMove,FinishMove};
    struct Scope {std::uintptr_t player=0;int command=-1;Phase phase=Phase::Unscoped;};
    struct ParentScope
    {
        std::uintptr_t player=0,module=0;std::uint32_t callerRva=0;char moduleName[64]="unknown";
        bool active=false,identity=false,callerValid=false;
    };
    enum class ParentKind : unsigned {Unavailable,Unscoped,Mismatch,InvalidCaller,Matched};
    struct Snapshot
    {
        Ring ring;std::array<Sample,3> samples;
        Vec3 value={NAN,NAN,NAN};bool valid=false;
    };
    struct Observation
    {
        std::uintptr_t player=0,watcher=0,module=0;
        std::uint32_t callerRva=0,capture=0,transaction=0,sequence=0;
        char moduleName[64]="unknown";
        int frame=-1,tick=-1,command=-1,latestInput=-1,tickBase=-1,flags=0,effects=0,moveType=-1;
        Phase phase=Phase::Unscoped;
        float nativeTime=NAN,nativeAfter=NAN,sdkTime=NAN,simulation=NAN;
        bool active=false,contextValid=false,identity=false,callerValid=false,clockValid=false,globalsMatched=false;
        bool predictionValid=false,inPrediction=false,firstPrediction=false,insideRender=false;
        unsigned failure=0;Snapshot before,after;
        ParentScope parent;bool parentInstalled=false,parentReturnMatched=false;
        int resetLatchMode=-1;bool resetLatchModeKnown=false;
    };
    inline ParentKind ParentState(const Observation& o)
    {
        if(!o.parentInstalled) return ParentKind::Unavailable;
        if(!o.parent.active) return ParentKind::Unscoped;
        if(!o.player||o.parent.player!=o.player||!o.parent.identity||!o.parentReturnMatched) return ParentKind::Mismatch;
        return o.parent.callerValid?ParentKind::Matched:ParentKind::InvalidCaller;
    }
    inline bool Safe(const Snapshot& s)
    {
        if(!s.valid||!PortalNativeOriginPolicy::ValidRing(s.ring)||!Finite(s.value)) return false;
        const auto n=std::min<unsigned>(3,s.ring.count);
        for(unsigned i=0;i<n;++i) if(!s.samples[i].valid||s.samples[i].index!=static_cast<int>(i)
            ||!std::isfinite(s.samples[i].time)||!Finite(s.samples[i].position)) return false;
        return true;
    }
    inline bool ThreeClones(const Snapshot& s)
    {
        if(!Safe(s)||s.ring.count!=3) return false;
        for(unsigned i=0;i<3;++i) if(s.samples[i].time!=s.samples[0].time
            ||s.samples[i].position.x!=s.value.x||s.samples[i].position.y!=s.value.y||s.samples[i].position.z!=s.value.z) return false;
        return true;
    }
    inline bool FirstInterval(const Snapshot& s)
    {return Safe(s)&&s.ring.count>=2&&s.samples[0].time>s.samples[1].time;}
    inline bool InCapture(const Observation& o,bool active,std::uint32_t capture)
    {return o.active&&active&&o.capture==capture;}
    inline bool PreludeMatches(const Observation& o,std::uintptr_t player,int frame,int tick)
    {return player&&o.player==player&&o.frame>=0&&o.tick>=0&&frame>=o.frame&&tick>=o.tick
        &&static_cast<std::int64_t>(tick)-o.tick<=4;}
    inline bool Valid(const Observation& o)
    {return !o.failure&&o.contextValid&&o.identity&&o.callerValid&&o.clockValid
        &&static_cast<unsigned>(o.phase)<4&&o.frame>=0&&o.tick>=0
        &&std::isfinite(o.nativeTime)&&std::isfinite(o.nativeAfter)&&std::isfinite(o.sdkTime)&&std::isfinite(o.simulation)
        &&Safe(o.before)&&Safe(o.after);}
    struct Caller
    {
        std::uintptr_t module=0;std::uint32_t rva=0;char name[64]={};
        unsigned calls=0,invalid=0,clones=0,phases[4]={};
    };
    inline bool AddCaller(std::array<Caller,16>& callers,std::uintptr_t module,std::uint32_t rva,
        const char (&name)[64],bool valid,bool rebuilt,unsigned phase)
    {
        Caller* bucket=nullptr;
        for(auto& c:callers) if(c.calls&&c.module==module&&c.rva==rva) {bucket=&c;break;}
        if(!bucket) for(auto& c:callers) if(!c.calls)
        {bucket=&c;c.module=module;c.rva=rva;std::memcpy(c.name,name,sizeof(c.name));break;}
        if(!bucket) return false;
        ++bucket->calls;bucket->invalid+=!valid;bucket->clones+=rebuilt;if(phase<4) ++bucket->phases[phase];return true;
    }
    struct Window
    {
        unsigned calls=0,invalid=0,clones=0,lostInterval=0,valueChanged=0,unknownCaller=0,callerOverflow=0;
        unsigned emitted=0,dropped=0,frameGroups=0,tickGroups=0,commandGroups=0,commandUnknown=0,phases[4]={},phaseInvalid=0;
        int lastFrame=-1,lastTick=-1,lastCommand=-1;
        std::uintptr_t lastModule=0;std::uint32_t lastRva=0;
        unsigned lastPhase=0,lastShape=0;float lastNativeTime=NAN;
        std::array<Caller,16> callers;
        std::array<Caller,16> parentCallers;unsigned parentStates[5]={},parentOverflow=0;
        unsigned lastParentState=0;std::uintptr_t lastParentModule=0;std::uint32_t lastParentRva=0;
        unsigned resetModes[5]={};int lastResetMode=-1;bool lastResetModeKnown=false;
        bool Add(const Observation& o)
        {
            ++calls;const bool valid=Valid(o),rebuilt=valid&&ThreeClones(o.after);
            invalid+=!valid;clones+=rebuilt;lostInterval+=rebuilt&&FirstInterval(o.before);
            const unsigned resetMode=!o.resetLatchModeKnown?0:o.resetLatchMode==0?1:o.resetLatchMode==1?2:o.resetLatchMode==2?3:4;
            ++resetModes[resetMode];
            if(valid&&(o.before.value.x!=o.after.value.x||o.before.value.y!=o.after.value.y
                ||o.before.value.z!=o.after.value.z)) ++valueChanged;
            const auto phase=static_cast<unsigned>(o.phase);
            if(phase<4) ++phases[phase];else ++phaseInvalid;
            if(o.frame!=lastFrame) ++frameGroups;if(o.tick!=lastTick) ++tickGroups;
            if(o.command<0) ++commandUnknown;else if(o.command!=lastCommand) ++commandGroups;
            if(!o.callerValid) ++unknownCaller;
            else if(!AddCaller(callers,o.module,o.callerRva,o.moduleName,valid,rebuilt,phase)) ++callerOverflow;
            const auto parent=static_cast<unsigned>(ParentState(o));++parentStates[parent];
            if(parent==static_cast<unsigned>(ParentKind::Matched)
                &&!AddCaller(parentCallers,o.parent.module,o.parent.callerRva,o.parent.moduleName,valid,rebuilt,phase)) ++parentOverflow;
            const unsigned shape=unsigned(valid)|(unsigned(rebuilt)<<1)|(unsigned(FirstInterval(o.before))<<2);
            const bool edge=calls==1||o.tick!=lastTick||o.command!=lastCommand||o.module!=lastModule||o.callerRva!=lastRva
                ||phase!=lastPhase||shape!=lastShape||o.nativeTime!=lastNativeTime
                ||parent!=lastParentState||o.parent.module!=lastParentModule||o.parent.callerRva!=lastParentRva
                ||o.resetLatchModeKnown!=lastResetModeKnown||o.resetLatchMode!=lastResetMode;
            lastFrame=o.frame;lastTick=o.tick;lastCommand=o.command;lastModule=o.module;lastRva=o.callerRva;
            lastPhase=phase;lastShape=shape;lastNativeTime=o.nativeTime;
            lastParentState=parent;lastParentModule=o.parent.module;lastParentRva=o.parent.callerRva;
            lastResetModeKnown=o.resetLatchModeKnown;lastResetMode=o.resetLatchMode;
            if(!edge) return false;
            if(emitted>=48) {++dropped;return false;}
            ++emitted;return true;
        }
    };
}
