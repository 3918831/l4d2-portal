#pragma once
#include "PortalNativePredictionErrorPolicy.h"
#include <array>
#include <charconv>
#include <cstring>

// Parses only bounded text already formatted by the native logger. No datamap/value reads.
namespace PortalNativeFieldPolicy
{
    template<class Observe,class Original> inline auto ForwardLog(bool& guard,Observe observe,Original original)->decltype(original())
    {
        struct Restore {bool& guard;bool previous;~Restore(){guard=previous;}} restore{guard,guard};
        guard=true;
        if(!restore.previous)observe();
        return original(); // listeners may recurse; forwarding still occurs once per invocation
    }
    struct Message
    {
        char className[160]={},fieldName[96]={},value[256]={};
        int ordinal=-1,index=-1;bool valid=false,readable=false,terminated=false;
    };
    inline bool Eligible(const PortalNativePredictionErrorPolicy::Observation& o,bool comparison,
        std::uint64_t epoch,std::uintptr_t local)
    {return comparison&&local&&o.localPlayer==local&&o.entity&&o.contextValid&&o.callerMatched
        &&o.frame>=0&&o.tick>=0&&std::isfinite(o.time)&&PortalNativePredictionErrorPolicy::InLifecycle(o,epoch);}
    inline Message Parse(const char* text,std::size_t n,int expectedIndex)
    {
        Message m;
        if(!text||!n||n>512) return m;
        m.readable=true;
        const auto* end=static_cast<const char*>(std::memchr(text,0,n));if(!end)return m;m.terminated=true;
        auto* p=text;while(p<end&&*p==' ')++p;
        auto first=std::from_chars(p,end,m.ordinal);if(first.ec!=std::errc()||m.ordinal<=0)return m;p=first.ptr;
        while(p<end&&*p==' ')++p;if(p==end||*p++!='(')return m;
        auto index=std::from_chars(p,end,m.index);if(index.ec!=std::errc()||m.index<0||m.index!=expectedIndex)return m;p=index.ptr;
        if(p==end||*p++!=')')return m;
        const auto* separator=std::strstr(p,"::");if(!separator||separator==p)return m;
        const auto* divider=std::strstr(separator+2," - ");if(!divider||divider==separator+2)return m;
        const auto cls=static_cast<std::size_t>(separator-p),field=static_cast<std::size_t>(divider-separator-2);
        auto* value=divider+3;while(end>value&&(end[-1]=='\r'||end[-1]=='\n'||end[-1]==' '))--end;
        const auto size=static_cast<std::size_t>(end-value);
        if(cls>=sizeof(m.className)||field>=sizeof(m.fieldName)||!size||size>=sizeof(m.value))return m;
        for(auto* c=p;c<end;++c) if(static_cast<unsigned char>(*c)<32||static_cast<unsigned char>(*c)>126)return m;
        std::memcpy(m.className,p,cls);std::memcpy(m.fieldName,separator+2,field);std::memcpy(m.value,value,size);
        m.valid=true;return m;
    }
    struct Field
    {
        Message first,last;unsigned occurrences=0;
    };
    struct Window
    {
        unsigned messages=0,parsed=0,unknown=0,unreadable=0,unterminated=0,overflow=0,emitted=0,dropped=0;
        std::array<Field,16> fields;
        bool Add(const Message& m)
        {
            ++messages;
            if(!m.valid){++unknown;unreadable+=!m.readable;unterminated+=m.readable&&!m.terminated;}
            else
            {
                ++parsed;Field* bucket=nullptr;
                for(auto& f:fields)if(f.occurrences&&f.first.index==m.index&&!std::strcmp(f.first.className,m.className)&&!std::strcmp(f.first.fieldName,m.fieldName)){bucket=&f;break;}
                if(!bucket)for(auto& f:fields)if(!f.occurrences){bucket=&f;f.first=m;break;}
                if(!bucket)++overflow;else {bucket->last=m;++bucket->occurrences;}
            }
            if(emitted>=48){++dropped;return false;}++emitted;return true;
        }
    };
}
