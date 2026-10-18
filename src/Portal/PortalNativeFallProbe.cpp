#include "PortalNativeFallProbe.h"
#include "../SDK/SDK.h"
#include "../SDK/L4D2/Entities/C_BasePlayer.h"
#include "../Util/Logger/Logger.h"
#include <Windows.h>
#include <cstddef>

namespace PortalNativeFallProbe
{
    namespace
    {
        namespace P=PortalNativeFallPolicy;
        std::uint32_t g_base=0,g_end=0,g_map=0,g_clientOffset=0,g_clientClass=0;
        P::Cache g_serverCache,g_clientCache;
        P::Binding g_fall;P::Field g_velocity,g_ground,g_flags,g_moveType,g_tickBase,g_lifeState;
        P::Diagnostic g_serverDiagnostic,g_clientDiagnostic;
        unsigned g_epoch=1;
        struct Cost
        {
            unsigned attempts=0,hits=0,reads=0,queries=0,regionHits=0,rejected=0;
            unsigned externalTextBlocks=0;std::uint32_t firstExternalText=0;
            std::uintptr_t deniedAddress=0;std::size_t deniedSize=0;const char* deniedReason="none";
            double totalMs=NAN,imageMs=NAN,walkMs=NAN,logMs=NAN;bool timingValid=false;
        };
        Cost g_serverCost,g_clientCost;
        struct Work
        {
            bool active=false,timingValid=false;LARGE_INTEGER start{},imageEnd{},frequency{};
            std::array<P::Region,16> regions{};unsigned nextRegion=0;
            Cost cost;
        };
        // Readable ranges live only during a synchronous metadata bind, never
        // across frames or entity value reads where protections may change.
        thread_local Work g_work;
        void StartWork()
        {
            g_work={};g_work.active=true;
            g_work.timingValid=QueryPerformanceFrequency(&g_work.frequency)&&g_work.frequency.QuadPart>0&&QueryPerformanceCounter(&g_work.start);
        }
        LARGE_INTEGER Now(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t;}
        double Milliseconds(LARGE_INTEGER begin,LARGE_INTEGER end)
        {return g_work.timingValid&&end.QuadPart>=begin.QuadPart?1000.0*double(end.QuadPart-begin.QuadPart)/double(g_work.frequency.QuadPart):NAN;}
        void EndWork(Cost& result)
        {
            const auto end=Now();g_work.cost.totalMs=Milliseconds(g_work.start,end);
            g_work.cost.imageMs=Milliseconds(g_work.start,g_work.imageEnd);
            g_work.cost.walkMs=Milliseconds(g_work.imageEnd,end);g_work.cost.timingValid=g_work.timingValid;
            g_work.cost.attempts=result.attempts+1;g_work.cost.hits=result.hits;result=g_work.cost;g_work.active=false;
        }
        bool Reject(std::uintptr_t at,std::size_t size,const char* reason)
        {
            if(g_work.active){++g_work.cost.rejected;g_work.cost.deniedAddress=at;g_work.cost.deniedSize=size;g_work.cost.deniedReason=reason;}
            return false;
        }
        bool Query(std::uintptr_t at,MEMORY_BASIC_INFORMATION& m)
        {if(g_work.active)++g_work.cost.queries;return VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m))!=0;}
        bool Readable(std::uintptr_t at,std::size_t size)
        {
            if(!at||!size||at>UINTPTR_MAX-size)return Reject(at,size,"address-range");
            if(g_work.active)for(const auto& region:g_work.regions)if(region.Covers(at,size)){++g_work.cost.regionHits;return true;}
            MEMORY_BASIC_INFORMATION m{};
            if(!Query(at,m))return Reject(at,size,"virtual-query");
            if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return Reject(at,size,"protection");
            const auto begin=reinterpret_cast<std::uintptr_t>(m.BaseAddress);const DWORD p=m.Protect&0xff;
            const bool readable=p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
            if(!readable||begin>UINTPTR_MAX-m.RegionSize)return Reject(at,size,"region-range");
            const P::Region region{begin,begin+m.RegionSize};
            if(!region.Covers(at,size))return Reject(at,size,"region-boundary");
            if(g_work.active)g_work.regions[g_work.nextRegion++%g_work.regions.size()]=region;
            return true;
        }
        bool Read(std::uintptr_t at,void* out,std::size_t size)
        {
            if(g_work.active)++g_work.cost.reads;
            if(!out||!Readable(at,size))return false;
            std::memcpy(out,reinterpret_cast<void*>(at),size);return true;
        }
        bool Metadata(std::uint32_t at,void* out,std::size_t size)
        {
            if(at<g_base||at>=g_end||size>g_end-at)
            {if(g_work.active)++g_work.cost.reads;return Reject(at,size,"server-image-range");}
            return Read(at,out,size);
        }
        bool Image()
        {
            g_base=reinterpret_cast<std::uint32_t>(GetModuleHandleA("server.dll"));g_end=0;
            IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
            if(!Read(g_base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||unsigned(dos.e_lfanew)>UINT32_MAX-g_base
                ||!Read(g_base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386
                ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||!nt.OptionalHeader.SizeOfImage||nt.OptionalHeader.SizeOfImage>UINT32_MAX-g_base)return false;
            g_end=g_base+nt.OptionalHeader.SizeOfImage;return true;
        }
        bool MetadataText(std::uint32_t at,void* out,std::size_t size)
        {
            // DEFINE_FUNCTION_RAW uses GenerateName/new char[] in the SDK.
            // Only the bounded name decoder uses this reader. Structures
            // still go through Metadata(), and entity values through Value().
            if(!Read(at,out,size))return false;
            if(g_work.active&&!P::Region{g_base,g_end}.Covers(at,size))
            {
                ++g_work.cost.externalTextBlocks;
                if(!g_work.cost.firstExternalText)g_work.cost.firstExternalText=at;
            }
            return true;
        }
        P::Identity ServerIdentity(void* player)
        {
            P::Identity key;key.entity=reinterpret_cast<std::uint32_t>(player);
            key.module=reinterpret_cast<std::uint32_t>(GetModuleHandleA("server.dll"));
            if(!Read(key.entity,&key.table,4)||key.module!=g_base||!g_end)return key;
            std::uint32_t target=0;unsigned char code[6]={};
            if(key.table>UINT32_MAX-11*4||!Metadata(key.table+11*4,&target,4)||!Metadata(target,code,6))return key;
            key.root=P::MapFromGetter(code,6,g_base,g_end);
            if(key.root&&!Metadata(key.root,key.header.data(),16))key.header={};
            return key;
        }
        bool Scalar(const P::Field& f,unsigned type,unsigned size)
        {return f.valid&&f.type==type&&f.count==1&&f.size==size&&!(f.flags&0x40)&&f.offset>0&&f.offset<65536;}
        void BindServer(void* player)
        {
            const auto identity=ServerIdentity(player);
            if(!g_serverCache.NeedsBind(identity)){++g_serverCost.hits;return;}
            StartWork();g_fall={};g_velocity={};g_ground={};g_flags={};g_moveType={};g_tickBase={};g_lifeState={};g_map=0;
            g_serverDiagnostic={};g_serverDiagnostic.stage="server-image";
            std::uint32_t table=0,target=0;unsigned char code[6]={};bool getter=false;
            do
            {
                if(!Image()){g_serverDiagnostic.issue="image-header";break;}
                g_serverDiagnostic.stage="entity-vtable";
                if(!Read(reinterpret_cast<std::uintptr_t>(player),&table,4)||table>UINT32_MAX-11*4
                    ||!Metadata(table+11*4,&target,4)){g_serverDiagnostic.issue="vtable-read";break;}
                g_serverDiagnostic.stage="native-getter";
                if(!Metadata(target,code,6)){g_serverDiagnostic.issue="code-read";break;}
                MEMORY_BASIC_INFORMATION m{};
                const bool executable=Query(target,m)&&m.State==MEM_COMMIT&&!(m.Protect&PAGE_GUARD)&&reinterpret_cast<std::uintptr_t>(m.AllocationBase)==g_base
                    &&((m.Protect&0xff)==PAGE_EXECUTE_READ||(m.Protect&0xff)==PAGE_EXECUTE_READWRITE||(m.Protect&0xff)==PAGE_EXECUTE_WRITECOPY);
                if(!executable){g_serverDiagnostic.issue="code-protection";break;}
                g_map=P::MapFromGetter(code,6,g_base,g_end);getter=g_map!=0;
                if(!getter){g_serverDiagnostic.issue="getter-shape-or-map-range";break;}
                g_work.imageEnd=Now();g_fall=P::BindFall(g_map,Metadata,MetadataText,&g_serverDiagnostic);
                // Preserve all identity/type/size/PTR guards. If this fails,
                // report the exact stage; do not substitute a disk offset.
                if(g_fall.valid)
                {
                    g_velocity=P::Find(g_map,"m_vecVelocity",Metadata,MetadataText);if(!Scalar(g_velocity,3,12))g_velocity={};
                    g_ground=P::Find(g_map,"m_hGroundEntity",Metadata,MetadataText);if(!Scalar(g_ground,13,4))g_ground={};
                    g_flags=P::Find(g_map,"m_fFlags",Metadata,MetadataText);if(!Scalar(g_flags,5,4))g_flags={};
                    g_moveType=P::Find(g_map,"m_MoveType",Metadata,MetadataText);if(!Scalar(g_moveType,8,1))g_moveType={};
                    g_tickBase=P::Find(g_map,"m_nTickBase",Metadata,MetadataText);if(!Scalar(g_tickBase,5,4))g_tickBase={};
                    g_lifeState=P::Find(g_map,"m_lifeState",Metadata,MetadataText);if(!Scalar(g_lifeState,8,1))g_lifeState={};
                }
            }while(false);
            if(!g_work.imageEnd.QuadPart)g_work.imageEnd=Now();
            // Store success AND failure, including the live map header. A
            // changed entity/vtable/module/root/header invalidates the key.
            g_serverCache.Remember(ServerIdentity(player));EndWork(g_serverCost);const auto logStart=Now();
            const auto& d=g_serverDiagnostic;const auto& c=g_serverCost;
            U::LogInfo("[PortalNativeFallStateProbe] domain=server entity=%p epoch=%u attempt=%u getterValidated=%s getter=0x%X map=0x%X descriptorStride=60 fallBound=%s fallOffset=0x%X localDescriptor=0x%X fallDescriptor=0x%X velocityBound=%s groundBound=%s flagsBound=%s moveTypeBound=%s tickBaseBound=%s failure(stage=%s issue=%s map=0x%X descriptor=0x%X index=%u owner='%s' field=%s type=%u count=%u flags=0x%X offset=0x%X size=%u embedded=0x%X textIssue=%s textAddress=0x%X inspected=%u) readReject(reason=%s address=0x%X size=%u) cost(timingValid=%s totalMs=%.6f imageMs=%.6f walkMs=%.6f reads=%u queries=%u regionHits=%u rejected=%u logExcluded=true) textRead(policy=readable-bounded-64 externalBlocks=%u firstExternal=0x%X) structureRead=server-image source=runtime-native-save-datamap cache=identity-success-and-failure legacyDataMapChanged=false fieldWrites=false behaviorMutation=false.\n",
                player,g_epoch,c.attempts,getter?"true":"false",target,g_map,g_fall.valid?"true":"false",g_fall.offset,g_fall.localDescriptor,g_fall.descriptor,
                g_velocity.valid?"true":"false",g_ground.valid?"true":"false",g_flags.valid?"true":"false",g_moveType.valid?"true":"false",g_tickBase.valid?"true":"false",
                d.stage,d.issue,d.map,d.descriptor,d.index,d.owner,d.field,d.type,d.count,d.flags,d.offset,d.size,d.embedded,d.text.issue,d.text.address,d.text.inspected,
                c.deniedReason,static_cast<unsigned>(c.deniedAddress),static_cast<unsigned>(c.deniedSize),c.timingValid?"true":"false",c.totalMs,c.imageMs,c.walkMs,c.reads,c.queries,c.regionHits,c.rejected,c.externalTextBlocks,c.firstExternalText);
            g_serverCost.logMs=Milliseconds(logStart,Now());
        }
        bool Value(void* player,std::uint32_t offset,void* out,std::size_t size)
        {
            const auto address=reinterpret_cast<std::uintptr_t>(player);
            return player&&offset>0&&offset<65536&&address<=UINTPTR_MAX-offset&&Read(address+offset,out,size);
        }
        bool RecvCount(std::uint32_t address,unsigned& count)
        {
            auto* table=reinterpret_cast<RecvTable*>(address);
            if(!Readable(address,sizeof(*table)))return false;
            const int n=table->GetNumProps();if(n<0||n>4096)return false;count=static_cast<unsigned>(n);return true;
        }
        bool RecvEntry(std::uint32_t address,unsigned index,P::RecvProperty& result)
        {
            unsigned count=0;if(!RecvCount(address,count)||index>=count)return false;
            const auto start=reinterpret_cast<std::uintptr_t>(reinterpret_cast<RecvTable*>(address)->m_pProps);
            if(!start||start>UINTPTR_MAX-index*sizeof(RecvProp))return false;
            auto* prop=reinterpret_cast<RecvProp*>(start+index*sizeof(RecvProp));
            if(!Readable(reinterpret_cast<std::uintptr_t>(prop),sizeof(*prop))||!P::Text(reinterpret_cast<std::uint32_t>(prop->GetName()),result.name,Read))return false;
            const int offset=prop->GetOffset();if(offset<0||offset>65536)return false;
            result.offset=static_cast<unsigned>(offset);result.child=reinterpret_cast<std::uint32_t>(prop->GetDataTable());result.type=prop->GetType();return true;
        }
        P::Identity ClientIdentity(C_BasePlayer* player)
        {
            P::Identity key;key.entity=reinterpret_cast<std::uint32_t>(player);Read(key.entity,&key.table,4);
            key.module=reinterpret_cast<std::uint32_t>(GetModuleHandleA("client.dll"));
            key.root=reinterpret_cast<std::uint32_t>(I::BaseClient?I::BaseClient->GetAllClasses():nullptr);
            ClientClass c{};
            if(g_clientClass&&Read(g_clientClass,&c,sizeof(c)))
            {
                key.header[0]=g_clientClass;key.header[1]=reinterpret_cast<std::uint32_t>(c.m_pRecvTable);
                Read(key.header[1],key.header.data()+2,8); // props pointer/count, not a value buffer
            }
            return key;
        }
        void BindClient(C_BasePlayer* player)
        {
            const auto identity=ClientIdentity(player);
            if(!g_clientCache.NeedsBind(identity)){++g_clientCost.hits;return;}
            StartWork();g_clientOffset=g_clientClass=0;g_clientDiagnostic={};g_clientDiagnostic.stage="client-class-search";
            auto* c=I::BaseClient?I::BaseClient->GetAllClasses():nullptr;
            g_work.imageEnd=Now();unsigned visits=0;
            for(;c&&visits<4096;++visits,c=c->m_pNext)
            {
                g_clientDiagnostic.map=reinterpret_cast<std::uint32_t>(c);g_clientDiagnostic.index=visits;
                if(!Readable(reinterpret_cast<std::uintptr_t>(c),sizeof(*c))){g_clientDiagnostic.issue="class-read";break;}
                char name[64]={};
                if(!P::Text(reinterpret_cast<std::uint32_t>(c->m_pNetworkName),name,Read)){g_clientDiagnostic.issue="class-text";break;}
                if(std::strcmp(name,"CBasePlayer"))continue;
                g_clientClass=reinterpret_cast<std::uint32_t>(c);std::memcpy(g_clientDiagnostic.owner,name,64);
                g_clientDiagnostic.stage="recv-float-search";g_clientDiagnostic.field="m_flFallVelocity";
                const auto result=P::FindRecvFloat(reinterpret_cast<std::uint32_t>(c->m_pRecvTable),RecvCount,RecvEntry);g_clientOffset=result.offset;
                g_clientDiagnostic.issue=result.valid?"none":result.failed?"recv-validation":"field-not-found";
                if(result.valid)g_clientDiagnostic.stage="bound";break;
            }
            if(!g_clientOffset&&!std::strcmp(g_clientDiagnostic.issue,"none"))g_clientDiagnostic.issue=visits==4096?"class-limit":"class-not-found";
            g_clientCache.Remember(ClientIdentity(player));EndWork(g_clientCost);const auto logStart=Now();
            const auto& d=g_clientDiagnostic;const auto& cost=g_clientCost;
            U::LogInfo("[PortalNativeFallStateProbe] domain=client epoch=%u attempt=%u fallBound=%s fallOffset=0x%X failure(stage=%s issue=%s map=0x%X index=%u owner='%s') readReject(reason=%s address=0x%X size=%u) cost(timingValid=%s totalMs=%.6f imageMs=%.6f walkMs=%.6f reads=%u queries=%u regionHits=%u rejected=%u logExcluded=true) source=runtime-RecvTable-float cache=identity-success-and-failure fieldWrites=false behaviorMutation=false.\n",
                g_epoch,cost.attempts,g_clientOffset?"true":"false",g_clientOffset,d.stage,d.issue,d.map,d.index,d.owner,cost.deniedReason,static_cast<unsigned>(cost.deniedAddress),static_cast<unsigned>(cost.deniedSize),
                cost.timingValid?"true":"false",cost.totalMs,cost.imageMs,cost.walkMs,cost.reads,cost.queries,cost.regionHits,cost.rejected);
            g_clientCost.logMs=Milliseconds(logStart,Now());
        }
    }
    ServerLayout InspectServer(void* player)
    {
        if(!player)return {};BindServer(player);
        return {g_serverCache.identity,g_fall.offset,g_fall.localOffset,g_ground.offset,g_moveType.offset,g_lifeState.offset,
            g_fall.valid&&g_ground.valid&&g_moveType.valid&&g_lifeState.valid};
    }
    void Reset()
    {
        g_serverCache.Reset();g_clientCache.Reset();g_fall={};g_clientOffset=g_clientClass=0;g_map=g_base=g_end=0;
        g_serverCost={};g_clientCost={};g_serverDiagnostic={};g_clientDiagnostic={};if(!++g_epoch)++g_epoch;
    }
    void Report(std::uint32_t capture,std::uint32_t transaction)
    {
        const auto& s=g_serverCost;const auto& c=g_clientCost;
        U::LogInfo("[PortalNativeFallStateProbe] phase=cache-summary capture=%u txn=%u epoch=%u server(attempts=%u hits=%u fallBound=%s stage=%s issue=%s lastBindMs=%.6f probeLogMs=%.6f reads=%u queries=%u) client(attempts=%u hits=%u fallBound=%s stage=%s issue=%s lastBindMs=%.6f probeLogMs=%.6f reads=%u queries=%u) lifetime=entity-vtable-module-map-header not-per-capture=true fieldWrites=false behaviorMutation=false.\n",
            capture,transaction,g_epoch,s.attempts,s.hits,g_fall.valid?"true":"false",g_serverDiagnostic.stage,g_serverDiagnostic.issue,s.totalMs,s.logMs,s.reads,s.queries,
            c.attempts,c.hits,g_clientOffset?"true":"false",g_clientDiagnostic.stage,g_clientDiagnostic.issue,c.totalMs,c.logMs,c.reads,c.queries);
    }
    P::Sample Server(void* player)
    {
        P::Sample s;if(!player)return s;BindServer(player);
        s.fallValid=g_fall.valid&&Value(player,g_fall.offset,&s.fall,4)&&std::isfinite(s.fall);
        float velocity[3]={};s.zValid=g_velocity.valid&&Value(player,g_velocity.offset,velocity,12)&&std::isfinite(velocity[2]);if(s.zValid)s.z=velocity[2];
        s.groundValid=g_ground.valid&&Value(player,g_ground.offset,&s.ground,4);
        s.flagsValid=g_flags.valid&&Value(player,g_flags.offset,&s.flags,4);
        unsigned char mt=0;s.moveTypeValid=g_moveType.valid&&Value(player,g_moveType.offset,&mt,1);if(s.moveTypeValid)s.moveType=mt;
        s.tickBaseValid=g_tickBase.valid&&Value(player,g_tickBase.offset,&s.tickBase,4);return s;
    }
    P::Sample Client(C_BasePlayer* player)
    {
        P::Sample s;if(!player)return s;BindClient(player);
        s.fallValid=g_clientOffset&&Value(player,g_clientOffset,&s.fall,4)&&std::isfinite(s.fall);
        const auto velocity=player->m_vecVelocity();s.z=velocity.z;s.zValid=std::isfinite(s.z);
        s.ground=player->m_hGroundEntity().ToInt();s.groundValid=true;s.flags=player->m_fFlags();s.flagsValid=true;
        s.moveType=player->m_MoveType();s.moveTypeValid=true;s.tickBase=player->m_nTickBase();s.tickBaseValid=true;return s;
    }
    P::Sample Movement(bool server,void* gameMovement,C_BasePlayer* local)
    {
        // No binding discovery for a movement-owned player. Only the entity
        // already observed by this domain's local command boundary is allowed.
        const auto& cache=server?g_serverCache:g_clientCache;
        if(!cache.attempted||(!server&&cache.identity.entity!=reinterpret_cast<std::uint32_t>(local)))return {};
        const auto context=P::ReadMovementContext(reinterpret_cast<std::uint32_t>(gameMovement),cache.identity.entity,Read);
        if(!context.valid)return {};
        auto s=server?Server(reinterpret_cast<void*>(context.player)):Client(local);
        s.movementMatched=true;
        // Verified in both PlayerMove bodies (mv+0x48). Do not read entity
        // velocity as a substitute for MoveData before FinishMove writes back.
        static_assert(offsetof(CMoveData,m_vecVelocity)==0x40,"native MoveData velocity layout changed");
        constexpr auto zOffset=offsetof(CMoveData,m_vecVelocity)+2*sizeof(float);
        s.moveZValid=context.move<=UINT32_MAX-zOffset
            &&Read(context.move+zOffset,&s.moveZ,sizeof(s.moveZ))&&std::isfinite(s.moveZ);
        return s;
    }
}
