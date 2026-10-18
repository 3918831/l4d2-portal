#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
namespace PortalNativePredictionErrorPolicy
{
    struct PacketScope
    {
        std::uintptr_t prediction=0,localPlayer=0;std::uint32_t sequence=0;int ack=-1;
        bool active=false,identity=false;
        std::uint64_t epoch=0;
    };
    struct Observation
    {
        std::uintptr_t localPlayer=0,entity=0,map=0;
        std::uint32_t capture=0,transaction=0,sequence=0,packet=0;
        std::uint64_t epoch=0;
        int frame=-1,tick=-1,ack=-1,packetAck=-1,index=-2,errors=-1;
        unsigned comparisonCalls=0;
        unsigned nativeMessages=0,nativeFields=0;
        float time=NAN;
        char type[96]="unknown";
        bool active=false,contextValid=false,callerMatched=false,typeKnown=false,indexKnown=false,countKnown=false,hadErrors=false;
    };
    enum class Outcome : unsigned {Invalid,NotChecked,NoErrors,Errors};
    template<class T> inline bool InLifecycle(const T& o,std::uint64_t epoch)
    {return epoch&&o.epoch==epoch;}
    inline bool NeedsLifecycleReset(bool enabled,std::uintptr_t previous,std::uintptr_t current,bool retained)
    {return !enabled||!current ? previous||retained : previous&&previous!=current;}
    inline bool Eligible(const PacketScope& p,std::uintptr_t prediction,std::uintptr_t local,
        std::uintptr_t caller,std::uintptr_t expectedCaller)
    {return p.active&&p.identity&&prediction&&local&&p.prediction==prediction&&p.localPlayer==local
        &&expectedCaller&&caller==expectedCaller;}
    inline Outcome Classify(const Observation& o)
    {
        if(!o.contextValid||!o.callerMatched||!o.localPlayer||!o.entity||o.frame<0||o.tick<0||o.ack<0||!std::isfinite(o.time)) return Outcome::Invalid;
        if(!o.ack) return !o.comparisonCalls&&!o.hadErrors?Outcome::NotChecked:Outcome::Invalid;
        if(o.comparisonCalls!=1||!o.countKnown||o.errors<0||o.hadErrors!=(o.errors>0)) return Outcome::Invalid;
        return o.errors?Outcome::Errors:Outcome::NoErrors;
    }
    inline bool InCapture(const Observation& o,bool active,std::uint32_t capture)
    {return o.active&&active&&o.capture==capture;}
    inline bool PreludeMatches(const Observation& o,std::uintptr_t player,int frame,int tick)
    {return player&&o.localPlayer==player&&o.frame>=0&&o.tick>=0&&frame>=o.frame&&tick>=o.tick
        &&static_cast<std::int64_t>(tick)-o.tick<=4;}
    struct Prelude
    {
        std::array<Observation,8> rows;unsigned count=0;
        static unsigned Priority(const Observation& o)
        {return o.hadErrors||Classify(o)==Outcome::Errors?2:Classify(o)==Outcome::Invalid?1:0;}
        void Add(const Observation& o)
        {
            if(count<rows.size()){rows[count++]=o;return;}
            unsigned lowest=0;
            for(unsigned i=1;i<rows.size();++i) if(Priority(rows[i])<Priority(rows[lowest])) lowest=i;
            if(Priority(o)<Priority(rows[lowest])) return;
            for(unsigned i=lowest+1;i<rows.size();++i) rows[i-1]=rows[i];
            rows.back()=o; // chronological output; errors/invalid precede eviction of clean rows
        }
    };
    struct Entity
    {
        std::uintptr_t address=0;int index=-2;char type[96]="unknown";bool typeKnown=false,indexKnown=false;
        unsigned calls=0,outcomes[4]={};std::uint64_t errorSum=0;
        int lastTick=-1,lastErrors=-1;unsigned lastOutcome=0;
    };
    struct Window
    {
        unsigned calls=0,outcomes[4]={},hadErrorsObserved=0,localCalls=0,localErrors=0,unknownType=0;
        unsigned packetGroups=0,entityOverflow=0,emitted=0,dropped=0;
        unsigned nativeMessages=0,nativeFields=0,positiveWithoutNativeField=0;
        std::uint32_t lastPacket=0;std::uint64_t errorSum=0;
        std::array<Entity,16> entities;
        bool Add(const Observation& o)
        {
            const auto outcome=static_cast<unsigned>(Classify(o));++calls;++outcomes[outcome];hadErrorsObserved+=o.hadErrors;
            nativeMessages+=o.nativeMessages;nativeFields+=o.nativeFields;positiveWithoutNativeField+=outcome==3&&!o.nativeFields;
            const bool local=o.entity&&o.localPlayer&&o.entity==o.localPlayer;
            localCalls+=local;localErrors+=local&&outcome==3;unknownType+=!o.typeKnown;
            if(o.packet!=lastPacket) ++packetGroups;lastPacket=o.packet;
            if(outcome==3) errorSum+=static_cast<unsigned>(o.errors);
            Entity* bucket=nullptr;
            for(auto& e:entities) if(e.calls&&e.address==o.entity) {bucket=&e;break;}
            bool first=false;
            if(!bucket) for(auto& e:entities) if(!e.calls) {bucket=&e;e.address=o.entity;first=true;break;}
            bool emit=!bucket||first;
            if(!bucket) ++entityOverflow;
            else
            {
                emit=emit||bucket->lastOutcome!=outcome||bucket->lastErrors!=o.errors||(outcome==3&&bucket->lastTick!=o.tick);
                ++bucket->calls;++bucket->outcomes[outcome];if(outcome==3) bucket->errorSum+=static_cast<unsigned>(o.errors);
                if(o.typeKnown){std::memcpy(bucket->type,o.type,sizeof(bucket->type));bucket->typeKnown=true;}
                if(o.indexKnown){bucket->index=o.index;bucket->indexKnown=true;}
                bucket->lastTick=o.tick;bucket->lastErrors=o.errors;bucket->lastOutcome=outcome;
            }
            if(!emit) return false;
            if(emitted>=48){++dropped;return false;}++emitted;return true;
        }
    };
}
