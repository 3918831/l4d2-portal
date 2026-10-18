#pragma once
#include "PortalNativeInterpolationPolicy.h"
#include "PortalNativeOriginPolicy.h"
#include <cstring>

namespace PortalNativePart1Policy
{
    using PortalNativeInterpolationPolicy::Vec3;
    using PortalNativeInterpolationPolicy::Finite;
    using PortalNativeInterpolationPolicy::Distance;
    constexpr std::size_t kBodySize=350;
    constexpr std::size_t kParentCallOffset=85;

    // Full supported Part1 body. Six absolute data operands are checked against
    // their exact module-relative addresses, not wildcarded. Native-local fields
    // are derived only after this and independent Part2 validation.
    inline bool ValidBody(const unsigned char* p,std::size_t size,std::uintptr_t imageBase)
    {
        constexpr unsigned char body[] = {
            0x55,0x8b,0xec,0x53,0x8b,0x5d,0x14,0x56,0x8b,0xf1,0xc7,0x03,0x01,0x00,0x00,0x00,0xf6,0x86,0xe0,0x00,
            0x00,0x00,0x01,0x74,0x3d,0x80,0xbe,0x44,0x01,0x00,0x00,0x00,0x75,0x34,0x8b,0x86,0x04,0x02,0x00,0x00,
            0x83,0xf8,0xff,0x74,0x29,0x8b,0x15,0xa8,0x50,0x6c,0x10,0x8b,0xc8,0x81,0xe1,0xff,0x0f,0x00,0x00,0x03,
            0xc9,0x8d,0x4c,0xca,0x04,0x85,0xc9,0x74,0x11,0xc1,0xe8,0x0c,0x39,0x41,0x04,0x75,0x09,0x83,0x39,0x00,
            0x0f,0x85,0xf7,0x00,0x00,0x00,0x80,0x3d,0x6c,0xf8,0x6b,0x10,0x00,0x0f,0x84,0xea,0x00,0x00,0x00,0x80,
            0xbe,0xfa,0x01,0x00,0x00,0x00,0x57,0x8b,0x7d,0x08,0x75,0x10,0x8b,0x06,0x8b,0x90,0xe0,0x01,0x00,0x00,
            0x8b,0xce,0xff,0xd2,0x84,0xc0,0x74,0x5d,0x8b,0x86,0x00,0x02,0x00,0x00,0x50,0xe8,0xa4,0xa5,0x01,0x00,
            0x8b,0xc8,0x83,0xc4,0x04,0x85,0xc9,0x74,0x48,0xa1,0xa8,0x49,0x6c,0x10,0xf3,0x0f,0x10,0x07,0x0f,0x2e,
            0x40,0x0c,0x9f,0xf6,0xc4,0x44,0x7a,0x35,0xe8,0xc3,0xa7,0x01,0x00,0xd9,0x55,0x14,0xf3,0x0f,0x10,0x45,
            0x14,0xd9,0x1f,0xa1,0xa8,0x49,0x6c,0x10,0xf3,0x0f,0x5c,0x40,0x1c,0xf3,0x0f,0x11,0x07,0x8b,0x0d,0xa8,
            0x49,0x6c,0x10,0xf3,0x0f,0x10,0x49,0x20,0xf3,0x0f,0x59,0x48,0x1c,0xf3,0x0f,0x58,0xc8,0xf3,0x0f,0x11,
            0x0f,0xd9,0x86,0xa0,0x00,0x00,0x00,0x8b,0x45,0x0c,0xd9,0x18,0x51,0xd9,0x86,0xa4,0x00,0x00,0x00,0x8d,
            0x56,0x18,0xd9,0x58,0x04,0x8b,0xce,0xd9,0x86,0xa8,0x00,0x00,0x00,0xd9,0x58,0x08,0x8b,0x45,0x10,0xd9,
            0x86,0xc4,0x00,0x00,0x00,0xd9,0x18,0xd9,0x86,0xc8,0x00,0x00,0x00,0xd9,0x58,0x04,0xd9,0x86,0xcc,0x00,
            0x00,0x00,0xd9,0x58,0x08,0xd9,0x07,0xd9,0x1c,0x24,0x52,0xe8,0xf8,0xcc,0xff,0xff,0x89,0x03,0xa1,0x0c,
            0x4a,0x72,0x10,0x83,0x78,0x30,0x00,0x5f,0x75,0x06,0xf6,0x46,0x5c,0x04,0x74,0x06,0xc7,0x03,0x00,0x00,
            0x00,0x00,0x5e,0xb8,0x01,0x00,0x00,0x00,0x5b,0x5d,0xc2,0x10,0x00,0x6a,0x00,0x8b,0xce,0xe8,0x4a,0xb8,
            0xff,0xff,0x5e,0x33,0xc0,0x5b,0x5d,0xc2,0x10,0x00,
        };
        constexpr std::size_t absoluteOffsets[]={47,88,150,184,199,299};
        if(!p || size<sizeof(body) || imageBase>UINT32_MAX) return false;
        for(std::size_t i=0;i<sizeof(body);)
        {
            bool absolute=false;
            for(auto offset:absoluteOffsets) if(i==offset) {absolute=true;break;}
            if(absolute)
            {
                std::uint32_t disk=0,actual=0;
                std::memcpy(&disk,body+i,4);std::memcpy(&actual,p+i,4);
                const std::uint64_t relocated=static_cast<std::uint64_t>(imageBase)+disk-0x10000000u;
                if(relocated>UINT32_MAX || actual!=relocated) return false;
                i+=4;
            }
            else if(p[i]!=body[i]) return false;
            else ++i;
        }
        return true;
    }
    // Exact instruction fixtures from the supported binary, no wildcard fields.
    inline bool ExactHex(const unsigned char* p,std::size_t size,const char* hex)
    {
        const auto n=std::strlen(hex)/2;if(!p||size<n) return false;
        auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for(std::size_t i=0;i<n;++i) if(p[i]!=((digit(hex[2*i])<<4)|digit(hex[2*i+1]))) return false;
        return true;
    }
    inline constexpr const char* kPart2="558bec8b5508f30f10020f2e81a00000009f56f6c4447a24f30f1042040f2e81a40000009ff6c4447a12f30f1042080f2e81a80000009ff6c4447b088b751083ce01eb038b75108b550cf30f10020f2e81c40000009ff6c4447a24f30f1042040f2e81c80000009ff6c4447a12f30f1042080f2e81cc0000009ff6c4447b0383ce0285f6740656e8948cfdff5e5dc20c00";
    inline constexpr const char* kLoop="558bec51f30f10450c53568b7508f30f104e180f2fc8ba01000000578955fc762133c03946147e1a33c98d9b000000008b3e8bda66895c0f024083c10c3b46147cee33dbf30f114618895d08395e147e4b8b3e66837c1f020074268b4c1f08d9450c8b018b501851d91c24ffd285c0740933c06689441f02eb07c745fc000000008b45084083c30c8945083b46147cc18b45fc5f5e5b8be55dc208005f5e8bc25b8be55dc20800";
    inline constexpr const char* kOriginBinding="8d86a00000008945f48d45f050518d4e18897df8c745f002000100e878dcffff";
    inline constexpr const char* kAnglesBinding="8d86c40000008945e88d45e4506a008d4e18895decc745e402000100e817dcffff";
    inline constexpr const char* kInsert="558bec568bf18b460c8b4e048d5001573bd17e0b2bc140508bcee8a193ffffff460c8b460c8b7d088b0e2bc748894e1085c07e1b8d044003c08d147f03c08d0c91505183c10c51e8d452380083c40c8b168d0c7f8d048a85c074138b4d0c8b1189108b51048950048b49088948088bc75f5e5dc20800";
    struct NativeLayout {std::uint32_t origin=0,angles=0,map=0;};
    inline bool DiscoverLocal(const unsigned char* p,std::size_t n,std::uintptr_t base,
        const unsigned char* part2,std::size_t n2,NativeLayout& layout)
    {
        layout={};
        if(!ValidBody(p,n,base)||!ExactHex(part2,n2,kPart2)) return false;
        // Operands of the validated fld instructions and map LEA, independently
        // corroborated by all six Part2 ucomiss operands in its full fingerprint.
        std::memcpy(&layout.origin,p+223,4);std::memcpy(&layout.angles,p+261,4);
        layout.map=p[241];
        return layout.origin==0xa0 && layout.angles==0xc4 && layout.map==0x18;
    }
    enum class MapState : unsigned {NotCalled,Invalid,OutsideRange,Skipped,Eligible,RewindEligible,MissingBinding};
    inline MapState MapBranch(bool called,bool valid,int count,int index,int needs,float previous,float selected)
    {
        if(!called) return MapState::NotCalled;
        if(!valid||count<0||!std::isfinite(previous)||!std::isfinite(selected)) return MapState::Invalid;
        if(index==-1) return MapState::MissingBinding;
        if(index<0||needs<0||needs>1) return MapState::Invalid;
        if(index>=count) return MapState::OutsideRange;
        if(previous>selected) return MapState::RewindEligible;
        return needs ? MapState::Eligible : MapState::Skipped;
    }
    struct MapObservation
    {
        bool installed=false,called=false,valid=false;
        int total=-1,count=-1,originIndex=-1,anglesIndex=-1,originNeeds=-1,anglesNeeds=-1;
        int originAfter=-1,anglesAfter=-1;
        float beforeTime=NAN,afterTime=NAN,selected=NAN;
        unsigned OriginState() const {return unsigned(MapBranch(called,valid,count,originIndex,originNeeds,beforeTime,selected));}
        unsigned AnglesState() const {return unsigned(MapBranch(called,valid,count,anglesIndex,anglesNeeds,beforeTime,selected));}
    };
    struct EntryBindings
    {
        std::uint32_t originWatcher=0,anglesWatcher=0;
        int originIndex=-1,anglesIndex=-1,originNeeds=-1,anglesNeeds=-1;
        bool Add(int i,std::uint32_t data,std::uint32_t watcher,int needs,std::uintptr_t origin,std::uintptr_t angles)
        {
            if(data!=origin&&data!=angles) return true;
            if(!watcher||needs<0||needs>1) return false;
            auto& index=data==origin?originIndex:anglesIndex;
            auto& flag=data==origin?originNeeds:anglesNeeds;
            auto& target=data==origin?originWatcher:anglesWatcher;
            if(index>=0) return false;index=i;flag=needs;target=watcher;return true;
        }
    };
    struct Observation
    {
        float inputTime=NAN,selectedTime=NAN;
        Vec3 beforeOrigin={NAN,NAN,NAN},afterOrigin={NAN,NAN,NAN},beforeAngles={NAN,NAN,NAN},afterAngles={NAN,NAN,NAN},oldOrigin={NAN,NAN,NAN},oldAngles={NAN,NAN,NAN};
        Vec3 recvBeforeOrigin={NAN,NAN,NAN},recvAfterOrigin={NAN,NAN,NAN},recvBeforeAngles={NAN,NAN,NAN},recvAfterAngles={NAN,NAN,NAN};
        bool nativeValid=false;
        MapObservation map;
        PortalNativeOriginPolicy::Calls origin;
        bool originInstalled=false;
        int result=-1,noMore=-1;
        bool callerMatched=false;
    };
    struct MapSnapshot : EntryBindings
    {
        std::uint32_t entries=0;int total=-1,count=-1;float time=NAN;
    };
    inline bool ValidMapReturn(const MapSnapshot& before,const MapSnapshot& after,float selected)
    {
        return std::isfinite(selected)&&std::isfinite(before.time)&&after.time==selected
            &&before.entries==after.entries&&before.total==after.total&&before.count==after.count
            &&before.originIndex==after.originIndex&&before.anglesIndex==after.anglesIndex
            &&before.originWatcher==after.originWatcher&&before.anglesWatcher==after.anglesWatcher;
    }
    struct Calls
    {
        unsigned count=0,matched=0,stopped=0,continued=0,unknownResult=0;
        unsigned noMoreTrue=0,noMoreFalse=0,invalid=0,oldOutputsValid=0,changed=0,timeAdjusted=0;
        unsigned recvChanged=0,anglesChanged=0,corroborated=0,corroborationMismatch=0;
        MapObservation lastMap;
        unsigned mapCalls=0,mapInvalid=0,mapUnavailable=0,originBranches[7]={},anglesBranches[7]={};
        PortalNativeOriginPolicy::Calls origin;
        bool originInstalled=false;
        unsigned originUnavailable=0,originEligibleNoCall=0;
        Vec3 lastRecvBefore={NAN,NAN,NAN},lastRecvAfter={NAN,NAN,NAN};
        Vec3 lastRecvBeforeAngles={NAN,NAN,NAN},lastRecvAfterAngles={NAN,NAN,NAN};
        bool recvValuesValid=false;
        float firstInput=NAN,lastInput=NAN,lastSelected=NAN,minOffset=NAN,maxOffset=NAN,maxDelta=NAN;
        Vec3 firstBefore={NAN,NAN,NAN},lastAfter={NAN,NAN,NAN},lastBeforeAngles={NAN,NAN,NAN},lastAfterAngles={NAN,NAN,NAN},lastOldOrigin={NAN,NAN,NAN},lastOldAngles={NAN,NAN,NAN};
        int lastResult=-1,lastNoMore=-1;
        void Add(const Observation& s)
        {
            ++count;if(s.callerMatched) ++matched;
            if(s.result!=0&&s.result!=1) ++unknownResult;
            // Raw map calls remain observable even when independent Part1 value
            // evidence fails; unknown callers never enter semantic counters.
            if(s.callerMatched)
            {
                lastMap=s.map;mapCalls+=s.map.called;mapInvalid+=s.map.called&&!s.map.valid;
                originInstalled=s.originInstalled;origin.Merge(s.origin);
                originUnavailable+=!s.originInstalled;
                const auto branch=s.map.OriginState();
                originEligibleNoCall+=(branch==4||branch==5)&&!s.origin.calls;
                if(!s.map.installed) ++mapUnavailable;
                else {++originBranches[s.map.OriginState()];++anglesBranches[s.map.AnglesState()];}
            }
            // STOP output references are uninitialized in the actual engine.
            const bool oldValid=s.result==1 && Finite(s.oldOrigin) && Finite(s.oldAngles);
            const float offset=s.selectedTime-s.inputTime,delta=Distance(s.beforeOrigin,s.afterOrigin);
            if(!s.callerMatched||!s.nativeValid||s.result<0||s.result>1||s.noMore<0||s.noMore>1||!std::isfinite(s.inputTime)||!std::isfinite(s.selectedTime)
                ||!Finite(s.beforeOrigin)||!Finite(s.afterOrigin)
                ||!Finite(s.beforeAngles)||!Finite(s.afterAngles)
                ||!std::isfinite(offset)||!std::isfinite(delta)||!std::isfinite(Distance(s.beforeAngles,s.afterAngles))
                ||(s.result==1&&!oldValid)) {++invalid;return;}
            if(s.result==0) ++stopped;else ++continued;
            if(s.noMore==1) ++noMoreTrue;else if(s.noMore==0) ++noMoreFalse;
            lastRecvBefore=s.recvBeforeOrigin;lastRecvAfter=s.recvAfterOrigin;
            lastRecvBeforeAngles=s.recvBeforeAngles;lastRecvAfterAngles=s.recvAfterAngles;
            recvValuesValid=Finite(s.recvBeforeOrigin)&&Finite(s.recvAfterOrigin)&&Finite(s.recvBeforeAngles)&&Finite(s.recvAfterAngles);
            if(Finite(s.recvBeforeOrigin)&&Finite(s.recvAfterOrigin)&&Distance(s.recvBeforeOrigin,s.recvAfterOrigin)>0) ++recvChanged;
            if(Distance(s.beforeAngles,s.afterAngles)>0) ++anglesChanged;
            if(oldValid)
            {
                if(Distance(s.beforeOrigin,s.oldOrigin)==0&&Distance(s.beforeAngles,s.oldAngles)==0) ++corroborated;
                else ++corroborationMismatch;
            }
            if(count-invalid==1) {firstInput=s.inputTime;firstBefore=s.beforeOrigin;minOffset=maxOffset=offset;maxDelta=delta;}
            lastInput=s.inputTime;lastSelected=s.selectedTime;lastResult=s.result;lastNoMore=s.noMore;
            lastAfter=s.afterOrigin;lastBeforeAngles=s.beforeAngles;lastAfterAngles=s.afterAngles;
            minOffset=std::min(minOffset,offset);maxOffset=std::max(maxOffset,offset);
            if(offset!=0) ++timeAdjusted;
            if(delta>0) ++changed;maxDelta=std::max(maxDelta,delta);
            if(oldValid) {++oldOutputsValid;lastOldOrigin=s.oldOrigin;lastOldAngles=s.oldAngles;}
        }
    };
    inline unsigned Classify(const Calls& c,bool installed)
    {
        return unsigned(installed)|(unsigned(c.count>0)<<1)|(unsigned(c.stopped>0)<<2)
            |(unsigned(c.continued>0)<<3)|(unsigned(c.unknownResult>0)<<4)
            |(unsigned(c.noMoreTrue>0)<<5)|(unsigned(c.noMoreFalse>0)<<6)
            |(unsigned(c.invalid>0)<<7)|(unsigned(c.timeAdjusted>0)<<8)
            |(unsigned(c.changed>0)<<9)|(unsigned(c.count!=c.matched)<<10);
    }
    inline unsigned MapClass(const Calls& c)
    {return c.lastMap.OriginState()|(c.lastMap.AnglesState()<<3)|(unsigned(c.lastMap.installed)<<6)
        |(unsigned(c.corroborationMismatch>0)<<7)|(unsigned(c.anglesChanged>0)<<8)|(unsigned(c.recvChanged>0)<<9)
        |(PortalNativeOriginPolicy::Classify(c.origin)<<10);}
    // 0 not called, 1 invalid/unobserved, 2 called-no-change, 3 called-and-changed.
    inline unsigned Outcome(const Calls& c,bool observed)
    {
        if(!observed||c.invalid) return 1;
        if(!c.count) return 0;
        return c.changed ? 3u : 2u;
    }
    inline unsigned AnglesOutcome(const Calls& c,bool observed)
    {return !observed||c.invalid ? 1u : !c.count ? 0u : c.anglesChanged ? 3u : 2u;}
    struct DetailEdges
    {
        bool valid=false;unsigned previous=0,previousMap=0;
        bool Take(const Calls& c,bool prelude)
        {
            const auto current=Classify(c,true),map=MapClass(c);
            if(prelude) return true;
            const bool emit=!valid||current!=previous||map!=previousMap;
            valid=true;previous=current;previousMap=map;return emit;
        }
    };
    struct Window
    {
        unsigned frames=0,observedFrames=0,notObservedFrames=0,noCallFrames=0,outerWithoutPart1=0;
        unsigned calls=0,matched=0,stopped=0,continued=0,unknownResult=0,invalid=0,changed=0;
        unsigned recvChanged=0,anglesChanged=0,corroborated=0,corroborationMismatch=0;
        unsigned mapCalls=0,mapInvalid=0,mapUnavailable=0,originBranches[7]={},anglesBranches[7]={};
        PortalNativeOriginPolicy::Calls origin;
        unsigned originUnavailable=0,originEligibleNoCall=0;
        unsigned noMoreTrue=0,noMoreFalse=0,timeAdjusted=0,selectedStalled=0,selectedBackwards=0;
        float minOffset=0,maxOffset=0;
        bool previousValid=false,hasOffset=false;
        float previousInput=0,previousSelected=0;
        static bool Comparable(const Calls& c,bool observed)
        {return observed && c.count>0 && c.invalid==0 && c.matched==c.count && c.unknownResult==0;}
        // 0=unknown, 1=advancing, 2=stalled, 3=backwards. Preview without
        // committing state, so rejected/duplicate frames cannot change history.
        unsigned CompareTime(const Calls& c,bool observed) const
        {
            if(!previousValid || !Comparable(c,observed) || c.lastInput<=previousInput) return 0;
            if(c.lastSelected==previousSelected) return 2;
            return c.lastSelected<previousSelected ? 3u : 1u;
        }
        void Add(const Calls& c,bool observed,unsigned outerCalls)
        {
            ++frames;
            if(observed) {++observedFrames;if(!c.count) {++noCallFrames;if(outerCalls) ++outerWithoutPart1;}}
            else ++notObservedFrames;
            calls+=c.count;matched+=c.matched;stopped+=c.stopped;continued+=c.continued;
            unknownResult+=c.unknownResult;invalid+=c.invalid;changed+=c.changed;
            recvChanged+=c.recvChanged;anglesChanged+=c.anglesChanged;corroborated+=c.corroborated;corroborationMismatch+=c.corroborationMismatch;
            mapCalls+=c.mapCalls;mapInvalid+=c.mapInvalid;mapUnavailable+=c.mapUnavailable;
            origin.Merge(c.origin);originUnavailable+=c.originUnavailable;originEligibleNoCall+=c.originEligibleNoCall;
            for(unsigned i=0;i<7;++i) {originBranches[i]+=c.originBranches[i];anglesBranches[i]+=c.anglesBranches[i];}
            noMoreTrue+=c.noMoreTrue;noMoreFalse+=c.noMoreFalse;timeAdjusted+=c.timeAdjusted;
            const bool valid=Comparable(c,observed);
            if(valid)
            {
                if(!hasOffset) {minOffset=c.minOffset;maxOffset=c.maxOffset;hasOffset=true;}
                else {minOffset=std::min(minOffset,c.minOffset);maxOffset=std::max(maxOffset,c.maxOffset);}
                const auto relation=CompareTime(c,observed);
                if(relation==2) ++selectedStalled;
                if(relation==3) ++selectedBackwards;
                previousInput=c.lastInput;previousSelected=c.lastSelected;
            }
            previousValid=valid;
        }
    };
}
