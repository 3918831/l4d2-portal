#pragma once
#include <cstdint>
#include <cstring>

namespace PortalNativeOriginFingerprint
{
    // Complete supported PE32 bodies, including every absolute relocation. No
    // wildcard fields, pointer casts to SDK layouts, or RVA-only acceptance.
    inline constexpr const char* kReset="33c0568bf189460e8b460433c985c074658b15a8496c10d9420c5150518bced91c24e809e7ffff8b46046a0050518b0da8496c10d9410c8bced91c24e8efe6ffff8b5604a1a8496c10d9400c6a0052518bced91c24e8d6e6ffff0fb6461d8b56048d0c408b461403c903c9515250e8dd29430083c40c5ec3";
    inline constexpr unsigned kResetRelocs[]={19,48,69};
    inline constexpr const char* kLatchedWrapper="56578bf98bb78806000085f674398d8e10100000e847caffff8d8efc0f0000e88cf9ffff56c786f00f00000000000089b6f40f0000e8865afcff83c404c78788060000000000008bcf5f5ee9e0ba0000";
    inline constexpr const char* kLatchedBase="538bd98b038b90e0010000ffd284c07525578b7b2485ff7e1c5633f68d6424008b43188b4c30088b118b4214ffd083c60c4f75ec5e5f5bc3";
    template<std::size_t N>
    inline bool Match(const unsigned char* p,std::size_t size,const char* hex,
        std::uintptr_t imageBase,const unsigned (&relocs)[N])
    {
        const auto bytes=std::strlen(hex)/2;
        if(!p||size<bytes||imageBase>UINT32_MAX) return false;
        auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for(std::size_t i=0;i<bytes;)
        {
            bool relocated=false;for(auto at:relocs) if(i==at) relocated=true;
            if(relocated)
            {
                if(bytes-i<4) return false;
                std::uint32_t disk=0,actual=0;
                for(unsigned j=0;j<4;++j) disk|=unsigned((digit(hex[2*(i+j)])<<4)|digit(hex[2*(i+j)+1]))<<(8*j);
                std::memcpy(&actual,p+i,4);
                const auto expected=static_cast<std::uint64_t>(imageBase)+disk-0x10000000u;
                if(disk<0x10000000u||expected>UINT32_MAX||actual!=expected) return false;
                i+=4;
            }
            else if(p[i]!=((digit(hex[2*i])<<4)|digit(hex[2*i+1]))) return false;
            else ++i;
        }
        return true;
    }
    // Complete bodies have no absolute operands. The tail preserves the
    // upstream return address; do not infer this from an RVA alone.
    inline std::uintptr_t LatchedBaseTarget(const unsigned char* p,std::size_t size,std::uintptr_t address)
    {
        constexpr unsigned none[]={UINT32_MAX};
        if(!address||address>UINT32_MAX-80||!Match(p,size,kLatchedWrapper,0,none)) return 0;
        std::int32_t displacement=0;std::memcpy(&displacement,p+76,4);
        const auto target=static_cast<std::int64_t>(address)+80+displacement;
        return target>0&&target<=UINT32_MAX?static_cast<std::uintptr_t>(target):0;
    }
    inline bool ValidLatchedBase(const unsigned char* p,std::size_t size,std::uint32_t mapOffset)
    {
        constexpr unsigned none[]={UINT32_MAX};
        return Match(p,size,kLatchedBase,0,none)&&mapOffset==p[34]&&mapOffset+12==p[20];
    }
    inline constexpr const char* kCtor=
        "8dbef80200008d4f08c707804a541033c033db6a048919894104c7410800001000e8f497ffff0f57c06a018bcfc7472884525410895f04f30f11472466c7471c0100f30f114718895f14895f20c707804d5410e8424effff8d9e240300008d4b08c703b44a541033c06a04c70100000000894104c7410800001000e86a98ffff0f57c033c06a018bcbc7432864525410894304f30f11432466c7431c0100f30f114318894314894320c703b44d5410e8864effff83c8ff8986f40500008986f805000033c9898e24060000898e28060000898e2c060000898e3006000033c0394e247e3e8b561883c208393a740b4083c20c3b46247cf3eb298945fc8b078b501c8bcfffd2a808743c8b45fc8b4e188d04408b5481046a01528bcee8badbffff33c98d86a00000008945f48d45f050518d4e18897df8c745f002000100e878dcffff";
    inline constexpr unsigned kCtorRelocs[]={11,48,79,99,140,171};
    inline constexpr const char* kWrapper="558becd9412483ec08d95c2404d94508d91c24e8b8e4ffff5dc20400";
    inline constexpr const char* kInner=
        "558bec83ec1cd9450c578d45fc5083ec08d95c24048bf9d945088d4de4d91c24518bcfc745fc00000000e881d5ffff84c00f849a010000807de400535674590fb7470e8b55f00fb75f0c03d03bd37c022bd38b4dec03c83bcb7c022bcb0fb75f0c0345e83bc37c022bc38b7708d945f46a00c1e20403d6528b5704c1e10403ce51c1e00403c65051d91c24528bcfe89dd7ffffe9180100008b5df03b5dec0f85d1000000803d24e67510008d43018945f80f848800000085c00f88800000000fb74f103bc17d78508d4f08e8b0adfffff30f10000f2e0528f352109ff6c4447b5ef30f10450c0f2f05341554107650f30f104f180f2f0d28e6751072428b154ce67510d9422cf30f104d0883ec08d95c2404f30f5cc8f30f110c24538d4f08e85cadffff508b45f8508d4f08e84fadffff8b4f0450518bcfe883adffffeb710fb7470e0fb7570c8d0c183bca7c022bca0345ec3bc27c022bc28b7708d945f48b5704c1e10403ce51c1e00403c65051d91c2452eb340fb7470e0fb7570c8d0c183bca7c022bca0345ec3bc27c022bc28b7708d945f4c1e10403cec1e0045103c6508b470451d91c24508bcfe850aefffff30f104508f30f5c450cf30f5c05b0125410518bcff30f110424e841f1ffff5e5b8b45fc5f8be55dc20800";
    inline constexpr unsigned kInnerRelocs[]={166,215,233,247,255,446};
    inline constexpr const char* kSelect=
        "558bec518b55080f57d2f30f10450cf30f5c45105383cbff5633c0c60200f30f115210895a0c895a08895a0433f657663b411073378972080fb7410e0fb7790c03c63bc77c022bc78b790803c0f30f100cc70f2eca9ff6c4447b110f2fc8762589720c0fb74110463bf07cc98b420c3bc30f84ea0100005f5e894208b0015b8be55dc210008b5a0c83fbff751b8b451489720c85c07406c700010000005f5eb0015b8be55dc210000fb7410e0fb7790c03c33bc77c022bc78b7908f30f101d4cf55310c1e004f30f101438f30f5cd10f2fd30f867e010000f30f5cc1f30f5ec2f30f101574e753100f2fd0f30f11421077030f28c2f30f114210f6411c108d7e01753285ff782e0fb741103bf87d26897a040fb7410e03c70fb7790c3bc77c022bc78b7908c1e004f30f5c0c380f2fcb7603c60201837d14000f84170100006633c0663b41101bc0f7d8483bd80f85030100000fb6411d0fb7590e8d044003c003c08945108bf88d04330fb7710c895d083bc67c022bc68b490803c0894d0c8d4cc1048b420c03c38945fc3bc67c022bc68b5d0c03c08d44c30483ff047219908b183b190f85ac00000083ef0483c10483c00483ff0473e885ff74288a193a180f859000000083ff0176198a59013a58010f857f00000083ff0276088a49023a48027572803a0074648b52040355088b7d103bd67c022bd68b45fc8b4d0c03d28d54d1043bc67c022bc603c08d44c10483ff0472148b083b0a753b83ef0483c20483c00483ff0473ec85ff74208a0a3a08752383ff0176158a4a013a4801751683ff0276088a52023a500275098b4514c700010000005f5eb0015b8be55dc210008b42085f33c93bc35e89420c0f95c05b8be55dc21000";
    inline constexpr unsigned kSelectRelocs[]={191,228};
    inline constexpr const char* kWriter=
        "558becf30f104d08535633db578bf9385d100f848800000066395f107450f30f1005fc1454100fb7470e0fb74f0c3bc17c022bc18b4f0803c0f30f1014c1f30f5cd10f2fd076270fb75710663bd376180fb7470e0fb74f0c403bc17c022bc14a6689470e6689571066395f1075b80fb757108d770842528bcee8b2d1ffff0fb746040fb74e0666ff46088d4c01ff3bc87c022bc866894e06895d10e9ab0000000fb757108d770842528bcee880d1ffff0fb74e040fb7460666ff46088d4408ff3bc17c022bc1ba010000008bca66894606895d10663b4f1073718d9b000000000fb75e060fb7460403da3bd87c022bd88b0ef30f10450803db0f2f04d9734c0fb75e068d0c133bc87c022bc88b451003c30fb75e043bc37c022bc38b1ec1e104d9041903cbc1e004d91c1803c3d94104895510d9580442d94108d95808d9410cd9580c0fb74f103bd17c950fb74e060fb74604034d103bc87c022bc80fb6471df30f1045088d14408b450cc1e104030e03d203d252f30f11015083c10451e89d41430083c40c5f5e5b5dc20c00";
    inline constexpr unsigned kWriterRelocs[]={34};
}
