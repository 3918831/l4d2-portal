#pragma once
class C_BasePlayer;
namespace PortalNativeInterpolationHook
{
    struct Status { bool installed=false, dispatchValid=false, part1Installed=false, mapInstalled=false, localIdentity=false, originInstalled=false, selectionInstalled=false, resetInstalled=false, resetLatchedInstalled=false; };
    // Lazy installation from the local player's validated RTTI/vtable only.
    Status Inspect(C_BasePlayer* player);
}
