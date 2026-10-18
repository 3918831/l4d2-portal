#include "../src/Portal/PortalFallWritePolicy.h"
#include "../src/Portal/PortalFallWriteFingerprint.h"
#include "../src/Util/Logger/PortalFileLog.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
#include <string>

int main()
{
    namespace P=PortalFallWritePolicy;
    // A fresh production configuration must repair the next committed flight,
    // even when expensive evidence capture is disabled.
    P::Runtime runtime;
    runtime.lifetime.Arm(0x1000,1,10);runtime.ready=true;
    float baselineField=680;
    U::PortalFileLog::SetNativeDiagnosticsEnabled(false);
    auto baseline=runtime.Apply(true,baselineField,40.f,[]{return true;},
        [&](float v){baselineField=v;return true;},[&](float v){return baselineField==v;});
    assert(baseline.readback&&baselineField==-40.f);
    runtime.reason="ready";runtime.Reset("native-ground");
    assert(!runtime.lifetime.active&&!runtime.ready&&std::strcmp(runtime.reason,"native-ground")==0);
    runtime.enabled=false;runtime.Reset("ab-toggle");
    assert(!runtime.enabled); // reset must not undo an explicit rollback
    runtime.lifetime.Arm(0x1000,2,11);runtime.lifetime.Arm(0x1000,3,12);
    assert(runtime.lifetime.epoch==3&&!runtime.lifetime.Continue(0x1000,11,true,true,2,false));
    assert(runtime.lifetime.Continue(0x1000,12,true,true,2,false));
    runtime.ready=true;baselineField=680;
    baseline=runtime.Apply(true,baselineField,40.f,[]{return true;},
        [&](float v){baselineField=v;return true;},[&](float v){return baselineField==v;});
    assert(!baseline.written&&baselineField==680);
    runtime.enabled=true;
    baseline=runtime.Apply(true,baselineField,40.f,[]{return false;},
        [&](float v){baselineField=v;return true;},[&](float v){return baselineField==v;});
    assert(!baseline.written&&baselineField==680); // native notification refusal
    P::Lifetime scope;scope.Arm(0x1000,4,500);
    assert(scope.Continue(0x1000,501,true,true,2,false));
    assert(!scope.Continue(0x1000,499,true,true,2,false)&&scope.active); // not an old-command write
    assert(scope.Continue(0x1000,90000,true,true,2,false)); // no arbitrary timeout
    assert(!scope.Continue(0x1000,90001,true,true,2,true)&&!scope.active);
    for(int reason=0;reason<4;++reason){scope.Arm(0x1000,4,500);assert(!scope.Continue(reason==0?0x2000:0x1000,501,reason!=1,reason!=2,reason==3?8:2,false));assert(!scope.active);}
    float field=680;std::vector<int> order;
    auto notify=[&]{assert(field==680);order.push_back(1);return true;};
    auto store=[&](float value){order.push_back(2);field=value;return true;};
    auto read=[&](float value){order.push_back(3);return field==value;};
    auto r=P::Apply(true,true,field,48.76618f,notify,store,read);
    assert(r.candidate&&r.notified&&r.written&&r.readback&&field==-48.76618f);
    assert((order==std::vector<int>{1,2,3})); // missing/reordered notification must fail
    order.clear();r=P::Apply(true,true,field,48.76618f,notify,store,read);
    assert(r.candidate&&!r.changed&&!r.notified&&!r.written&&order.empty());
    field=680;
    r=P::Apply(false,true,field,-43.73123f,notify,store,read);
    assert(r.candidate&&!r.written&&field==680&&order.empty());
    r=P::Apply(true,false,field,-43.73123f,notify,store,read);
    assert(!r.candidate&&!r.written&&order.empty());
    r=P::Apply(true,true,field,-43.73123f,[]{return false;},store,read);
    assert(r.candidate&&!r.notified&&!r.written&&field==680&&order.empty());
    for(float bad:{NAN,INFINITY,4097.f}){r=P::Apply(true,true,field,bad,notify,store,read);assert(!r.candidate&&!r.written&&order.empty());}
    r=P::Apply(true,true,field,-43.73123f,notify,store,read);
    assert(r.readback&&field==43.73123f);
    // Independent disk fixture, server SHA 278A48DB...1D3DD. A changed opcode,
    // calling convention or unadjusted relocation must refuse write capability.
    std::ifstream fixture("tests/fixtures/portal-fall-write-x86.hex");assert(fixture);
    std::string hex;unsigned which=0;
    while(std::getline(fixture,hex))
    {
        if(!hex.empty()&&hex.back()=='\r')hex.pop_back();
        std::vector<unsigned char> bytes;
        for(unsigned i=0;i<hex.size();i+=2)bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));
        auto valid=[&](std::uintptr_t base){using namespace PortalFallWriteFingerprint;
            switch(which){case 0:return Update(bytes.data(),bytes.size(),base);case 1:return Ground(bytes.data(),bytes.size(),base);case 2:return Notify(bytes.data(),bytes.size());default:return Accessor(bytes.data(),bytes.size(),base);}};
        assert(valid(0x10000000));
        for(unsigned i=0;i<bytes.size();++i){bytes[i]^=1;assert(!valid(0x10000000));bytes[i]^=1;}
        if(which!=2)
        {
            assert(!valid(0x30000000));
            const std::vector<unsigned> relocs=which==0?std::vector<unsigned>{36,68,93}:which==1?std::vector<unsigned>{13}:std::vector<unsigned>{3};
            for(auto at:relocs){std::uint32_t v;std::memcpy(&v,bytes.data()+at,4);v+=0x20000000;std::memcpy(bytes.data()+at,&v,4);}
            assert(valid(0x30000000));
        }
        bytes.pop_back();assert(!valid(which==2?0x10000000:0x30000000));++which;
    }
    assert(which==4);
    std::ifstream layoutFixture("tests/fixtures/portal-fall-write-x86.hex");
    std::vector<std::vector<unsigned char>> layout;
    for(unsigned j=0;j<2;++j){std::getline(layoutFixture,hex);std::vector<unsigned char> b;for(unsigned i=0;i+1<hex.size();i+=2)b.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));layout.push_back(b);}
    using PortalFallWriteFingerprint::Fields;
    assert(Fields(layout[0].data(),layout[1].data(),0x1aa8,0x1a44,0x172,0x244));
    assert(!Fields(layout[0].data(),layout[1].data(),0x11fc,0x1a44,0x172,0x244));
    assert(!Fields(layout[0].data(),layout[1].data(),0x1aa8,0x1a40,0x172,0x244));
    assert(!Fields(layout[0].data(),layout[1].data(),0x1aa8,0x1a44,0x144,0x244));
    assert(!Fields(layout[0].data(),layout[1].data(),0x1aa8,0x1a44,0x172,0x240));
    auto scan=layout[0];scan.insert(scan.begin(),17,0xcc);
    auto sites=PortalFallWriteFingerprint::FindUpdates(scan.data(),scan.size(),0x10000000);
    assert(sites.count==1&&sites.offset==17);
    scan.insert(scan.end(),layout[0].begin(),layout[0].end());
    assert(PortalFallWriteFingerprint::FindUpdates(scan.data(),scan.size(),0x10000000).count==2);
    scan[17+99]^=1;scan[17+135+99]^=1;
    assert(PortalFallWriteFingerprint::FindUpdates(scan.data(),scan.size(),0x10000000).count==0);
    std::cout<<"PortalFallWrite tests passed\n";
}
