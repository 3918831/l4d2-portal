#include "PortalNativeInterpolationHook.h"
#include "PortalNativeInterpolationPolicy.h"
#include "PortalNativePart1Policy.h"
#include "PortalNativeOriginFingerprint.h"
#include "PortalNativeDiagnostics.h"
#include "../SDK/SDK.h"
#include "../SDK/L4D2/Entities/C_BasePlayer.h"
#include "../Util/Hook/MinHook/MinHook.h"
#include "../Util/Logger/Logger.h"
#include <Windows.h>
#include <cstring>
#include <limits>
#include <intrin.h>

namespace PortalNativeInterpolationHook
{
    namespace
    {
        constexpr unsigned kSlot=107;
        using Original = bool (__thiscall*)(C_BasePlayer*,float);
        Original g_original=nullptr;
        // Native L4D2 has FOUR references, unlike the SDK version with oldVelocity.
        using Part1Original = int (__thiscall*)(C_BasePlayer*,float&,Vector&,Vector&,int&);
        Part1Original g_part1Original=nullptr;
        std::uintptr_t g_part1Caller=0;
        bool g_part1Installed=false;
        using MapOriginal = int (__thiscall*)(C_BasePlayer*,void*,float);
        MapOriginal g_mapOriginal=nullptr;
        std::uintptr_t g_mapCaller=0;
        bool g_mapInstalled=false,g_localValidated=false,g_bindingValidated=false;
        namespace OriginPolicy=PortalNativeOriginPolicy;
        namespace OriginFingerprint=PortalNativeOriginFingerprint;
        using OriginOriginal=int (__thiscall*)(void*,float);
        using SelectionOriginal=bool (__thiscall*)(void*,void*,float,float,int*);
        using ResetOriginal=void (__thiscall*)(void*);
        OriginOriginal g_originOriginal=nullptr;
        SelectionOriginal g_selectionOriginal=nullptr;
        ResetOriginal g_resetOriginal=nullptr;
        using ResetLatchedOriginal=void (__thiscall*)(C_BasePlayer*);
        ResetLatchedOriginal g_latchedOriginal=nullptr;
        bool g_latchedInstalled=false;
        std::uintptr_t g_latchedEntry=0,g_latchedBase=0;
        thread_local PortalNativeResetPolicy::ParentScope g_latchedScope;
        bool g_resetInstalled=false;
        std::uintptr_t g_resetTarget=0;
        std::uint32_t g_resetGlobalsSlot=0;
        bool g_originInstalled=false,g_selectionInstalled=false;
        std::uintptr_t g_originTable=0,g_originTarget=0,g_originCaller=0,g_selectionCaller=0;
        std::uint32_t g_originObjectOffset=0;
        struct OriginMapScope
        {PortalNativePart1Policy::Observation* sample=nullptr;std::uintptr_t watcher=0,value=0;float time=NAN;};
        thread_local OriginMapScope g_originMapScope;
        struct SelectionScope {void* watcher=nullptr;OriginPolicy::Observation* sample=nullptr;float amount=NAN;};
        thread_local SelectionScope g_selectionScope;
        PortalNativePart1Policy::NativeLayout g_layout;
        thread_local PortalNativePart1Policy::Observation* g_part1Scope=nullptr;
        struct OuterScope { C_BasePlayer* player=nullptr; int frame=-1; };
        thread_local OuterScope g_outerScope;
        std::uintptr_t g_target=0,g_base=0,g_end=0;
        bool g_attempted=false,g_installed=false;

        bool Readable(std::uintptr_t address,std::size_t size)
        {
            if(!address || !size || address>UINTPTR_MAX-size) return false;
            MEMORY_BASIC_INFORMATION m{};
            if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m)
                || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
            const DWORD p=m.Protect&0xff;
            const bool readable=p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY
                ||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
            const auto begin=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
            return readable && begin<=UINTPTR_MAX-m.RegionSize && address>=begin && address+size<=begin+m.RegionSize;
        }
        template<class T> bool Read(std::uintptr_t address,T& out)
        {
            if(!Readable(address,sizeof(T))) return false;
            std::memcpy(&out,reinterpret_cast<void*>(address),sizeof(T));return true;
        }
        bool InImage(std::uintptr_t p,std::size_t n)
        {return p>=g_base && p<g_end && n<=g_end-p && Readable(p,n);}
        bool Code(std::uintptr_t p,std::size_t n)
        {
            MEMORY_BASIC_INFORMATION m{};
            if(!InImage(p,n)||!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))
                ||reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=g_base) return false;
            const auto protection=m.Protect&0xff;
            return protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        }
        bool Image()
        {
            g_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
            IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
            if(!Read(g_base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0
                ||g_base>UINTPTR_MAX-static_cast<unsigned>(dos.e_lfanew)
                ||!Read(g_base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE
                ||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC
                ||!nt.OptionalHeader.SizeOfImage||g_base>UINTPTR_MAX-nt.OptionalHeader.SizeOfImage) return false;
            g_end=g_base+nt.OptionalHeader.SizeOfImage;return true;
        }
        std::uintptr_t RelativeCall(std::uintptr_t call)
        {
            unsigned char op=0;std::int32_t delta=0;
            if(!Code(call,5)||!Read(call,op)||op!=0xe8||!Read(call+1,delta)) return 0;
            const auto result=static_cast<std::int64_t>(call)+5+delta;
            return result>0&&result<=UINTPTR_MAX ? static_cast<std::uintptr_t>(result) : 0;
        }
        bool Fingerprint(std::uintptr_t address,const char* hex)
        {
            const auto size=std::strlen(hex)/2;
            return Code(address,size)&&PortalNativePart1Policy::ExactHex(reinterpret_cast<unsigned char*>(address),size,hex);
        }
        template<std::size_t N> bool OriginBody(std::uintptr_t p,const char* hex,const unsigned (&relocs)[N])
        {
            const auto n=std::strlen(hex)/2;
            return Code(p,n)&&OriginFingerprint::Match(reinterpret_cast<unsigned char*>(p),n,hex,g_base,relocs);
        }
        bool OriginIdentity(std::uintptr_t watcher,std::uintptr_t entity,std::uintptr_t value)
        {
            std::uint32_t table=0,data=0,target=0;unsigned char maxCount=0;
            return watcher==entity+g_originObjectOffset&&Read(watcher,table)&&table==g_originTable
                &&Read(table+24,target)&&target==g_originTarget&&Read(watcher+4,data)&&data==value
                &&Read(watcher+0x1d,maxCount)&&maxCount==1;
        }
        bool ReadRing(std::uintptr_t watcher,OriginPolicy::Ring& ring)
        {
            return Read(watcher+8,ring.base)&&Read(watcher+12,ring.capacity)
                &&Read(watcher+14,ring.head)&&Read(watcher+16,ring.count)&&OriginPolicy::ValidRing(ring);
        }
        bool ReadHistorySample(const OriginPolicy::Ring& ring,int index,OriginPolicy::Sample& sample)
        {
            sample={};sample.index=index;
            const auto address=OriginPolicy::SampleAddress(ring,index);
            sample.valid=address&&Read(address,sample.time)&&Read(address+4,sample.position)
                &&std::isfinite(sample.time)&&OriginPolicy::Finite(sample.position);
            return sample.valid;
        }
        bool ReadResetSnapshot(std::uintptr_t watcher,std::uintptr_t value,PortalNativeResetPolicy::Snapshot& out)
        {
            const bool ready=ReadRing(watcher,out.ring)&&Read(value,out.value)&&OriginPolicy::Finite(out.value);
            if(!ready) return false;
            bool samples=true;
            for(unsigned i=0;i<std::min<unsigned>(3,out.ring.count);++i)
                samples=ReadHistorySample(out.ring,i,out.samples[i])&&samples;
            OriginPolicy::Ring after;
            out.valid=samples&&ReadRing(watcher,after)&&after.base==out.ring.base&&after.capacity==out.ring.capacity
                &&after.head==out.ring.head&&after.count==out.ring.count;
            return out.valid;
        }
        bool ReadCaller(std::uintptr_t address,std::uintptr_t& moduleBase,std::uint32_t& callerRva,char (&moduleName)[64])
        {
            HMODULE module=nullptr;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};MEMORY_BASIC_INFORMATION memory{};
            if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address),&module)) return false;
            const auto base=reinterpret_cast<std::uintptr_t>(module);
            if(!Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0
                ||static_cast<unsigned>(dos.e_lfanew)>0x100000||base>UINTPTR_MAX-static_cast<unsigned>(dos.e_lfanew)
                ||!Read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE
                ||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC
                ||address<base||address-base>=nt.OptionalHeader.SizeOfImage||address-base>UINT32_MAX
                ||!VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory))
                ||reinterpret_cast<std::uintptr_t>(memory.AllocationBase)!=base||!Readable(address,1)) return false;
            const auto protection=memory.Protect&0xff;
            if(protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY) return false;
            char filename[MAX_PATH]={};const auto n=GetModuleFileNameA(module,filename,sizeof(filename));
            if(!n||n>=sizeof(filename)) return false;
            const char* name=std::strrchr(filename,'\\');name=name?name+1:filename;
            if(std::strlen(name)>=sizeof(moduleName)) return false;
            std::memcpy(moduleName,name,std::strlen(name)+1);moduleBase=base;callerRva=static_cast<std::uint32_t>(address-base);
            return true;
        }
        void __fastcall ResetLatchedDetour(C_BasePlayer* player,void*)
        {
            // The validated slot106 wrapper tail-jumps here without replacing
            // this return address. Never walk/guess a caller's stack frame.
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto previous=g_latchedScope;
            g_latchedScope={}; // nested non-local calls must not inherit a local source
            if(player&&player==PortalNativeDiagnostics::OriginResetPlayer())
            {
                auto& scope=g_latchedScope;
                scope.active=true;scope.player=reinterpret_cast<std::uintptr_t>(player);
                std::uint32_t table=0,entry=0;
                scope.identity=Read(scope.player,table)&&Read(table+106*4,entry)&&entry==g_latchedEntry
                    &&OriginIdentity(scope.player+g_originObjectOffset,scope.player,scope.player+g_layout.origin);
                scope.callerValid=ReadCaller(caller,scope.module,scope.callerRva,scope.moduleName);
            }
            g_latchedOriginal(player); // void thiscall/no stack args; natural call exactly once
            g_latchedScope=previous;
        }
        void __fastcall ResetDetour(void* watcher,void*)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            auto* player=PortalNativeDiagnostics::OriginResetPlayer();
            const auto entity=reinterpret_cast<std::uintptr_t>(player),object=reinterpret_cast<std::uintptr_t>(watcher);
            PortalNativeResetPolicy::Observation sample;
            const bool observe=player&&entity<=UINTPTR_MAX-g_originObjectOffset&&object==entity+g_originObjectOffset
                &&PortalNativeDiagnostics::OriginResetBegin(player,sample);
            std::uint32_t globals=0,slot=0;
            const auto value=entity+g_layout.origin;
            if(observe)
            {
                sample.watcher=object;
                sample.identity=OriginIdentity(object,entity,value)&&Read(g_originTable+20,slot)&&slot==g_resetTarget;
                sample.callerValid=ReadCaller(caller,sample.module,sample.callerRva,sample.moduleName);
                sample.parentInstalled=g_latchedInstalled;sample.parent=g_latchedScope;
                sample.parentReturnMatched=g_latchedInstalled&&caller==g_latchedBase+46;
                sample.clockValid=Read(g_resetGlobalsSlot,globals)&&Readable(globals,16)&&Read(globals+12,sample.nativeTime)
                    &&std::isfinite(sample.nativeTime);
                sample.globalsMatched=globals==reinterpret_cast<std::uintptr_t>(I::GlobalVars);
                if(!sample.identity) sample.failure=1;
                else if(!sample.callerValid) sample.failure=2;
                else if(!sample.clockValid) sample.failure=3;
                else if(!ReadResetSnapshot(object,value,sample.before)) sample.failure=4;
            }
            g_resetOriginal(watcher); // void thiscall, no stack args; always once, unchanged
            if(observe)
            {
                std::uint32_t afterGlobals=0;
                sample.clockValid=sample.clockValid&&Read(g_resetGlobalsSlot,afterGlobals)&&afterGlobals==globals
                    &&Readable(afterGlobals,16)&&Read(afterGlobals+12,sample.nativeAfter)&&std::isfinite(sample.nativeAfter);
                const bool afterIdentity=OriginIdentity(object,entity,value)&&Read(g_originTable+20,slot)&&slot==g_resetTarget;
                const bool afterRead=afterIdentity&&ReadResetSnapshot(object,value,sample.after);
                if(!sample.failure&&!sample.clockValid) sample.failure=3;
                else if(!sample.failure&&!afterIdentity) sample.failure=5;
                else if(!sample.failure&&!afterRead) sample.failure=6;
                sample.identity=sample.identity&&afterIdentity;
                PortalNativeDiagnostics::OriginResetEnd(player,sample);
            }
        }
        bool __fastcall SelectionDetour(void* watcher,void*,void* info,float time,float amount,int* noMore)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto scope=g_selectionScope;
            const bool observe=scope.sample&&scope.watcher==watcher;
            const bool result=g_selectionOriginal(watcher,info,time,amount,noMore);
            if(observe)
            {
                auto& s=scope.sample->selection;
                s.called=true;s.returned=result;s.time=time;s.amount=amount;s.effective=time-amount;
                s.callerMatched=caller==g_selectionCaller;
                if(!s.callerMatched) {s.failure=1;return result;}
                if(time!=scope.sample->time||amount!=scope.amount) {s.failure=2;return result;}
                const auto address=reinterpret_cast<std::uintptr_t>(info);
                unsigned char hermite=0;
                // Read only initialized fields. Padding bytes in the native
                // output are never read; false is not treated as uninitialized.
                s.outputsValid=Read(address,hermite)&&hermite<=1&&Read(address+4,s.oldestIndex)
                    &&Read(address+8,s.olderIndex)&&Read(address+12,s.newerIndex)
                    &&Read(address+16,s.fraction)&&Read(reinterpret_cast<std::uintptr_t>(noMore),s.noMore);
                s.hermite=hermite==1;
                if(!s.outputsValid) {s.failure=3;return result;}
                s.ringValid=ReadRing(reinterpret_cast<std::uintptr_t>(watcher),s.ring);
                if(!s.ringValid) {s.failure=4;return result;}
                s.outputsValid=OriginPolicy::OutputsValid(s);
                if(!s.outputsValid) {s.failure=5;return result;}
                bool samples=true;
                if(s.ring.count) samples=ReadHistorySample(s.ring,0,s.newest);
                if(result)
                {
                    samples=ReadHistorySample(s.ring,s.newerIndex,s.newer)&&samples;
                    samples=ReadHistorySample(s.ring,s.olderIndex,s.older)&&samples;
                    if(s.oldestIndex>=0) samples=ReadHistorySample(s.ring,s.oldestIndex,s.oldest)&&samples;
                }
                OriginPolicy::Ring after;
                const bool stable=ReadRing(reinterpret_cast<std::uintptr_t>(watcher),after)
                    &&after.base==s.ring.base&&after.capacity==s.ring.capacity
                    &&after.head==s.ring.head&&after.count==s.ring.count;
                s.samplesValid=samples&&stable;
                if(!stable) {s.ringValid=false;s.failure=7;}
                else if(!samples) s.failure=6;
            }
            return result;
        }
        int __fastcall OriginDetour(void* watcher,void*,float time)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto scope=g_originMapScope;
            const bool observe=scope.sample&&scope.watcher==reinterpret_cast<std::uintptr_t>(watcher);
            OriginPolicy::Observation sample;
            float amount=NAN;
            if(observe)
            {
                sample.installed=g_originInstalled;sample.called=true;sample.time=time;
                sample.callerMatched=caller==g_originCaller;
                sample.identity=OriginIdentity(scope.watcher,reinterpret_cast<std::uintptr_t>(g_outerScope.player),scope.value);
                sample.valid=sample.identity&&OriginPolicy::MatchesScope(scope.watcher,scope.watcher,scope.value,scope.value,
                    caller,g_originCaller,time,scope.time)&&Read(scope.value,sample.before)&&OriginPolicy::Finite(sample.before)
                    &&Read(scope.watcher+0x24,amount)&&std::isfinite(amount);
                if(!sample.callerMatched) sample.failure=1;
                else if(!sample.identity) sample.failure=2;
                else if(!sample.valid) sample.failure=3;
            }
            const auto previous=g_selectionScope;
            g_selectionScope=observe&&sample.valid ? SelectionScope{watcher,&sample,amount} : SelectionScope{};
            const int result=g_originOriginal(watcher,time); // natural call once; no native writes by observer
            g_selectionScope=previous;
            if(observe)
            {
                sample.noMore=result;
                sample.valid=sample.valid&&result>=0&&result<=1
                    &&OriginIdentity(scope.watcher,reinterpret_cast<std::uintptr_t>(g_outerScope.player),scope.value)
                    &&Read(scope.value,sample.after)&&OriginPolicy::Finite(sample.after);
                if(!sample.valid&&!sample.failure) sample.failure=4;
                scope.sample->origin.Add(sample);
            }
            return result;
        }
        bool ReadLocal(C_BasePlayer* player,PortalNativePart1Policy::Vec3& origin,PortalNativePart1Policy::Vec3& angles)
        {
            origin=angles={NAN,NAN,NAN};const auto entity=reinterpret_cast<std::uintptr_t>(player);
            return g_localValidated&&entity<=UINTPTR_MAX-g_layout.angles-sizeof(angles)
                &&Read(entity+g_layout.origin,origin)&&Read(entity+g_layout.angles,angles)
                &&PortalNativePart1Policy::Finite(origin)&&PortalNativePart1Policy::Finite(angles);
        }
        using MapSnapshot=PortalNativePart1Policy::MapSnapshot;
        bool ReadMap(C_BasePlayer* player,std::uintptr_t map,MapSnapshot& out)
        {
            if(!g_bindingValidated||map!=reinterpret_cast<std::uintptr_t>(player)+g_layout.map||!Readable(map,28)) return false;
            int capacity=-1;
            if(!Read(map,out.entries)||!Read(map+4,capacity)||!Read(map+12,out.total)
                ||!Read(map+20,out.count)||!Read(map+24,out.time)||!std::isfinite(out.time)
                ||out.total<0||out.total>256||out.count<0||out.count>out.total||capacity<out.total||capacity>4096
                ||(out.total&&!Readable(out.entries,static_cast<std::size_t>(out.total)*12))) return false;
            const auto entity=reinterpret_cast<std::uintptr_t>(player);
            for(int i=0;i<out.total;++i)
            {
                const auto entry=out.entries+i*12;std::uint32_t data=0,watcher=0;std::uint16_t needs=0;
                if(!Read(entry+4,data)||!Read(entry+8,watcher)||!Read(entry+2,needs)) return false;
                if(data!=entity+g_layout.origin&&data!=entity+g_layout.angles) continue;
                if(needs>1||!Readable(watcher,4)) return false;
                if(!out.Add(i,data,watcher,needs,entity+g_layout.origin,entity+g_layout.angles)) return false;
            }
            std::uint32_t entries=0;int total=-1,count=-1;float time=NAN;
            return Read(map,entries)&&Read(map+12,total)&&Read(map+20,count)&&Read(map+24,time)
                &&entries==out.entries&&total==out.total&&count==out.count&&time==out.time;
        }
        int __fastcall MapDetour(C_BasePlayer* player,void*,void* map,float time)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            auto* sample=g_part1Scope;
            const bool observe=sample&&sample->callerMatched&&g_outerScope.player==player;
            MapSnapshot before,after;bool valid=false;
            if(observe)
            {
                sample->map.called=true;sample->map.selected=time;
                valid=caller==g_mapCaller&&ReadMap(player,reinterpret_cast<std::uintptr_t>(map),before);
            }
            const auto previousScope=g_originMapScope;
            g_originMapScope=observe&&valid&&g_originInstalled
                ? OriginMapScope{sample,before.originWatcher,reinterpret_cast<std::uintptr_t>(player)+g_layout.origin,time}
                : OriginMapScope{};
            const int result=g_mapOriginal(player,map,time);
            g_originMapScope=previousScope;
            if(observe)
            {
                auto& m=sample->map;
                m.valid=valid&&ReadMap(player,reinterpret_cast<std::uintptr_t>(map),after)
                    &&PortalNativePart1Policy::ValidMapReturn(before,after,time);
                if(m.valid)
                {
                    m.total=before.total;m.count=before.count;m.originIndex=before.originIndex;m.anglesIndex=before.anglesIndex;
                    m.originNeeds=before.originNeeds;m.anglesNeeds=before.anglesNeeds;
                    m.originAfter=after.originNeeds;m.anglesAfter=after.anglesNeeds;
                    m.beforeTime=before.time;m.afterTime=after.time;
                }
            }
            return result;
        }
        bool LocalEntry(C_BasePlayer* player,std::uintptr_t& target)
        {
            std::uintptr_t table=0,col=0;
            struct Locator {unsigned signature,offset,cdOffset,type,hierarchy;} locator{};
            constexpr char name[]=".?AVC_TerrorPlayer@@";
            if(!Read(reinterpret_cast<std::uintptr_t>(player),table)||table<4
                ||!InImage(table-4,(kSlot+2)*4)||!Read(table-4,col)||!InImage(col,sizeof(locator))
                ||!Read(col,locator)||locator.signature!=0||locator.offset!=0||locator.cdOffset!=0
                ||!InImage(locator.type,8+sizeof(name))||!InImage(locator.hierarchy,16)
                ||std::memcmp(reinterpret_cast<void*>(locator.type+8),name,sizeof(name))!=0) return false;
            return Read(table+kSlot*4,target)&&Code(target,24);
        }
        bool __fastcall Detour(C_BasePlayer* player,void*,float time)
        {
            Vector before;
            const int frame=PortalNativeDiagnostics::InterpolationBegin(player,time,before);
            const auto previous=g_outerScope;
            g_outerScope={player,frame};
            const bool result=g_original(player,time); // exactly once, unchanged arguments/result
            g_outerScope=previous;
            if(frame>=0) PortalNativeDiagnostics::InterpolationEnd(player,time,before,result,frame);
            return result;
        }
        int __fastcall Part1Detour(C_BasePlayer* player,void*,float& time,
            Vector& oldOrigin,Vector& oldAngles,int& noMore)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto scope=g_outerScope;
            PortalNativePart1Policy::Observation sample;
            const bool observe=scope.frame>=0 && scope.player==player
                && PortalNativeDiagnostics::Part1Begin(player,scope.frame,sample);
            if(observe)
            {
                sample.inputTime=time;sample.callerMatched=caller==g_part1Caller;
                sample.map.installed=g_mapInstalled;
                sample.originInstalled=g_originInstalled;
                sample.nativeValid=sample.callerMatched&&ReadLocal(player,sample.beforeOrigin,sample.beforeAngles);
            }
            auto* previous=g_part1Scope;g_part1Scope=observe?&sample:nullptr;
            const int result=g_part1Original(player,time,oldOrigin,oldAngles,noMore);
            g_part1Scope=previous;
            if(observe)
            {
                sample.selectedTime=time;sample.result=result;sample.noMore=noMore;
                // STOP leaves these native output references uninitialized.
                if(result==1&&sample.callerMatched)
                {
                    sample.oldOrigin={oldOrigin.x,oldOrigin.y,oldOrigin.z};
                    sample.oldAngles={oldAngles.x,oldAngles.y,oldAngles.z};
                }
                sample.nativeValid=ReadLocal(player,sample.afterOrigin,sample.afterAngles)&&sample.nativeValid;
                PortalNativeDiagnostics::Part1End(player,scope.frame,sample);
            }
            return result;
        }
    }
    Status Inspect(C_BasePlayer* player)
    {
        std::uintptr_t target=0;
        if(!g_attempted)
        {
            g_attempted=true;
            const bool image=Image();
            const bool dispatch=image&&LocalEntry(player,target);
            const bool entry=dispatch&&PortalNativeInterpolationPolicy::ValidEntry(reinterpret_cast<unsigned char*>(target),24);
            std::int32_t displacement=0;
            std::uintptr_t parent=0;
            if(entry&&Read(target+11,displacement))
            {
                const std::int64_t address=static_cast<std::int64_t>(target)+15+displacement;
                if(address>0&&address<=UINTPTR_MAX) parent=static_cast<std::uintptr_t>(address);
            }
            const bool chain=parent&&Code(parent,181)&&PortalNativeInterpolationPolicy::ValidParent(reinterpret_cast<unsigned char*>(parent),181);
            std::uintptr_t part1=0;
            if(chain && Read(parent+PortalNativePart1Policy::kParentCallOffset+1,displacement))
            {
                const std::int64_t address=static_cast<std::int64_t>(parent)
                    +PortalNativePart1Policy::kParentCallOffset+5+displacement;
                if(address>0 && address<=UINTPTR_MAX) part1=static_cast<std::uintptr_t>(address);
            }
            const bool part1Validated=part1 && Code(part1,PortalNativePart1Policy::kBodySize)
                && PortalNativePart1Policy::ValidBody(reinterpret_cast<unsigned char*>(part1),PortalNativePart1Policy::kBodySize,g_base);
            const auto part2=chain?RelativeCall(parent+166):0;
            g_localValidated=part1Validated&&Code(part2,145)
                &&PortalNativePart1Policy::DiscoverLocal(reinterpret_cast<unsigned char*>(part1),350,g_base,
                    reinterpret_cast<unsigned char*>(part2),145,g_layout);
            const auto loop=part1Validated?RelativeCall(part1+291):0;
            const bool loopValidated=Fingerprint(loop,PortalNativePart1Policy::kLoop);
            // Guarded constructor anchors, not unvalidated entity offsets: both
            // independent native insertion call sites must bind these local fields.
            const auto originBinding=g_base+0x4e728,anglesBinding=g_base+0x4e788;
            const auto insert=RelativeCall(originBinding+27);
            g_bindingValidated=g_localValidated&&Fingerprint(originBinding,PortalNativePart1Policy::kOriginBinding)
                &&Fingerprint(anglesBinding,PortalNativePart1Policy::kAnglesBinding)
                &&insert==RelativeCall(anglesBinding+28)&&Fingerprint(insert,PortalNativePart1Policy::kInsert);
            if(loopValidated&&g_bindingValidated)
            {
                g_mapCaller=part1+296;
                if(MH_CreateHook(reinterpret_cast<void*>(loop),&MapDetour,reinterpret_cast<void**>(&g_mapOriginal))==MH_OK)
                {
                    g_mapInstalled=MH_EnableHook(reinterpret_cast<void*>(loop))==MH_OK;
                    if(!g_mapInstalled) MH_RemoveHook(reinterpret_cast<void*>(loop));
                }
            }
            // Resolve actual type and ABI through a complete constructor path,
            // its final vtable, the wrapper rel32 and the inner selection rel32.
            // The independently fingerprinted history writer proves the ring
            // stride and inline vec3 payload used by the selection reader.
            const auto ctor=g_base+0x4e606;
            const bool ctorValid=g_bindingValidated&&OriginBody(ctor,OriginFingerprint::kCtor,OriginFingerprint::kCtorRelocs);
            std::uint32_t table=0,originTarget=0;
            const bool typeValid=ctorValid&&Read(ctor+2,g_originObjectOffset)&&g_originObjectOffset==0x2f8
                &&Read(ctor+79,table)&&InImage(table,28)&&Read(table+24,originTarget)
                &&Fingerprint(originTarget,OriginFingerprint::kWrapper);
            const auto inner=typeValid?RelativeCall(originTarget+19):0;
            const bool innerValid=OriginBody(inner,OriginFingerprint::kInner,OriginFingerprint::kInnerRelocs);
            const auto selection=innerValid?RelativeCall(inner+42):0;
            const bool selectionValid=OriginBody(selection,OriginFingerprint::kSelect,OriginFingerprint::kSelectRelocs);
            const bool ringLayout=selectionValid&&OriginBody(g_base+0x4abf0,OriginFingerprint::kWriter,OriginFingerprint::kWriterRelocs);
            g_originTable=table;g_originTarget=originTarget;
            MapSnapshot initialMap;
            const bool liveIdentity=typeValid&&ReadMap(player,reinterpret_cast<std::uintptr_t>(player)+g_layout.map,initialMap)
                &&OriginIdentity(initialMap.originWatcher,reinterpret_cast<std::uintptr_t>(player),reinterpret_cast<std::uintptr_t>(player)+g_layout.origin);
            MH_STATUS selectionCreated=MH_UNKNOWN,selectionEnabled=MH_UNKNOWN,originCreated=MH_UNKNOWN,originEnabled=MH_UNKNOWN;
            if(g_mapInstalled&&innerValid&&ringLayout&&liveIdentity)
            {
                g_originCaller=loop+109;g_selectionCaller=inner+47;
                selectionCreated=MH_CreateHook(reinterpret_cast<void*>(selection),&SelectionDetour,reinterpret_cast<void**>(&g_selectionOriginal));
                if(selectionCreated==MH_OK)
                {
                    selectionEnabled=MH_EnableHook(reinterpret_cast<void*>(selection));
                    g_selectionInstalled=selectionEnabled==MH_OK;
                    if(!g_selectionInstalled) MH_RemoveHook(reinterpret_cast<void*>(selection));
                }
                if(g_selectionInstalled)
                {
                    originCreated=MH_CreateHook(reinterpret_cast<void*>(originTarget),&OriginDetour,reinterpret_cast<void**>(&g_originOriginal));
                    if(originCreated==MH_OK)
                    {
                        originEnabled=MH_EnableHook(reinterpret_cast<void*>(originTarget));
                        g_originInstalled=originEnabled==MH_OK;
                        if(!g_originInstalled) MH_RemoveHook(reinterpret_cast<void*>(originTarget));
                    }
                    if(!g_originInstalled)
                    {
                        MH_DisableHook(reinterpret_cast<void*>(selection));MH_RemoveHook(reinterpret_cast<void*>(selection));
                        g_selectionInstalled=false;
                    }
                }
            }
            U::LogInfo("[PortalNativeOriginHook] installed=%s selectionInstalled=%s constructorValidated=%s typeValidated=%s innerAbiValidated=%s selectionAbiValidated=%s ringLayoutValidated=%s liveBindingValidated=%s watcherOffset=0x%X originTargetRva=0x%X selectionTargetRva=0x%X watcherCallerRva=0x%X selectionCallerRva=0x%X originCreate=%d originEnable=%d selectionCreate=%d selectionEnable=%d passive=true behaviorMutation=false.\n",
                g_originInstalled?"true":"false",g_selectionInstalled?"true":"false",ctorValid?"true":"false",typeValid?"true":"false",
                innerValid?"true":"false",selectionValid?"true":"false",ringLayout?"true":"false",liveIdentity?"true":"false",g_originObjectOffset,
                typeValid?static_cast<unsigned>(originTarget-g_base):0,selectionValid?static_cast<unsigned>(selection-g_base):0,
                g_originInstalled?static_cast<unsigned>(g_originCaller-g_base):0,g_selectionInstalled?static_cast<unsigned>(g_selectionCaller-g_base):0,
                originCreated,originEnabled,selectionCreated,selectionEnabled);
            std::uint32_t resetTarget=0,globalsSecond=0,globalsThird=0;
            const bool resetBody=typeValid&&Read(table+20,resetTarget)
                &&OriginBody(resetTarget,OriginFingerprint::kReset,OriginFingerprint::kResetRelocs);
            const bool resetChain=resetBody&&ringLayout&&RelativeCall(resetTarget+34)==g_base+0x4abf0
                &&RelativeCall(resetTarget+60)==g_base+0x4abf0&&RelativeCall(resetTarget+85)==g_base+0x4abf0;
            const bool globalsOperand=resetBody&&Read(resetTarget+19,g_resetGlobalsSlot)
                &&Read(resetTarget+48,globalsSecond)&&Read(resetTarget+69,globalsThird)
                &&g_resetGlobalsSlot==globalsSecond&&g_resetGlobalsSlot==globalsThird&&InImage(g_resetGlobalsSlot,4);
            MH_STATUS resetCreated=MH_UNKNOWN,resetEnabled=MH_UNKNOWN;
            if(g_originInstalled&&liveIdentity&&resetChain&&globalsOperand)
            {
                g_resetTarget=resetTarget;
                resetCreated=MH_CreateHook(reinterpret_cast<void*>(resetTarget),&ResetDetour,reinterpret_cast<void**>(&g_resetOriginal));
                if(resetCreated==MH_OK)
                {
                    resetEnabled=MH_EnableHook(reinterpret_cast<void*>(resetTarget));g_resetInstalled=resetEnabled==MH_OK;
                    if(!g_resetInstalled) MH_RemoveHook(reinterpret_cast<void*>(resetTarget));
                }
            }
            U::LogInfo("[PortalNativeOriginResetHook] installed=%s originInstalled=%s signatureValidated=%s abiValidated=%s writerCallsValidated=%s globalsOperandValidated=%s liveBindingValidated=%s slot=5 stackArgs=0 return=void targetRva=0x%X globalsSlotRva=0x%X createStatus=%d enableStatus=%d observationScope=local-origin-all-phases passive=true behaviorMutation=false.\n",
                g_resetInstalled?"true":"false",g_originInstalled?"true":"false",resetBody?"true":"false",resetBody?"true":"false",
                resetChain?"true":"false",globalsOperand?"true":"false",liveIdentity?"true":"false",resetBody?static_cast<unsigned>(resetTarget-g_base):0,
                globalsOperand?static_cast<unsigned>(g_resetGlobalsSlot-g_base):0,resetCreated,resetEnabled);
            std::uint32_t playerTable=0,latchedEntry=0;
            const bool latchedDispatch=dispatch&&Read(reinterpret_cast<std::uintptr_t>(player),playerTable)
                &&InImage(playerTable+106*4,4)&&Read(playerTable+106*4,latchedEntry)&&Code(latchedEntry,80);
            const auto latchedBase=latchedDispatch?OriginFingerprint::LatchedBaseTarget(
                reinterpret_cast<unsigned char*>(latchedEntry),80,latchedEntry):0;
            const bool latchedBody=latchedBase&&Code(latchedBase,56)&&g_bindingValidated
                &&OriginFingerprint::ValidLatchedBase(reinterpret_cast<unsigned char*>(latchedBase),56,g_layout.map);
            MH_STATUS latchedCreated=MH_UNKNOWN,latchedEnabled=MH_UNKNOWN;
            if(g_resetInstalled&&liveIdentity&&latchedBody)
            {
                g_latchedEntry=latchedEntry;g_latchedBase=latchedBase;
                latchedCreated=MH_CreateHook(reinterpret_cast<void*>(latchedBase),&ResetLatchedDetour,reinterpret_cast<void**>(&g_latchedOriginal));
                if(latchedCreated==MH_OK)
                {
                    latchedEnabled=MH_EnableHook(reinterpret_cast<void*>(latchedBase));g_latchedInstalled=latchedEnabled==MH_OK;
                    if(!g_latchedInstalled) MH_RemoveHook(reinterpret_cast<void*>(latchedBase));
                }
            }
            U::LogInfo("[PortalNativeResetLatchedHook] installed=%s resetInstalled=%s rttiDispatchValidated=%s wrapperValidated=%s baseValidated=%s mapLayoutValidated=%s abiValidated=%s liveBindingValidated=%s slot=106 wrapperBytes=80 baseBytes=56 stackArgs=0 return=void tailPreservesCaller=%s wrapperRva=0x%X baseRva=0x%X childReturnRva=0x%X createStatus=%d enableStatus=%d passive=true behaviorMutation=false.\n",
                g_latchedInstalled?"true":"false",g_resetInstalled?"true":"false",latchedDispatch?"true":"false",latchedBase?"true":"false",
                latchedBody?"true":"false",latchedBody?"true":"false",latchedBody?"true":"false",liveIdentity?"true":"false",latchedBase?"true":"false",
                latchedDispatch?static_cast<unsigned>(latchedEntry-g_base):0,latchedBody?static_cast<unsigned>(latchedBase-g_base):0,
                latchedBody?static_cast<unsigned>(latchedBase+46-g_base):0,latchedCreated,latchedEnabled);
            U::LogInfo("[PortalNativeMapHook] installed=%s loopValidated=%s localIdentity=%s bindingValidated=%s originOffset=0x%X anglesOffset=0x%X mapOffset=0x%X watcherHookInstalled=%s invocationEvidence=OriginFrame behaviorMutation=false.\n",
                g_mapInstalled?"true":"false",loopValidated?"true":"false",g_localValidated?"true":"false",g_bindingValidated?"true":"false",g_layout.origin,g_layout.angles,g_layout.map,g_originInstalled?"true":"false");
            MH_STATUS created=MH_UNKNOWN,enabled=MH_UNKNOWN;
            if(entry&&chain)
            {
                g_target=target;
                created=MH_CreateHook(reinterpret_cast<void*>(target),&Detour,reinterpret_cast<void**>(&g_original));
                if(created==MH_OK)
                {
                    enabled=MH_EnableHook(reinterpret_cast<void*>(target));
                    g_installed=enabled==MH_OK;
                    if(!g_installed) MH_RemoveHook(reinterpret_cast<void*>(target));
                }
            }
            U::LogInfo("[PortalNativeInterpolationHook] installed=%s imageValid=%s rttiDispatchValid=%s signatureValidated=%s abiStaticValidated=%s slot=%u targetRva=0x%X parentRva=0x%X createStatus=%d enableStatus=%d behaviorMutation=false.\n",
                g_installed?"true":"false",image?"true":"false",dispatch?"true":"false",entry?"true":"false",chain?"true":"false",
                kSlot,dispatch?static_cast<unsigned>(target-g_base):0,chain?static_cast<unsigned>(parent-g_base):0,created,enabled);
            MH_STATUS partCreated=MH_UNKNOWN,partEnabled=MH_UNKNOWN;
            if(g_installed && part1Validated)
            {
                g_part1Caller=parent+PortalNativePart1Policy::kParentCallOffset+5;
                partCreated=MH_CreateHook(reinterpret_cast<void*>(part1),&Part1Detour,reinterpret_cast<void**>(&g_part1Original));
                if(partCreated==MH_OK)
                {
                    partEnabled=MH_EnableHook(reinterpret_cast<void*>(part1));
                    g_part1Installed=partEnabled==MH_OK;
                    if(!g_part1Installed) MH_RemoveHook(reinterpret_cast<void*>(part1));
                }
            }
            U::LogInfo("[PortalNativePart1Hook] installed=%s outerInstalled=%s signatureValidated=%s abiStaticValidated=%s stackArgs=4 calleePopBytes=16 targetRva=0x%X callerRva=0x%X createStatus=%d enableStatus=%d behaviorMutation=false.\n",
                g_part1Installed?"true":"false",g_installed?"true":"false",part1Validated?"true":"false",part1Validated?"true":"false",
                part1Validated?static_cast<unsigned>(part1-g_base):0,g_part1Installed?static_cast<unsigned>(g_part1Caller-g_base):0,partCreated,partEnabled);
        }
        const bool dispatch=g_installed&&LocalEntry(player,target)&&target==g_target;
        return {g_installed,dispatch,g_part1Installed,g_mapInstalled,g_localValidated,g_originInstalled,g_selectionInstalled,g_resetInstalled,g_latchedInstalled};
    }
}
