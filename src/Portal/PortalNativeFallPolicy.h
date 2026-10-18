#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Read-only L4D2 metadata decoder. Public L4D2 datamap.h + disk descriptors
// use localOffset at +8, embedded map at +28, size at +32, stride60.
// Never pass these bindings to the old ServerDataMap write API.
namespace PortalNativeFallPolicy
{
    inline unsigned ClientCommandPhase(const char* stage)
    {
        if(stage&&!std::strcmp(stage,"runcommand-pre-original"))return 2;
        if(stage&&!std::strcmp(stage,"runcommand-post-original"))return 3;
        return UINT32_MAX;
    }
    struct MovementContext {std::uint32_t player=0,move=0;bool valid=false;};
    template<class Read> MovementContext ReadMovementContext(std::uint32_t at,std::uint32_t expectedPlayer,Read read)
    {
        // Both native PlayerMove bodies read player at +4 and mv at +8.
        // The identity comes from the local RunCommand probe, never from gm.
        std::array<std::uint32_t,3> prefix{};
        if(!at||!expectedPlayer||at>UINT32_MAX-sizeof(prefix)||!read(at,prefix.data(),sizeof(prefix))
            ||prefix[1]!=expectedPlayer||!prefix[2])return {};
        return {prefix[1],prefix[2],true};
    }
    struct Region
    {
        std::uintptr_t begin=0,end=0;
        bool Covers(std::uintptr_t at,std::size_t size) const
        {return size&&begin<end&&at>=begin&&at<end&&size<=end-at;}
    };
    struct Identity
    {
        std::uint32_t entity=0,module=0,table=0,root=0;
        std::array<std::uint32_t,4> header{};
        bool operator==(const Identity& b) const
        {return entity==b.entity&&module==b.module&&table==b.table&&root==b.root&&header==b.header;}
    };
    struct Cache
    {
        Identity identity{};bool attempted=false;
        bool NeedsBind(const Identity& value) const {return !attempted||!(identity==value);}
        void Remember(const Identity& value){identity=value;attempted=true;}
        void Reset(){identity={};attempted=false;}
    };
    inline std::uint32_t MapFromGetter(const unsigned char* code,std::size_t n,std::uint32_t base,std::uint32_t end)
    {
        std::uint32_t map=0;
        if(!code||n<6||code[0]!=0xb8||code[5]!=0xc3||base>=end)return 0;
        std::memcpy(&map,code+1,4);
        return map>=base&&map<end&&16<=end-map?map:0;
    }
    struct Field
    {
        std::uint32_t descriptor=0,map=0,offset=0,embedded=0,size=0;
        unsigned type=0,count=0,flags=0,index=UINT32_MAX;char owner[64]={};bool valid=false;
    };
    struct Binding {std::uint32_t offset=0,descriptor=0,localDescriptor=0;bool valid=false;std::uint32_t localOffset=0;};
    struct TextStatus {const char* issue="none";std::uint32_t address=0;unsigned inspected=0;};
    struct Diagnostic
    {
        const char* stage="not-attempted";const char* issue="none";
        std::uint32_t map=0,descriptor=0;unsigned index=UINT32_MAX;
        char owner[64]={};const char* field="none";
        std::uint32_t offset=0,size=0,embedded=0;unsigned type=0,count=0,flags=0;
        TextStatus text;
    };
    inline void Describe(Diagnostic* d,const Field& f)
    {
        if(!d)return;d->map=f.map;d->descriptor=f.descriptor;d->index=f.index;std::memcpy(d->owner,f.owner,64);
        d->offset=f.offset;d->size=f.size;d->embedded=f.embedded;d->type=f.type;d->count=f.count;d->flags=f.flags;
    }
    struct RecvProperty {char name[64]={};std::uint32_t offset=0,child=0;int type=-1;};
    struct RecvResult {std::uint32_t offset=0;bool valid=false,failed=false;};
    template<class Count,class Entry> RecvResult FindRecvFloat(std::uint32_t root,Count count,Entry entry)
    {
        RecvResult result;std::array<std::uint32_t,17> ancestry{};unsigned visits=0,matches=0;
        auto walk=[&](auto&& self,std::uint32_t table,std::uint32_t base,unsigned depth)->void
        {
            if(result.failed)return;
            if(!table||depth>=ancestry.size()){result.failed=true;return;}
            for(unsigned i=0;i<depth;++i)if(ancestry[i]==table){result.failed=true;return;}
            ancestry[depth]=table;unsigned n=0;
            if(!count(table,n)||n>4096){result.failed=true;return;}
            for(unsigned i=0;i<n&&!result.failed;++i)
            {
                RecvProperty p;
                if(++visits>4096||!entry(table,i,p)||!std::memchr(p.name,0,sizeof(p.name))||p.offset>65536||base>65536-p.offset)
                {result.failed=true;return;}
                const auto offset=base+p.offset;
                if(!std::strcmp(p.name,"m_flFallVelocity"))
                {
                    if(p.type!=1||p.child||!offset||++matches!=1){result.failed=true;return;}
                    result.offset=offset;
                }
                else if(p.child)
                {
                    if(p.type!=6){result.failed=true;return;}
                    self(self,p.child,offset,depth+1);
                }
            }
        };
        walk(walk,root,0,0);result.valid=!result.failed&&matches==1;
        if(!result.valid)result.offset=0;return result;
    }
    template<class Read> bool Text(std::uint32_t at,char* out,Read read,TextStatus* status=nullptr)
    {
        if(status){*status={};status->address=at;}
        auto fail=[&](const char* issue){if(status)status->issue=issue;return false;};
        if(!out||!at||at>UINT32_MAX-64)return fail("address-range");
        out[0]=0;
        for(unsigned i=0;i<64;)
        {
            char block[16]={};unsigned n=64-i<16?64-i:16;
            // A short string can end at a page/image boundary. Shrink a
            // rejected block; never dereference across an unreadable region.
            while(!read(at+i,block,n)){if(n==1)return fail("read-rejected");n/=2;}
            for(unsigned j=0;j<n;++j)
            {
                const unsigned char c=static_cast<unsigned char>(block[j]);
                if(status)status->inspected=i+j+1;
                if(c&& (c<32||c>126))return fail("non-ascii");
                out[i+j]=static_cast<char>(c);if(!c)return i+j>0?true:fail("empty");
            }
            i+=n;
        }
        out[63]=0;
        return fail("unterminated");
    }
    // Structure addresses remain image-owned; SDK-generated function names may
    // be heap-owned. Only bounded Text() may use the separate text reader.
    template<class Read,class ReadText> Field Find(std::uint32_t root,const char* name,Read read,ReadText readText,Diagnostic* report=nullptr,const char* stage="field-search")
    {
        Field found;std::array<std::uint32_t,16> visited{};unsigned depth=0,total=0;
        if(report){*report={};report->stage=stage;report->field=name;}
        auto fail=[&](const char* issue)->Field{if(report)report->issue=issue;return {};};
        while(root)
        {
            if(report)
            {
                report->map=root;report->descriptor=0;report->index=UINT32_MAX;std::memset(report->owner,0,64);
                report->offset=report->size=report->embedded=0;report->type=report->count=report->flags=0;
            }
            if(depth==visited.size())return fail("map-depth");
            if(root>UINT32_MAX-16)return fail("map-range");
            for(unsigned i=0;i<depth;++i)if(visited[i]==root)return fail("map-cycle");
            visited[depth++]=root;
            std::array<std::uint32_t,4> map{};char owner[64]={};TextStatus textStatus;
            if(!read(root,map.data(),16))return fail("map-read");
            if(map[1]>4096||total+map[1]>8192)return fail("map-count");
            if(!Text(map[2],owner,readText,&textStatus)){if(report)report->text=textStatus;return fail("class-text");}
            if(report)std::memcpy(report->owner,owner,64);
            total+=map[1];
            if(map[1]&&(!map[0]||map[0]>UINT32_MAX-map[1]*60))return fail("descriptor-range");
            for(unsigned i=0;i<map[1];++i)
            {
                std::array<std::uint32_t,15> d{};char fieldName[64]={};const auto at=map[0]+i*60;
                if(report)
                {
                    report->descriptor=at;report->index=i;report->offset=report->size=report->embedded=0;
                    report->type=report->count=report->flags=0;
                }
                if(!read(at,d.data(),60))return fail("descriptor-read");
                if(report){report->type=d[0];report->offset=d[2];report->count=d[3]&0xffff;report->flags=d[3]>>16;report->embedded=d[7];report->size=d[8];}
                if(!d[1])continue;
                if(!Text(d[1],fieldName,readText,&textStatus)){if(report)report->text=textStatus;return fail("field-text");}
                if(std::strcmp(name,fieldName))continue;
                if(found.valid)return fail("duplicate-field");
                found.descriptor=at;found.map=root;found.index=i;found.offset=d[2];found.type=d[0];found.count=d[3]&0xffff;
                found.flags=d[3]>>16;found.embedded=d[7];found.size=d[8];std::memcpy(found.owner,owner,64);
                Describe(report,found);
                if(d[0]>29||d[2]>65536||!(d[3]&0xffff)||!d[8]||d[8]>65536)return fail("field-layout");
                found.valid=true;
            }
            root=map[3];
        }
        if(!found.valid)return fail("field-not-found");
        Describe(report,found);
        return found;
    }
    template<class Read,class ReadText> Binding BindFall(std::uint32_t root,Read read,ReadText readText,Diagnostic* report=nullptr)
    {
        Binding result;std::uint32_t className=0;char text[64]={};TextStatus textStatus;
        if(report){*report={};report->stage="root-class";report->map=root;}
        auto fail=[&](const char* issue)->Binding{if(report)report->issue=issue;return {};};
        if(!root||root>UINT32_MAX-12)return fail("map-range");
        if(!read(root+8,&className,4))return fail("class-read");
        if(!Text(className,text,readText,&textStatus)){if(report)report->text=textStatus;return fail("class-text");}
        if(report)std::memcpy(report->owner,text,64);
        if(std::strcmp(text,"CTerrorPlayer"))return fail("class-identity");
        const auto local=Find(root,"m_Local",read,readText,report,"local-search");
        if(!local.valid)return result;
        if(report)report->stage="local-validation";
        if(std::strcmp(local.owner,"CBasePlayer"))return fail("owner-identity");
        if(local.type!=10||local.count!=1)return fail("type-count");
        if(local.flags&0x40)return fail("pointer-field");
        if(!local.embedded)return fail("embedded-null");
        if(report){*report={};report->stage="embedded-class";report->map=local.embedded;report->field="m_Local";}
        if(local.embedded>UINT32_MAX-12)return fail("map-range");
        if(!read(local.embedded+8,&className,4))return fail("class-read");
        if(!Text(className,text,readText,&textStatus)){if(report)report->text=textStatus;return fail("class-text");}
        if(report)std::memcpy(report->owner,text,64);
        if(std::strcmp(text,"CPlayerLocalData"))return fail("class-identity");
        const auto fall=Find(local.embedded,"m_flFallVelocity",read,readText,report,"fall-search");
        if(!fall.valid)return result;
        if(report)report->stage="fall-validation";
        if(std::strcmp(fall.owner,"CPlayerLocalData"))return fail("owner-identity");
        if(fall.type!=1||fall.count!=1||fall.size!=4)return fail("type-count-size");
        if(fall.flags&0x40)return fail("pointer-field");
        if(fall.offset>local.size||4>local.size-fall.offset||local.offset>65536-fall.offset)return fail("embedded-offset-range");
        if(report){report->stage="bound";report->issue="none";}
        result.offset=local.offset+fall.offset;result.localOffset=local.offset;result.descriptor=fall.descriptor;result.localDescriptor=local.descriptor;result.valid=true;return result;
    }
    struct Sample
    {
        float fall=NAN,z=NAN,moveZ=NAN;std::uint32_t ground=0;
        int flags=0,moveType=-1,tickBase=-1;
        bool fallValid=false,zValid=false,groundValid=false,flagsValid=false,moveTypeValid=false,tickBaseValid=false;
        bool movementMatched=false,moveZValid=false;
    };
    struct Window
    {
        unsigned calls=0,valid=0,unavailable=0,groundNull=0,groundNonNull=0,groundUnknown=0;
        unsigned relationValid=0,mismatch=0,changed=0,held=0,emitted=0,dropped=0,suppressedUnavailable=0;
        unsigned movementMatched=0,moveZValid=0;
        float first=NAN,last=NAN,min=NAN,max=NAN;
        bool Add(const Sample& s)
        {
            ++calls;
            movementMatched+=s.movementMatched;moveZValid+=s.moveZValid&&std::isfinite(s.moveZ);
            if(s.groundValid)(s.ground==UINT32_MAX?groundNull:groundNonNull)++;else ++groundUnknown;
            if(!s.fallValid||!std::isfinite(s.fall))++unavailable;
            else
            {
                if(!valid)first=min=max=s.fall;
                else {changed+=std::fabs(last-s.fall)>.0001f;held+=std::fabs(last-s.fall)<=.0001f;min=std::fmin(min,s.fall);max=std::fmax(max,s.fall);}
                last=s.fall;++valid;
                if(s.zValid&&std::isfinite(s.z)){++relationValid;mismatch+=std::fabs(s.fall+s.z)>.5f;}
            }
            // Missing/nonfinite core values are still counted, not printed as
            // command rows full of unknowns. They don't consume detail budget.
            if(!s.fallValid||!std::isfinite(s.fall)){++suppressedUnavailable;return false;}
            if(emitted==48){++dropped;return false;}++emitted;return true;
        }
    };
}
