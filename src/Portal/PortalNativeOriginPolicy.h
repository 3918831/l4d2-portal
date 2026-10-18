#pragma once
#include "PortalNativeInterpolationPolicy.h"

namespace PortalNativeOriginPolicy
{
    using PortalNativeInterpolationPolicy::Vec3;
    using PortalNativeInterpolationPolicy::Finite;
    using PortalNativeInterpolationPolicy::Distance;
    struct Ring {std::uint32_t base=0;std::uint16_t capacity=0,head=0,count=0;};
    struct Sample {int index=-1;float time=NAN;Vec3 position={NAN,NAN,NAN};bool valid=false;};
    enum class SelectionState : unsigned {Unknown,Invalid,Empty,NoSelection,Clamped,Repeated,Interval,Hermite};
    struct Selection
    {
        bool called=false,callerMatched=false,outputsValid=false,ringValid=false,samplesValid=false,returned=false,hermite=false;
        float time=NAN,amount=NAN,effective=NAN,fraction=NAN;
        int noMore=-1,newerIndex=-1,olderIndex=-1,oldestIndex=-1;
        unsigned failure=0;
        Ring ring;Sample newest,newer,older,oldest;
    };
    struct Observation
    {
        bool installed=false,called=false,valid=false,identity=false,callerMatched=false;
        float time=NAN;int noMore=-1;
        unsigned failure=0;
        Vec3 before={NAN,NAN,NAN},after={NAN,NAN,NAN};
        Selection selection;
    };
    inline bool ValidRing(const Ring& r)
    {
        if(!r.capacity) return !r.base&&!r.head&&!r.count;
        return r.base && r.capacity<=4096 && r.head<r.capacity && r.count<=r.capacity
            &&static_cast<std::uint64_t>(r.base)+r.capacity*16u<=UINT32_MAX;
    }
    inline std::uintptr_t SampleAddress(const Ring& r,int index)
    {
        if(!ValidRing(r)||index<0||index>=r.count) return 0;
        const auto physical=(r.head+static_cast<unsigned>(index))%r.capacity;
        return r.base+physical*16u;
    }
    inline bool MatchesScope(std::uintptr_t watcher,std::uintptr_t expected,std::uintptr_t value,std::uintptr_t entry,
        std::uintptr_t caller,std::uintptr_t expectedCaller,float time,float selected)
    {return watcher&&watcher==expected&&value&&value==entry&&caller&&caller==expectedCaller
        &&std::isfinite(time)&&time==selected;}
    inline bool OutputsValid(const Selection& s)
    {
        const auto index=[&](int i){return i==-1||(i>=0&&i<s.ring.count);};
        return s.callerMatched&&ValidRing(s.ring)&&std::isfinite(s.time)&&std::isfinite(s.amount)&&s.amount>=0
            &&std::isfinite(s.effective)&&s.effective==s.time-s.amount
            &&std::isfinite(s.fraction)&&s.noMore>=0&&s.noMore<=1
            &&index(s.newerIndex)&&index(s.olderIndex)&&index(s.oldestIndex)
            &&(!s.returned||(s.newerIndex>=0&&s.olderIndex>=0&&(!s.hermite||s.oldestIndex>=0)));
    }
    inline SelectionState State(const Selection& s)
    {
        if(!s.called) return SelectionState::Unknown;
        if(s.failure||!s.outputsValid||!s.ringValid||!OutputsValid(s)) return SelectionState::Invalid;
        if(!s.ring.count) return SelectionState::Empty;
        const auto valid=[](const Sample& p,int index){return p.valid&&p.index==index&&std::isfinite(p.time)&&Finite(p.position);};
        if(!s.samplesValid||!valid(s.newest,0)) return SelectionState::Invalid;
        if(!s.returned) return SelectionState::NoSelection;
        if(!valid(s.newer,s.newerIndex)||!valid(s.older,s.olderIndex)
            ||(s.hermite&&!valid(s.oldest,s.oldestIndex))) return SelectionState::Invalid;
        if(s.newerIndex==s.olderIndex) return SelectionState::Clamped;
        if(Distance(s.newer.position,s.older.position)==0
            &&(!s.hermite||Distance(s.oldest.position,s.newer.position)==0)) return SelectionState::Repeated;
        return s.hermite ? SelectionState::Hermite : SelectionState::Interval;
    }
    struct Calls
    {
        unsigned calls=0,invalid=0,changed=0,noChange=0,noMoreTrue=0,noMoreFalse=0;
        unsigned selectionCalls=0,selectionInvalid=0,selectionTrue=0,selectionFalse=0,missingSelection=0;
        unsigned states[8]={};
        bool hasAmount=false,hasCount=false;
        float minAmount=0,maxAmount=0;unsigned minCount=0,maxCount=0;
        Observation last;
        void Add(const Observation& o)
        {
            if(!o.called) return;
            ++calls;last=o;
            if(!o.valid||!Finite(o.before)||!Finite(o.after)||!std::isfinite(Distance(o.before,o.after))) ++invalid;
            else
            {
                if(Distance(o.before,o.after)>0) ++changed;else ++noChange;
                noMoreTrue+=o.noMore==1;noMoreFalse+=o.noMore==0;
            }
            const auto& s=o.selection;
            if(!s.called) {++missingSelection;return;}
            ++selectionCalls;const auto state=State(s);++states[unsigned(state)];
            if(state==SelectionState::Invalid) {++selectionInvalid;return;}
            selectionTrue+=s.returned;selectionFalse+=!s.returned;
            if(!hasAmount) {minAmount=maxAmount=s.amount;hasAmount=true;}
            else {minAmount=std::min(minAmount,s.amount);maxAmount=std::max(maxAmount,s.amount);}
            if(!hasCount) {minCount=maxCount=s.ring.count;hasCount=true;}
            else {minCount=std::min<unsigned>(minCount,s.ring.count);maxCount=std::max<unsigned>(maxCount,s.ring.count);}
        }
        void Merge(const Calls& c)
        {
            calls+=c.calls;invalid+=c.invalid;changed+=c.changed;noChange+=c.noChange;
            noMoreTrue+=c.noMoreTrue;noMoreFalse+=c.noMoreFalse;
            selectionCalls+=c.selectionCalls;selectionInvalid+=c.selectionInvalid;
            selectionTrue+=c.selectionTrue;selectionFalse+=c.selectionFalse;missingSelection+=c.missingSelection;
            for(unsigned i=0;i<8;++i) states[i]+=c.states[i];
            if(c.calls) last=c.last;
            if(c.hasAmount)
            {
                if(!hasAmount) {minAmount=c.minAmount;maxAmount=c.maxAmount;hasAmount=true;}
                else {minAmount=std::min(minAmount,c.minAmount);maxAmount=std::max(maxAmount,c.maxAmount);}
            }
            if(c.hasCount)
            {
                if(!hasCount) {minCount=c.minCount;maxCount=c.maxCount;hasCount=true;}
                else {minCount=std::min(minCount,c.minCount);maxCount=std::max(maxCount,c.maxCount);}
            }
        }
    };
    inline unsigned Classify(const Calls& c)
    {return unsigned(c.calls>0)|(unsigned(c.invalid>0)<<1)|(unsigned(c.changed>0)<<2)
        |(unsigned(c.selectionCalls>0)<<3)|(unsigned(c.selectionInvalid>0)<<4)
        |(unsigned(c.last.selection.returned)<<5)|(unsigned(State(c.last.selection))<<6);}
}
