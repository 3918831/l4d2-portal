#pragma once
#include "PortalNativeFallPolicy.h"
class C_BasePlayer;
namespace PortalNativeFallProbe
{
    struct ServerLayout
    {
        PortalNativeFallPolicy::Identity identity;
        std::uint32_t fall=0,local=0,ground=0,moveType=0,lifeState=0;
        bool valid=false;
    };
    // Read-only metadata capability. A caller must independently validate any
    // write and native notification; this does not expose a legacy setter.
    ServerLayout InspectServer(void* player);
    void Reset();
    void Report(std::uint32_t capture,std::uint32_t transaction);
    PortalNativeFallPolicy::Sample Server(void* player);
    PortalNativeFallPolicy::Sample Client(C_BasePlayer* player);
    PortalNativeFallPolicy::Sample Movement(bool server,void* gameMovement,C_BasePlayer* local);
}
