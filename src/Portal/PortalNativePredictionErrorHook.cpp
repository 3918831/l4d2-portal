#include "PortalNativePredictionErrorHook.h"
#include "PortalNativePredictionErrorFingerprint.h"
#include "PortalNativePredictionErrorPolicy.h"
#include "PortalNativeLoggingFingerprint.h"
#include "PortalNativeFieldPolicy.h"
#include "PortalNativeDiagnostics.h"
#include "../SDK/SDK.h"
#include "../Util/Hook/MinHook/MinHook.h"
#include "../Util/Logger/Logger.h"
#include <Windows.h>
#include <intrin.h>
#include <cstring>

namespace PortalNativePredictionErrorHook
{
    namespace
    {
        namespace FP=PortalNativePredictionErrorFingerprint;
        namespace Policy=PortalNativePredictionErrorPolicy;
        namespace LogFP=PortalNativeLoggingFingerprint;
        namespace Field=PortalNativeFieldPolicy;
        using NetworkOriginal=void (__thiscall*)(void*,int);
        using EntityOriginal=bool (__thiscall*)(void*,int);
        using TransferOriginal=int (__thiscall*)(void*,const char*,int,void*);
        // Public LogDirect's four 32-bit stack arguments; verified native member ret16.
        using LogOriginal=int (__thiscall*)(void*,int,int,std::uint32_t,const char*);
        NetworkOriginal g_networkOriginal=nullptr;EntityOriginal g_entityOriginal=nullptr;TransferOriginal g_transferOriginal=nullptr;
        std::uintptr_t g_base=0,g_end=0,g_network=0,g_entity=0,g_transfer=0;
        bool g_attempted=false,g_installed=false;
        std::uint32_t g_packetSequence=0;
        thread_local Policy::PacketScope g_packet;
        thread_local Policy::Observation* g_comparison=nullptr;
        LogOriginal g_logOriginal=nullptr;
        bool g_fieldInstalled=false;
        std::uintptr_t g_logObject=0;
        thread_local Policy::Observation* g_nativePass=nullptr;
        thread_local int g_nativeIndex=-1;
        thread_local bool g_logging=false;

        bool Readable(std::uintptr_t address,std::size_t size)
        {
            if(!address||!size||address>UINTPTR_MAX-size) return false;
            MEMORY_BASIC_INFORMATION m{};
            if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT
                ||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
            const auto p=m.Protect&0xff;const auto start=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
            const bool readable=p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY
                ||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
            return readable&&start<=UINTPTR_MAX-m.RegionSize&&address>=start&&address+size<=start+m.RegionSize;
        }
        template<class T> bool Read(std::uintptr_t address,T& out)
        {if(!Readable(address,sizeof(T)))return false;std::memcpy(&out,reinterpret_cast<void*>(address),sizeof(T));return true;}
        bool InImage(std::uintptr_t p,std::size_t n)
        {return p>=g_base&&p<g_end&&n<=g_end-p&&Readable(p,n);}
        bool Code(std::uintptr_t p,std::size_t n)
        {
            MEMORY_BASIC_INFORMATION m{};
            if(!InImage(p,n)||!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))
                ||reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=g_base) return false;
            const auto protection=m.Protect&0xff;
            return protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        }
        bool ImageBounds(const char* module,std::uintptr_t& base,std::uintptr_t& end)
        {
            base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(module));
            IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
            if(!Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0
                ||base>UINTPTR_MAX-static_cast<unsigned>(dos.e_lfanew)||!Read(base+dos.e_lfanew,nt)
                ||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386
                ||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||!nt.OptionalHeader.SizeOfImage
                ||base>UINTPTR_MAX-nt.OptionalHeader.SizeOfImage) return false;
            end=base+nt.OptionalHeader.SizeOfImage;return true;
        }
        bool Image(){return ImageBounds("client.dll",g_base,g_end);}
        bool TypeName(std::uintptr_t entity,char (&name)[96])
        {
            struct Locator {std::uint32_t signature,offset,cdOffset,type,hierarchy;};
            std::uint32_t table=0,col=0;Locator locator{};
            if(!Read(entity,table)||table<4||!InImage(table-4,4)||!Read(table-4,col)||!InImage(col,sizeof(locator))
                ||!Read(col,locator)||locator.signature||locator.offset||locator.cdOffset
                ||!InImage(locator.type,8+sizeof(name))||!InImage(locator.hierarchy,16)) return false;
            char text[96]={};std::memcpy(text,reinterpret_cast<void*>(locator.type+8),sizeof(text));
            const auto* end=static_cast<const char*>(std::memchr(text,0,sizeof(text)));
            if(!end||end==text) return false;
            for(const char* p=text;p<end;++p) if(*p<32||*p>126) return false;
            std::memcpy(name,text,static_cast<std::size_t>(end-text)+1);return true;
        }
        bool PredictionIdentity(void* prediction,std::uintptr_t& network)
        {
            if(!prediction||prediction!=I::Prediction) return false;
            char type[96]={};std::uint32_t table=0,target=0;
            if(!TypeName(reinterpret_cast<std::uintptr_t>(prediction),type)
                ||(std::strcmp(type,".?AVCPrediction@@")&&std::strcmp(type,".?AVCCSPrediction@@"))
                ||!Read(reinterpret_cast<std::uintptr_t>(prediction),table)||!InImage(table,7*4)||!Read(table+6*4,target)
                ||!Code(target,597)) return false;
            network=target;return true;
        }
        void __fastcall NetworkDetour(void* prediction,void*,int ack)
        {
            const auto previous=g_packet;g_packet={};
            auto* local=PortalNativeDiagnostics::OriginResetPlayer();std::uintptr_t target=0;
            if(g_installed&&local&&PredictionIdentity(prediction,target)&&target==g_network)
                g_packet={reinterpret_cast<std::uintptr_t>(prediction),reinterpret_cast<std::uintptr_t>(local),++g_packetSequence,ack,true,true,PortalNativeDiagnostics::PredictionErrorEpoch()};
            g_networkOriginal(prediction,ack); // original void/ret4 exactly once
            g_packet=previous;
        }
        bool __fastcall EntityDetour(void* entity,void*,int ack)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            auto* local=PortalNativeDiagnostics::OriginResetPlayer();Policy::Observation o;
            const bool observe=g_installed&&Policy::InLifecycle(g_packet,PortalNativeDiagnostics::PredictionErrorEpoch())
                &&Policy::Eligible(g_packet,reinterpret_cast<std::uintptr_t>(I::Prediction),
                reinterpret_cast<std::uintptr_t>(local),caller,g_network+190)
                &&PortalNativeDiagnostics::PredictionErrorBegin(g_packet,entity,ack,o);
            if(observe){o.callerMatched=true;o.typeKnown=TypeName(reinterpret_cast<std::uintptr_t>(entity),o.type);}
            auto* previous=g_comparison;g_comparison=observe?&o:nullptr;
            const bool result=g_entityOriginal(entity,ack); // natural bool/ret4, no extra invocation
            g_comparison=previous;
            if(observe){o.hadErrors=result;PortalNativeDiagnostics::PredictionErrorEnd(o);}
            return result;
        }
        int __fastcall TransferDetour(void* copy,void*,const char* operation,int entityIndex,void* map)
        {
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            auto* observation=g_comparison;
            auto* previous=g_nativePass;const int previousIndex=g_nativeIndex;
            // Only the exact error comparison call may own formatted native messages.
            g_nativePass=observation&&caller==g_entity+230?observation:nullptr;g_nativeIndex=entityIndex;
            const int result=g_transferOriginal(copy,operation,entityIndex,map); // original int/ret12 exactly once
            g_nativePass=previous;g_nativeIndex=previousIndex;
            if(observation&&caller==g_entity+230)
            {
                ++observation->comparisonCalls;observation->index=entityIndex;observation->indexKnown=true;
                observation->map=reinterpret_cast<std::uintptr_t>(map);observation->errors=result;observation->countKnown=true;
            }
            return result;
        }
        int __fastcall LogDetour(void* system,void*,int channel,int severity,std::uint32_t color,const char* text)
        {
            return Field::ForwardLog(g_logging,[&]
            {
                auto* owner=g_nativePass;
                if(g_fieldInstalled&&reinterpret_cast<std::uintptr_t>(system)==g_logObject&&severity==0&&owner
                    &&Field::Eligible(*owner,true,PortalNativeDiagnostics::PredictionErrorEpoch(),reinterpret_cast<std::uintptr_t>(PortalNativeDiagnostics::OriginResetPlayer())))
                {
                    Field::Message m;
                    if(Readable(reinterpret_cast<std::uintptr_t>(text),512))m=Field::Parse(text,512,g_nativeIndex);
                    ++owner->nativeMessages;owner->nativeFields+=m.valid;
                    PortalNativeDiagnostics::PredictionFieldMessage(*owner,m);
                }
            },[&]{return g_logOriginal(system,channel,severity,color,text);});
        }
        void InstallFieldHook()
        {
            std::uintptr_t base=0,end=0;
            const bool image=ImageBounds("tier0.dll",base,end);
            const auto exported=image?reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(base),"LoggingSystem_LogDirect")):0;
            auto code=[&](std::uintptr_t p,std::size_t n){MEMORY_BASIC_INFORMATION m{};if(!p||p<base||p>=end||n>end-p||!Readable(p,n)||!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))||reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=base)return false;const auto protection=m.Protect&0xff;return protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;};
            const auto target=code(exported,73)?LogFP::Target(reinterpret_cast<unsigned char*>(exported),73,exported,base):0;
            const auto object=target?LogFP::Object(reinterpret_cast<unsigned char*>(exported),73,base):0;
            const bool signature=code(target,383)&&LogFP::Core(reinterpret_cast<unsigned char*>(target),383,base);
            const bool objectValid=signature&&object>=base&&object<end&&Readable(object,4);
            MH_STATUS create=MH_UNKNOWN,enable=MH_UNKNOWN;
            if(objectValid)
            {
                g_logObject=object;
                create=MH_CreateHook(reinterpret_cast<void*>(target),&LogDetour,reinterpret_cast<void**>(&g_logOriginal));
                if(create==MH_OK)enable=MH_EnableHook(reinterpret_cast<void*>(target));
                g_fieldInstalled=create==MH_OK&&enable==MH_OK;
                if(create==MH_OK&&!g_fieldInstalled)MH_RemoveHook(reinterpret_cast<void*>(target));
            }
            U::LogInfo("[PortalNativePredictionFieldHook] installed=%s rootInstalled=%s exportValidated=%s memberValidated=%s objectValidated=%s abiValidated=%s provider=tier0.dll publicExport=LoggingSystem_LogDirect exportBytes=73 memberBytes=383 memberStackArgs=4 memberReturn=int memberRetPop=16 exportRva=0x%X memberRva=0x%X objectRva=0x%X create=%d enable=%d source=natural-formatted-message privatePredictionBuffers=untouched loggingListeners=unchanged nativeConvars=not-written passive=true behaviorMutation=false.\n",
                g_fieldInstalled?"true":"false",g_installed?"true":"false",target?"true":"false",signature?"true":"false",objectValid?"true":"false",signature?"true":"false",
                exported?static_cast<unsigned>(exported-base):0,signature?static_cast<unsigned>(target-base):0,objectValid?static_cast<unsigned>(object-base):0,create,enable);
        }
    }
    Status Inspect()
    {
        std::uintptr_t network=0;
        if(!g_attempted)
        {
            g_attempted=true;
            const bool image=Image(),identity=image&&PredictionIdentity(I::Prediction,network);
            const auto entity=identity?FP::EntityTarget(reinterpret_cast<unsigned char*>(network),597,network,g_base):0;
            const auto transfer=entity&&Code(entity,265)?FP::TransferTarget(reinterpret_cast<unsigned char*>(entity),265,entity,g_base):0;
            std::uint32_t jumpTable=0;
            const bool bodies=transfer&&Code(transfer,150)&&Read(transfer+42,jumpTable)&&InImage(jumpTable,16)
                &&FP::ValidTransfer(reinterpret_cast<unsigned char*>(transfer),150,reinterpret_cast<unsigned char*>(jumpTable),16,g_base);
            MH_STATUS nc=MH_UNKNOWN,ec=MH_UNKNOWN,tc=MH_UNKNOWN,ne=MH_UNKNOWN,ee=MH_UNKNOWN,te=MH_UNKNOWN;
            if(bodies)
            {
                g_network=network;g_entity=entity;g_transfer=transfer;
                nc=MH_CreateHook(reinterpret_cast<void*>(network),&NetworkDetour,reinterpret_cast<void**>(&g_networkOriginal));
                ec=MH_CreateHook(reinterpret_cast<void*>(entity),&EntityDetour,reinterpret_cast<void**>(&g_entityOriginal));
                tc=MH_CreateHook(reinterpret_cast<void*>(transfer),&TransferDetour,reinterpret_cast<void**>(&g_transferOriginal));
                if(nc==MH_OK&&ec==MH_OK&&tc==MH_OK)
                {
                    ne=MH_EnableHook(reinterpret_cast<void*>(network));ee=MH_EnableHook(reinterpret_cast<void*>(entity));te=MH_EnableHook(reinterpret_cast<void*>(transfer));
                    g_installed=ne==MH_OK&&ee==MH_OK&&te==MH_OK;
                }
                if(!g_installed)
                {
                    if(nc==MH_OK){MH_DisableHook(reinterpret_cast<void*>(network));MH_RemoveHook(reinterpret_cast<void*>(network));}
                    if(ec==MH_OK){MH_DisableHook(reinterpret_cast<void*>(entity));MH_RemoveHook(reinterpret_cast<void*>(entity));}
                    if(tc==MH_OK){MH_DisableHook(reinterpret_cast<void*>(transfer));MH_RemoveHook(reinterpret_cast<void*>(transfer));}
                }
            }
            U::LogInfo("[PortalNativePredictionErrorHook] installed=%s predictionIdentityValidated=%s signatureValidated=%s abiValidated=%s jumpTableValidated=%s slot=6 networkBytes=597 entityBytes=265 transferBytes=150 rootStackArgs=1 rootReturn=void entityStackArgs=1 entityReturn=bool transferStackArgs=3 transferReturn=int rootRva=0x%X entityRva=0x%X transferRva=0x%X entityCallerRva=0x%X comparisonCallerRva=0x%X create(network=%d entity=%d transfer=%d) enable(network=%d entity=%d transfer=%d) reads=native-arguments-and-returns privatePredictionBuffers=untouched slotAttribution=unknown passive=true behaviorMutation=false.\n",
                g_installed?"true":"false",identity?"true":"false",bodies?"true":"false",bodies?"true":"false",bodies?"true":"false",
                identity?static_cast<unsigned>(network-g_base):0,bodies?static_cast<unsigned>(entity-g_base):0,bodies?static_cast<unsigned>(transfer-g_base):0,
                bodies?static_cast<unsigned>(network+190-g_base):0,bodies?static_cast<unsigned>(entity+230-g_base):0,nc,ec,tc,ne,ee,te);
            if(g_installed)InstallFieldHook();
        }
        return {g_installed,g_installed&&PredictionIdentity(I::Prediction,network)&&network==g_network,g_fieldInstalled};
    }
}
