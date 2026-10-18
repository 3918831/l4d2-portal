#pragma once
#include "PortalNativeOriginFingerprint.h"
#include <limits>
namespace PortalNativePredictionErrorFingerprint
{
    inline constexpr const char* kNetwork="558bec83ec205356578bf18b0d14486c108b018b505433ff397d08c645ff000f9f45feffd28845fda1e42878108b4830894de4897df0e8456f03008bd883fbff0f84a70100008b45088d149b014496208d7c961833c0897df889470c3845fd0f840a010000538945e88945ece8ef5cecff8b400c83c4048945e0c745f40000000085c00f8ead0000008da4240000000053e8ca5cecff8b088b55f48b3c9183c40485ff747e8bcfe88473ecff84c074738b45f88b4808518bcfe8f219edff84c0740a8b55f8c7420c010000008b45e485c075373845fe744b3845ff7546398694000000743e578bcee8a3eeffff84c074328b45f88b4808518b8e9400000057c645ff01e808f9faffeb198d55ec528d4de851578b7df050578bcee821f2ffff47897df08b45f4408945f43b45e00f8c5dffffff8b7df88b55ec8b45e88b4de4528b55f05051528bcee823ebffff807dfe00741c53e84778eeff83c40485c074778b4f088b168b52585150538bceffd283be9400000000a12c2978108b78307457807dfe007451807dff00754b83ffff74468b0dac506c108b91140001008b421881c114000100ffd085ff782b3bf87f278b0dac506c1057e834d1f3ff85c074178b4df88b51088b8e940000005250e83df8faffc645ff0153e8d36e03008bd883c40483fbff0f855bfeffff33ff8b0d14486c108b018b50548a5e09ffd23ac3741e84db75078bcee8a4f0ffff897e1c897e20897e30897e34c7460cffffffff8b0d14486c108b018b5054ffd28846098bb6940000003bf77413807dfe00740d807dff0075078bcee8f4e7faff5f5e5b8be55dc20400";
    inline constexpr unsigned kNetworkRelocs[]={13,41,375,403,434,495,545};
    inline constexpr const char* kEntity="558bec83ec44837d08005356576a006a006a008bf18b86dc050000566a01506a028d4dbc0f9f45ffe8430913008b068b50488bceffd2508b46088b50208d4e08ffd25068484d54108d4dbce8302d13008d4de8e808edffffa1542878108b583083fb020f9dc185db79128b56088b42208d4e08ffd0f7db3bc30f94c1807dff00747c83bedc05000000750433d2eb138b45084899bb96000000f7fb8b94968403000033c084c90f95c06a008d4dbc40508b86dc0500006a01506a01526a01e8ad0813008b168b42488bceffd08b5608508b42208d4e08ffd05068abf352108d4dbce89a2c130085c08d4de80f9fc3e86decffff5f5e8ac35b8be55dc204005f5e32c05b8be55dc20400";
    inline constexpr unsigned kEntityRelocs[]={68,89,218};
    inline constexpr const char* kTransfer="558bec538b5d0c568b7510578bf9895f1c85f6740b837e14007505e8f0fcffff8b0783f8037731ff2485080418108b450856508bcfe886daffffeb318b4d0856518bcfe868fbffffeb238b550856528bcfe89afbffffeb158b45085653508bcfe81bffffff568bcfe8b3d9ffff8b472485c074180fb7480c8b5710518b4c9030034f0851508bcfe8a4ceffff8b47185f5e5b5dc20c00";
    inline constexpr unsigned kTransferRelocs[]={42};
    inline constexpr const char* kTransferTable="c80318109e031810ac031810ba031810";
    inline constexpr unsigned kTransferTableRelocs[]={0,4,8,12};
    inline std::uintptr_t Relative(const unsigned char* p,std::size_t n,unsigned at,std::uintptr_t address)
    {
        if(!p||at>n||n-at<5||p[at]!=0xe8||!address||address>UINT32_MAX-n) return 0;
        std::int32_t delta=0;std::memcpy(&delta,p+at+1,4);
        const auto target=static_cast<std::int64_t>(address)+at+5+delta;
        return target>0&&target<=UINT32_MAX?static_cast<std::uintptr_t>(target):0;
    }
    inline std::uintptr_t EntityTarget(const unsigned char* p,std::size_t n,std::uintptr_t address,std::uintptr_t base)
    {
        return PortalNativeOriginFingerprint::Match(p,n,kNetwork,base,kNetworkRelocs)?Relative(p,n,185,address):0;
    }
    inline std::uintptr_t TransferTarget(const unsigned char* p,std::size_t n,std::uintptr_t address,std::uintptr_t base)
    {
        if(!PortalNativeOriginFingerprint::Match(p,n,kEntity,base,kEntityRelocs)) return 0;
        const auto target=Relative(p,n,225,address);
        return target&&target==Relative(p,n,75,address)?target:0;
    }
    inline bool ValidTransfer(const unsigned char* p,std::size_t n,const unsigned char* table,std::size_t nt,std::uintptr_t base)
    {
        return PortalNativeOriginFingerprint::Match(p,n,kTransfer,base,kTransferRelocs)
            &&PortalNativeOriginFingerprint::Match(table,nt,kTransferTable,base,kTransferTableRelocs);
    }
}
