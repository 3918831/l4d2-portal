#pragma once
#include "PortalNativeOriginFingerprint.h"

namespace PortalFallWriteFingerprint
{
    struct Sites {unsigned count=0;std::size_t offset=0;};
    inline std::uint32_t U32(const unsigned char* p,unsigned offset)
    {std::uint32_t v=0;std::memcpy(&v,p+offset,4);return v;}
    // Caller first validates full blocks. These arguments come from datamaps,
    // never the literals in the fingerprint, so no unverified offset fallback.
    inline bool Fields(const unsigned char* update,const unsigned char* ground,std::uint32_t fall,std::uint32_t local,std::uint32_t moveType,std::uint32_t groundField)
    {
        return update&&ground&&local&&fall>local&&fall-local==100
            &&U32(update,29)==fall&&U32(update,42)==fall
            &&U32(update,50)==moveType&&U32(ground,2)==groundField;
    }
    // Full supported update block + callees, not a loose opcode search. Field
    // immediates must ALSO agree with independently discovered live datamaps.
    inline bool Update(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {
        constexpr unsigned relocs[]={36,68,93};
        return PortalNativeOriginFingerprint::Match(p,n,"8b4e040fb6813f02000089460ce847c6040085c075718b4604f30f1088a81a00000f2f0d602b5d108db8a81a0000762280b8720100000275198b4e08f30f1041480f570530335d100f2fc876140f28c1eb0f8b4e08f30f1041480f570530335d10f30f100f0f2ec89ff30f1145fcf6c4447b148b579c8b028d4f9c57ffd0f30f1045fcf30f1107",base,relocs);
    }
    inline Sites FindUpdates(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {
        Sites sites;if(!p||n<135)return sites;
        for(std::size_t i=0;i<=n-135;++i)
            if(p[i]==0x8b&&p[i+1]==0x4e&&p[i+2]==4&&Update(p+i,135,base))
            {++sites.count;sites.offset=i;}
        return sites;
    }
    inline bool Ground(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {
        constexpr unsigned relocs[]={13};
        return PortalNativeOriginFingerprint::Match(p,n,"8b814402000083f8ff74238b15b8e975108bc881e1ff0f000003c98d4cca0485c9740bc1e80c39410475038b01c333c0c3",base,relocs);
    }
    inline bool Notify(const unsigned char* p,std::size_t n)
    {
        constexpr unsigned none[]={UINT32_MAX};
        return PortalNativeOriginFingerprint::Match(p,n,"80b920e6ffff00740a808924e6ffff01c204008b89e4e5ffff85c97411810901010000e8d8c2f7ff33c966894802c20400",0,none);
    }
    inline bool Accessor(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {
        constexpr unsigned relocs[]={3};
        return PortalNativeOriginFingerprint::Match(p,n,"518b0d98767f108b118b8284010000ffd0c3",base,relocs);
    }
}
