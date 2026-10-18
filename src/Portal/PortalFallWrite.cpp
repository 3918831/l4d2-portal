#include "PortalFallWrite.h"
#include "PortalFallWritePolicy.h"
#include "PortalFallWriteFingerprint.h"
#include "PortalNativeFallProbe.h"
#include "PortalPlayerTeleport.h"
#include "../SDK/SDK.h"
#include "../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../SDK/L4D2/Includes/edict.h"
#include "../SDK/L4D2/Interfaces/IPlayerInfoManager.h"
#include "../SDK/L4D2/Interfaces/IVEngineServer.h"
#include "../Util/Hook/MinHook/MinHook.h"
#include "../Util/Logger/Logger.h"
#include <Windows.h>
#include <intrin.h>
#include <cstddef>

namespace PortalFallWrite
{
    namespace
    {
        namespace FP=PortalFallWriteFingerprint;
        namespace P=PortalFallWritePolicy;
        using GroundFn=void* (__thiscall*)(void*);
        using NotifyFn=void (__thiscall*)(void*,void*);
        GroundFn g_original=nullptr;
        std::uintptr_t g_base=0,g_end=0,g_site=0,g_ground=0;
        unsigned char g_groundBody[49]={};
        bool g_attempted=false,g_installed=false;
        P::Runtime g_state;
        PortalNativeFallProbe::ServerLayout g_layout;
        std::uintptr_t g_local=0,g_field=0,g_table=0,g_notify=0,g_deferred=0,g_pending=0,g_edictSlot=0;
        const char* g_bindingReason="not-bound";
        unsigned g_calls=0,g_candidates=0,g_changed=0,g_notified=0,g_written=0,g_readback=0,g_rejected=0,g_printed=0;
        thread_local void* g_movement=nullptr;
        thread_local int g_command=0;
        const char* Yes(bool b){return b?"true":"false";}

        bool Memory(std::uintptr_t p,std::size_t n,bool write=false)
        {
            if(!p||!n||p>UINTPTR_MAX-n)return false;
            MEMORY_BASIC_INFORMATION m{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
            const auto start=reinterpret_cast<std::uintptr_t>(m.BaseAddress);const auto prot=m.Protect&0xff;
            const bool writable=prot==PAGE_READWRITE||prot==PAGE_WRITECOPY||prot==PAGE_EXECUTE_READWRITE||prot==PAGE_EXECUTE_WRITECOPY;
            const bool readable=writable||prot==PAGE_READONLY||prot==PAGE_EXECUTE_READ;
            return (write?writable:readable)&&start<=UINTPTR_MAX-m.RegionSize&&PortalNativeFallPolicy::Region{start,start+m.RegionSize}.Covers(p,n);
        }
        bool Read(std::uintptr_t p,void* out,std::size_t n)
        {if(!out||!Memory(p,n))return false;std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}
        template<class T> bool Read(std::uintptr_t p,T& out){return Read(p,&out,sizeof(out));}
        bool ImageData(std::uintptr_t p,std::size_t n){return p>=g_base&&p<g_end&&n<=g_end-p&&Memory(p,n);}
        bool Executable(std::uintptr_t p,std::size_t n,bool server=true)
        {
            MEMORY_BASIC_INFORMATION m{};
            if(!Memory(p,n)||!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m)))return false;
            const auto prot=m.Protect&0xff;
            return (prot==PAGE_EXECUTE_READ||prot==PAGE_EXECUTE_READWRITE||prot==PAGE_EXECUTE_WRITECOPY)
                &&(!server||(ImageData(p,n)&&reinterpret_cast<std::uintptr_t>(m.AllocationBase)==g_base));
        }
        std::uintptr_t Relative(std::uintptr_t instruction)
        {std::int32_t d=0;if(!Read(instruction+1,d))return 0;const auto target=std::int64_t(instruction)+5+d;return target>0&&target<=UINT32_MAX?static_cast<std::uintptr_t>(target):0;}
        std::uintptr_t Displaced(std::uintptr_t object,std::uintptr_t operand)
        {std::int32_t d=0;if(!Read(operand,d))return 0;const auto target=std::int64_t(object)+d;return target>0&&target<=UINT32_MAX?static_cast<std::uintptr_t>(target):0;}
        bool Image()
        {
            g_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA("server.dll"));
            IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
            if(!Read(g_base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||unsigned(dos.e_lfanew)>UINT32_MAX-g_base
                ||!Read(g_base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386
                ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||!nt.OptionalHeader.SizeOfImage||nt.OptionalHeader.SizeOfImage>UINT32_MAX-g_base)return false;
            g_end=g_base+nt.OptionalHeader.SizeOfImage;return true;
        }
        void* LocalServer()
        {
            if(!I::EngineClient||!I::ClientEntityList||!I::EngineClient->IsInGame())return nullptr;
            const int index=I::EngineClient->GetLocalPlayer();if(index<=0)return nullptr;
            auto* entity=I::ClientEntityList->GetClientEntity(index);
            auto* player=entity?entity->As<C_TerrorPlayer*>():nullptr;
            return player?PortalPlayerTeleport::ResolveServerLocalPlayer(player):nullptr;
        }
        bool SameLayout(void* player)
        {
            const auto now=PortalNativeFallProbe::InspectServer(player);
            return now.valid&&now.identity==g_layout.identity&&now.fall==g_layout.fall&&now.local==g_layout.local
                &&now.ground==g_layout.ground&&now.moveType==g_layout.moveType&&now.lifeState==g_layout.lifeState;
        }
        bool NotifyState(unsigned char& deferred,std::uintptr_t& edict,unsigned& flags)
        {
            std::uint32_t table=0,target=0,edict32=0;
            if(!Read(g_local,table)||table!=g_table||!Read(table,target)||target!=g_notify
                ||!Read(g_deferred,deferred)||!Memory(g_field,4,true))return false;
            if(deferred){unsigned char pending=0;if(!Memory(g_pending,1,true)||!Read(g_pending,pending))return false;flags=pending;edict=0;return true;}
            if(!Read(g_edictSlot,edict32)||!edict32||!Memory(edict32,4,true))return false;
            auto* globals=I::PlayerInfoManager?I::PlayerInfoManager->GetGlobalVars():nullptr;
            const int index=I::EngineClient?I::EngineClient->GetLocalPlayer():0;
            if(!globals||!globals->pEdicts||index<=0||index>globals->maxClients
                ||reinterpret_cast<std::uintptr_t>(&globals->pEdicts[index])!=edict32)return false;
            edict=edict32;return Read(edict,flags);
        }
        bool Bind(void* player)
        {
            g_state.ready=false;g_bindingReason="hook-unavailable";
            if(!g_installed)return false;
            g_layout=PortalNativeFallProbe::InspectServer(player);g_bindingReason="datamap-layout";
            if(!g_layout.valid||g_layout.identity.module!=g_base
                ||!FP::Fields(reinterpret_cast<unsigned char*>(g_site),g_groundBody,g_layout.fall,g_layout.local,g_layout.moveType,g_layout.ground))return false;
            const auto p=reinterpret_cast<std::uintptr_t>(player);
            if(p>UINT32_MAX-g_layout.fall)return false;
            g_local=p+g_layout.local;g_field=p+g_layout.fall;
            std::uint32_t table=0,target=0;g_bindingReason="notify-vtable-or-body";
            if(!Read(g_local,table)||!ImageData(table,4)||!Read(table,target)||!Executable(target,49)
                ||!FP::Notify(reinterpret_cast<unsigned char*>(target),49))return false;
            g_table=table;g_notify=target;
            const auto helper=Relative(target+35);g_bindingReason="notify-accessor";
            if(!Executable(helper,18)||!FP::Accessor(reinterpret_cast<unsigned char*>(helper),18,g_base))return false;
            std::uint32_t engineGlobal=0,engine=0,engineTable=0,method=0;
            if(!Read(helper+3,engineGlobal)||!ImageData(engineGlobal,4)||!Read(engineGlobal,engine)
                ||engine!=reinterpret_cast<std::uintptr_t>(I::EngineServer)||!Read(engine,engineTable)
                ||!Read(engineTable+0x184,method)||!Executable(method,1,false))return false;
            g_deferred=Displaced(g_local,target+2);g_pending=Displaced(g_local,target+11);g_edictSlot=Displaced(g_local,target+21);
            // The embedded callback must resolve back inside this exact entity.
            if(g_deferred<p||g_pending<p||g_edictSlot<p||g_deferred>=g_local||g_pending>=g_local||g_edictSlot>=g_local)return false;
            unsigned char deferred=0;std::uintptr_t edict=0;unsigned flags=0;g_bindingReason="write-permission-or-notify-state";
            if(!NotifyState(deferred,edict,flags))return false;
            g_bindingReason="ready";g_state.reason="ready";g_state.ready=true;return true;
        }
        void* __fastcall GroundReturn(void* player,void*)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            void* ground=g_original(player); // exactly once; never change return value
            if(caller!=g_site+18||!g_movement||!g_state.lifetime.active)return ground;
            ++g_calls;
            const auto entity=reinterpret_cast<std::uintptr_t>(player);
            const auto context=PortalNativeFallPolicy::ReadMovementContext(reinterpret_cast<std::uint32_t>(g_movement),
                static_cast<std::uint32_t>(g_state.lifetime.player),[](std::uint32_t p,void* out,std::size_t n){return Read(p,out,n);});
            unsigned char life=255,mt=255;float old=NAN,z=NAN;
            static_assert(offsetof(CMoveData,m_vecVelocity)==0x40,"MoveData ABI");
            const bool valid=g_state.ready&&context.valid&&context.player==entity&&SameLayout(player)
                &&Read(entity+g_layout.lifeState,life)&&Read(entity+g_layout.moveType,mt)
                &&Read(g_field,old)&&context.move<=UINT32_MAX-0x48&&Read(context.move+0x48,z);
            if(!g_state.lifetime.Continue(entity,g_command,valid,life==0,mt,ground!=nullptr))
            {if(!g_state.lifetime.active)Reset(ground?"native-ground":"invalid-context");return ground;}
            unsigned char deferred=0;std::uintptr_t edict=0;unsigned before=0,after=0;
            const bool writable=NotifyState(deferred,edict,before);
            const auto result=g_state.Apply(writable,old,z,[&]
            {
                reinterpret_cast<NotifyFn>(g_notify)(reinterpret_cast<void*>(g_local),reinterpret_cast<void*>(g_field));
                if(deferred){unsigned char pending=0;if(!Read(g_pending,pending))return false;after=pending;return (after&1)!=0;}
                return Read(edict,after)&&(after&0x101)==0x101;
            },[&](float value){if(!Memory(g_field,4,true))return false;std::memcpy(reinterpret_cast<void*>(g_field),&value,4);return true;},
            [&](float value){float actual=NAN;return Read(g_field,actual)&&std::memcmp(&actual,&value,4)==0;});
            g_candidates+=result.candidate;g_changed+=result.changed;g_notified+=result.notified;g_written+=result.written;g_readback+=result.readback;
            const bool failed=!result.candidate||(g_state.enabled&&result.changed&&!result.readback);g_rejected+=failed;
            if(U::PortalFileLog::ShouldEmitFallWriteDetail(g_printed,failed))
            {
                ++g_printed;
                U::LogInfo("[PortalFallWrite] epoch=%u committedCmd=%d cmd=%d enabled=%s exactCaller=true localMatched=true groundNull=true fallBefore=%.9g moveZ=%.9g candidateValue=%.9g writeValidated=%s notifyValidated=%s candidate=%s changed=%s notifyCalled=%s dirtyObserved=%s dirtyBefore=0x%X dirtyAfter=0x%X notifyPath=%s applied=%s readback=%s failed=%s actualVelocityMutation=false cameraMutation=false.\n",
                    g_state.lifetime.epoch,g_state.lifetime.committedCmd,g_command,Yes(g_state.enabled),double(old),double(z),double(result.value),Yes(writable),Yes(g_state.ready),Yes(result.candidate),Yes(result.changed),Yes(g_state.enabled&&result.changed&&result.candidate),Yes(result.notified),before,after,deferred?"deferred":"edict",Yes(result.written),Yes(result.readback),Yes(failed));
            }
            if(failed)Reset("write-or-notify-failed");
            return ground;
        }
    }
    void Initialize(void* nativePlayerMove)
    {
        if(g_attempted)return;g_attempted=true;
        const auto root=reinterpret_cast<std::uintptr_t>(nativePlayerMove);unsigned matches=0;
        if(Image()&&Executable(root,8))
        {
            IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
            if(Read(g_base,dos)&&Read(g_base+dos.e_lfanew,nt)&&nt.FileHeader.NumberOfSections<=96)
            {
                const auto sections=g_base+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
                for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i)
                {
                    IMAGE_SECTION_HEADER section{};
                    if(!Read(sections+i*sizeof(section),section)||!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
                    const auto start=g_base+section.VirtualAddress;const auto size=section.Misc.VirtualSize;
                    if(!Executable(start,size))continue;
                    const auto sites=FP::FindUpdates(reinterpret_cast<unsigned char*>(start),size,g_base);
                    matches+=sites.count;if(sites.count)g_site=start+sites.offset;
                }
            }
        }
        bool body=false;MH_STATUS create=MH_UNKNOWN,enable=MH_UNKNOWN;
        if(matches==1)
        {
            g_ground=Relative(g_site+13);
            body=Executable(g_ground,sizeof(g_groundBody))&&Read(g_ground,g_groundBody,sizeof(g_groundBody))&&FP::Ground(g_groundBody,sizeof(g_groundBody),g_base);
            if(body)
            {
                create=MH_CreateHook(reinterpret_cast<void*>(g_ground),&GroundReturn,reinterpret_cast<void**>(&g_original));
                if(create==MH_OK)enable=MH_EnableHook(reinterpret_cast<void*>(g_ground));
                g_installed=create==MH_OK&&enable==MH_OK;
                if(create==MH_OK&&!g_installed)MH_RemoveHook(reinterpret_cast<void*>(g_ground));
            }
        }
        U::LogInfo("[PortalFallWriteHook] installed=%s updateMatches=%u getterValidated=%s abi=thiscall-pointer-ret0 exactCallerRva=0x%X getterRva=0x%X create=%d enable=%d defaultEnabled=%s rawOffsetsFallback=false.\n",
            Yes(g_installed),matches,Yes(body),g_site?static_cast<unsigned>(g_site+18-g_base):0,g_ground?static_cast<unsigned>(g_ground-g_base):0,create,enable,Yes(g_state.enabled));
    }
    void Reset(const char* reason)
    {
        if(g_state.lifetime.player)
            U::LogInfo("[PortalFallWriteSummary] epoch=%u committedCmd=%d enabled=%s reason=%s calls=%u candidates=%u changed=%u notified=%u applied=%u readback=%u rejected=%u details=%u nativeGroundReturn=unchanged actualVelocityMutation=false cameraMutation=false.\n",
                g_state.lifetime.epoch,g_state.lifetime.committedCmd,Yes(g_state.enabled),reason,g_calls,g_candidates,g_changed,g_notified,g_written,g_readback,g_rejected,g_printed);
        g_state.Reset(reason);g_calls=g_candidates=g_changed=g_notified=g_written=g_readback=g_rejected=g_printed=0;
    }
    void Arm(void* player,std::uint32_t epoch,int cmd)
    {
        Reset("new-commit");
        g_state.lifetime.Arm(reinterpret_cast<std::uintptr_t>(player),epoch,cmd);
        if(!g_state.lifetime.active)return;
        const bool local=player==LocalServer();g_bindingReason="not-local";const bool bound=local&&Bind(player);
        U::LogInfo("[PortalFallWriteBind] epoch=%u committedCmd=%d enabled=%s localMatched=%s bound=%s reason=%s entity=%p fallOffset=0x%X localOffset=0x%X groundOffset=0x%X moveTypeOffset=0x%X lifeStateOffset=0x%X notifyRva=0x%X writeValidated=%s notifyValidated=%s notifyAbi=thiscall-void-ret4 source=runtime-datamap-crosschecked-native-code lifetime=commit-until-native-ground-or-invalidation.\n",
            epoch,cmd,Yes(g_state.enabled),Yes(local),Yes(bound),local?g_bindingReason:"not-local",player,g_layout.fall,g_layout.local,g_layout.ground,g_layout.moveType,g_layout.lifeState,g_notify?static_cast<unsigned>(g_notify-g_base):0,Yes(bound),Yes(bound));
        if(!bound)Reset("binding-rejected");
    }
    void SetEnabled(bool enabled){if(g_state.enabled!=enabled){Reset("ab-toggle");g_state.enabled=enabled;}Report();}
    void Report()
    {
        U::LogInfo("[PortalBsp][Command] command=portal_server_fallfix enabled=%s hookValidated=%s writeValidated=%s notifyValidated=%s active=%s epoch=%u applied=%u reason=%s lastBindingReason=%s detailEnabled=%s usage='portal_server_fallfix <0|1>' onlyAfterNextCommit=true.\n",
            Yes(g_state.enabled),Yes(g_installed),Yes(g_state.ready),Yes(g_state.ready),Yes(g_state.lifetime.active),g_state.lifetime.epoch,g_written,g_state.reason,g_bindingReason,Yes(U::PortalFileLog::NativeDiagnosticsEnabled()));
    }
    PlayerMoveScope::PlayerMoveScope(void* gm,int cmd):previous(g_movement),previousCmd(g_command)
    {
        g_movement=nullptr;g_command=0;
        if(!g_state.lifetime.active||gm!=I::ServerGameMovement||cmd<=0||cmd<g_state.lifetime.committedCmd)return;
        if(reinterpret_cast<std::uintptr_t>(LocalServer())!=g_state.lifetime.player){Reset("local-identity-changed");return;}
        unsigned char life=255,mt=255;
        if(!Read(g_state.lifetime.player+g_layout.lifeState,life)||!Read(g_state.lifetime.player+g_layout.moveType,mt)||life!=0||mt!=2)
        {Reset("dead-or-movetype");return;}
        g_movement=gm;g_command=cmd;
    }
    PlayerMoveScope::~PlayerMoveScope(){g_movement=previous;g_command=previousCmd;}
}
