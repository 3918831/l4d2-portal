#pragma once
#include <cstdint>

namespace PortalFallWrite
{
    void Initialize(void* nativePlayerMove);
    void Arm(void* serverPlayer,std::uint32_t traversalEpoch,int committedCmd);
    void Reset(const char* reason);
    void SetEnabled(bool enabled);
    void Report();
    class PlayerMoveScope
    {
        void* previous=nullptr;int previousCmd=0;
    public:
        PlayerMoveScope(void* gameMovement,int command);
        ~PlayerMoveScope();
        PlayerMoveScope(const PlayerMoveScope&)=delete;
        PlayerMoveScope& operator=(const PlayerMoveScope&)=delete;
    };
}
