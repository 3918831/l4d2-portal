#pragma once
#include "PortalNativeOriginFingerprint.h"
#include "PortalNativePredictionErrorFingerprint.h"
namespace PortalNativeLoggingFingerprint
{
    inline constexpr const char* kExport="558becb8010000008405e8430410751d0905e8430410b908cf0310e850f2ffff6860fc0210e84366010083c4048b45148b4d108b550c508b4508515250b908cf0310e859fdffff5dc3";
    inline constexpr unsigned kExportRelocs[]={10,18,23,33,62};
    inline constexpr const char* kCore="558bec83ec1ca110b9031033c58945fc8b45088b550c538945ec568d0440578bf98b4d14c1e00403c7894de48b4804894df08b4d10897de88955f43b0de8ce0310750583c00ceb038d45108b108bb70c7000008955f8ff15840003108bc88b063bc8740e8bd18bde33c0f00fb11385c07505ff4604eb0cf3906a00518bcee86d570000b904cf0310e8d355000085c075068b87107000006bc04c8d9c381470000033f63973047e248d7b08eb038d49008b45e48b0f8b118b12508d45ec50ffd24683c7043b73047ce78b7de88b5b488b038b108d4dec518bcbffd28bd88b870c700000ff48048b4804750433d287108bc3487461487573f605e8430410018b3500cf0310751e830de843041001b908cf0310e8b9f3ffff6860fc0210e8ac67010083c4048d0476c1e00483b810cf0310007f168b0d00cf031068500a03106a0051e8cafbffff83c40c6a01e8c02d000083c404eb15837d0c02740f6a00e83e2e000083c40484c07401cc8b4dfc5f5e8bc333cd5be85b5701008be55dc21000";
    inline constexpr unsigned kCoreRelocs[]={7,61,88,132,249,256,264,270,280,300,309,314};
    inline std::uintptr_t Target(const unsigned char* p,std::size_t n,std::uintptr_t address,std::uintptr_t base)
    {return PortalNativeOriginFingerprint::Match(p,n,kExport,base,kExportRelocs)?PortalNativePredictionErrorFingerprint::Relative(p,n,66,address):0;}
    inline std::uintptr_t Object(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {if(!PortalNativeOriginFingerprint::Match(p,n,kExport,base,kExportRelocs))return 0;std::uint32_t object=0;std::memcpy(&object,p+62,4);return object;}
    inline bool Core(const unsigned char* p,std::size_t n,std::uintptr_t base)
    {return PortalNativeOriginFingerprint::Match(p,n,kCore,base,kCoreRelocs);}
}
