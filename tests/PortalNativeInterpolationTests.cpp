#include "../src/Portal/PortalNativeInterpolationPolicy.h"
#include "../src/Portal/PortalNativePart1Policy.h"
#include "../src/Portal/PortalNativeOriginPolicy.h"
#include "../src/Portal/PortalNativeOriginFingerprint.h"
#include "../src/Portal/PortalNativeResetPolicy.h"
#include "../src/Portal/PortalNativePredictionErrorPolicy.h"
#include "../src/Portal/PortalNativePredictionErrorFingerprint.h"
#include "fixtures/PortalNativePredictionErrorBodies.h"
#include "../src/Portal/PortalNativeFieldPolicy.h"
#include "../src/Portal/PortalNativeLoggingFingerprint.h"
#include "fixtures/PortalNativeLoggingBodies.h"
#include "../src/Portal/PortalNativeFallPolicy.h"
#include <cassert>
#include <array>
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
using namespace PortalNativeInterpolationPolicy;
int main()
{
    namespace Fall=PortalNativeFallPolicy;
    // Command-final entity velocity must come from RunCommand, not the
    // ProcessMovement boundary before FinishMove writes MoveData back.
    assert(Fall::ClientCommandPhase("runcommand-pre-original")==2);
    assert(Fall::ClientCommandPhase("runcommand-post-original")==3);
    assert(Fall::ClientCommandPhase("processmovement-post-original")==UINT32_MAX);
    assert(Fall::ClientCommandPhase("setupmove-post-original")==UINT32_MAX);
    assert(Fall::ClientCommandPhase(nullptr)==UINT32_MAX);
    // Real x86 movement prefix decoder: no reads for missing identity/range,
    // no foreign-player or missing-mv fallback, no entity or movement writes.
    std::array<std::uint32_t,3> movementPrefix{0x1000,0x2000,0x3000};
    unsigned movementReads=0;
    auto movementRead=[&](std::uint32_t at,void* out,std::size_t size)
    {++movementReads;if(at!=0x100||size!=12)return false;std::memcpy(out,movementPrefix.data(),size);return true;};
    auto context=Fall::ReadMovementContext(0x100,0x2000,movementRead);
    assert(context.valid&&context.player==0x2000&&context.move==0x3000&&movementReads==1);
    assert(!Fall::ReadMovementContext(0x100,0x4000,movementRead).valid);
    movementPrefix[2]=0;assert(!Fall::ReadMovementContext(0x100,0x2000,movementRead).valid);
    movementPrefix[2]=0x3000;
    movementReads=0;
    assert(!Fall::ReadMovementContext(0,0x2000,movementRead).valid);
    assert(!Fall::ReadMovementContext(0x100,0,movementRead).valid);
    assert(!Fall::ReadMovementContext(UINT32_MAX-4,0x2000,movementRead).valid);
    assert(movementReads==0);
    assert(!Fall::ReadMovementContext(0x200,0x2000,movementRead).valid);
    assert((movementPrefix==std::array<std::uint32_t,3>{0x1000,0x2000,0x3000}));
    Fall::Window movementWindow;Fall::Sample movementSample;
    movementSample.fallValid=movementSample.movementMatched=movementSample.moveZValid=true;
    movementSample.fall=706.6665f;movementSample.moveZ=-.900354f;
    for(unsigned i=0;i<60;++i)movementWindow.Add(movementSample);
    assert(movementWindow.calls==60&&movementWindow.movementMatched==60&&movementWindow.moveZValid==60);
    assert(movementWindow.emitted==48&&movementWindow.dropped==12);
    movementSample.moveZ=NAN;movementWindow.Add(movementSample);
    movementWindow.Add(Fall::Sample{});
    assert(movementWindow.calls==62&&movementWindow.movementMatched==61&&movementWindow.moveZValid==60);
    assert(movementWindow.unavailable==1&&movementWindow.suppressedUnavailable==1);
    // A ordinary short metadata name is one bounded block read, not a
    // VirtualQuery-backed read for every character. The seam is real Text().
    std::array<char,80> textMemory{};std::strcpy(textMemory.data(),"CBasePlayer");
    unsigned textReads=0;
    auto textRead=[&](std::uint32_t at,void* out,std::size_t size)
    {++textReads;if(at<100||at-100>textMemory.size()||size>textMemory.size()-(at-100))return false;std::memcpy(out,textMemory.data()+at-100,size);return true;};
    char decodedText[64]={};assert(Fall::Text(100,decodedText,textRead));
    assert(!std::strcmp(decodedText,"CBasePlayer")&&textReads==1);
    // NUL at the end of a readable region must not require the following page.
    auto edgeRead=[&](std::uint32_t at,void* out,std::size_t size)
    {if(at<100||at-100>4||size>4-(at-100))return false;std::memcpy(out,"abc"+at-100,size);return true;};
    assert(Fall::Text(100,decodedText,edgeRead)&&!std::strcmp(decodedText,"abc"));
    textMemory.fill('a');textReads=0;assert(!Fall::Text(100,decodedText,textRead)&&textReads==4);
    textMemory.fill(0);assert(!Fall::Text(100,decodedText,textRead));
    textMemory[0]='a';textMemory[1]='\x01';assert(!Fall::Text(100,decodedText,textRead));
    assert(!Fall::Text(UINT32_MAX-32,decodedText,textRead));
    // A shortened first block must not shift subsequent reads/writes past64.
    textMemory.fill('a');struct GuardedText{char text[64];char guard[16];} guardedText{};
    std::memset(guardedText.guard,'Z',16);
    auto shiftedRead=[&](std::uint32_t at,void* out,std::size_t size)
    {return !(at==100&&size>4)&&textRead(at,out,size);};
    assert(!Fall::Text(100,guardedText.text,shiftedRead));
    for(char c:guardedText.guard)assert(c=='Z');
    Fall::TextStatus textStatus;
    assert(!Fall::Text(100,decodedText,textRead,&textStatus));
    assert(!std::strcmp(textStatus.issue,"unterminated")&&textStatus.inspected==64);
    assert(!Fall::Text(100,decodedText,[](std::uint32_t,void*,std::size_t){return false;},&textStatus));
    assert(!std::strcmp(textStatus.issue,"read-rejected"));
    textMemory.fill(0);assert(!Fall::Text(100,decodedText,textRead,&textStatus));
    assert(!std::strcmp(textStatus.issue,"empty"));
    textMemory[0]='\x01';assert(!Fall::Text(100,decodedText,textRead,&textStatus));
    assert(!std::strcmp(textStatus.issue,"non-ascii"));
    // Binding identity, not the capture number, controls discovery. This
    // applies equally to a successful and a failed previous binding.
    Fall::Identity identity;identity.entity=0x100;identity.module=0x10000000;identity.table=0x200;
    identity.root=0x300;identity.header={0x400,12,0x500,0x600};
    Fall::Cache cache;assert(cache.NeedsBind(identity));cache.Remember(identity);
    for(unsigned capture=1;capture<18;++capture)assert(!cache.NeedsBind(identity));
    auto changedIdentity=identity;changedIdentity.entity+=4;assert(cache.NeedsBind(changedIdentity));
    changedIdentity=identity;changedIdentity.module+=0x10000;assert(cache.NeedsBind(changedIdentity));
    changedIdentity=identity;changedIdentity.table+=4;assert(cache.NeedsBind(changedIdentity));
    changedIdentity=identity;changedIdentity.root+=4;assert(cache.NeedsBind(changedIdentity));
    changedIdentity=identity;changedIdentity.header[0]+=4;assert(cache.NeedsBind(changedIdentity));
    cache.Reset();assert(cache.NeedsBind(identity));
    Fall::Region region{0x100,0x200};assert(region.Covers(0x110,16));
    assert(!region.Covers(0x1ff,2)&&!region.Covers(0x80,1)&&!region.Covers(0x110,0));
    assert(!region.Covers(UINTPTR_MAX-1,4));
    unsigned char getter[]={0xb8,0x00,0x20,0x00,0x10,0xc3};
    assert(Fall::MapFromGetter(getter,6,0x10000000,0x10010000)==0x10002000);
    getter[5]=0xc2;assert(!Fall::MapFromGetter(getter,6,0x10000000,0x10010000));getter[5]=0xc3;
    assert(!Fall::MapFromGetter(getter,5,0x10000000,0x10010000));
    assert(!Fall::MapFromGetter(getter,6,0x10003000,0x10010000));
    // Public RecvTable lookup: bad/cyclic/duplicate branches cannot be hidden
    // by a valid match in a later branch. The complete traversal is the seam.
    std::array<Fall::RecvProperty,4> recvProps{};
    std::strcpy(recvProps[0].name,"m_Local");recvProps[0].offset=0x100;recvProps[0].child=2;recvProps[0].type=6;
    std::strcpy(recvProps[1].name,"m_flFallVelocity");recvProps[1].offset=0x64;recvProps[1].type=1;
    std::strcpy(recvProps[2].name,"other");recvProps[2].type=0;
    auto recvCount=[](std::uint32_t table,unsigned& count){if(table==1){count=2;return true;}if(table==2){count=1;return true;}return false;};
    auto recvEntry=[&](std::uint32_t table,unsigned i,Fall::RecvProperty& p){if(table==1&&i<2){p=recvProps[i?2:0];return true;}if(table==2&&!i){p=recvProps[1];return true;}return false;};
    auto recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.valid&&recv.offset==0x164&&!recv.failed);
    recvProps[0].child=3;recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.failed&&!recv.valid);recvProps[0].child=2;
    recvProps[2]=recvProps[1];recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.failed&&!recv.valid);
    std::strcpy(recvProps[2].name,"other");recvProps[2].type=0;
    recvProps[0].child=1;recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.failed&&!recv.valid);recvProps[0].child=2;
    recvProps[1].type=0;recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.failed&&!recv.valid);recvProps[1].type=1;
    recvProps[0].offset=65535;recv=Fall::FindRecvFloat(1,recvCount,recvEntry);assert(recv.failed&&!recv.valid);
    // Native-format byte fixture, not a cast to this repo's old datamap structs.
    std::array<unsigned char,4096> fallMemory{};
    auto put=[&](unsigned at,std::uint32_t value){std::memcpy(fallMemory.data()+at,&value,4);};
    auto name=[&](unsigned at,const char* value){std::strcpy(reinterpret_cast<char*>(fallMemory.data()+at),value);};
    auto read=[&](std::uint32_t at,void* out,std::size_t size){if(at>fallMemory.size()||size>fallMemory.size()-at)return false;std::memcpy(out,fallMemory.data()+at,size);return true;};
    put(100,300);put(104,1);put(108,800);put(112,200);name(800,"CTerrorPlayer");
    put(200,360);put(204,2);put(208,820);name(820,"CBasePlayer");
    put(300,5);put(304,860);name(860,"other"); // root irrelevant field
    put(360,10);put(364,880);name(880,"m_Local");put(368,0x600);put(372,0x00020001);put(388,250);put(392,464);
    put(420,3);put(424,900);name(900,"m_vecVelocity");put(428,0x100);put(432,0x00020001);put(452,12);
    put(250,480);put(254,1);put(258,840);name(840,"CPlayerLocalData");
    put(480,1);put(484,920);name(920,"m_flFallVelocity");put(488,0x64);put(492,0x00020001);put(512,4);
    const auto field=Fall::BindFall(100,read,read);
    assert(field.valid&&field.offset==0x664&&field.localOffset==0x600&&field.descriptor==480&&field.localDescriptor==360);
    Fall::Diagnostic bindingDiagnostic;
    assert(Fall::BindFall(100,read,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.stage,"bound")&&!std::strcmp(bindingDiagnostic.issue,"none"));
    // Test24: the first FIELD_VOID/FTYPEDESC_FUNCTIONTABLE entry has a
    // generated heap name. Module ownership applies to structures, not text.
    auto imageRead=[&](std::uint32_t at,void* out,std::size_t size)
    {return at>=100&&at<1024&&size<=1024-at&&read(at,out,size);};
    put(300,0);put(304,3000);put(312,0x00200001);name(3000,"CTerrorPlayerGeneratedFunction");
    const auto dynamicNameBinding=Fall::BindFall(100,imageRead,read,&bindingDiagnostic);
    assert(dynamicNameBinding.valid&&dynamicNameBinding.offset==0x664);
    // Every text path, including class and embedded field names, is separate.
    name(3100,"CTerrorPlayer");name(3200,"m_Local");name(3300,"CPlayerLocalData");name(3400,"m_flFallVelocity");
    put(108,3100);put(364,3200);put(258,3300);put(484,3400);
    assert(Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    // Readable external structures must still fail; accepting arbitrary
    // memory for names does not authorize external datamaps/descriptors.
    std::memcpy(fallMemory.data()+3500,fallMemory.data()+300,60);put(100,3500);
    assert(!Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.issue,"descriptor-read"));put(100,300);
    std::memcpy(fallMemory.data()+3500,fallMemory.data()+200,16);put(112,3500);
    assert(!Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.issue,"map-read"));put(112,200);
    put(304,4096);assert(!Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.text.issue,"read-rejected"));put(304,3000);
    fallMemory[3000]=1;assert(!Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.text.issue,"non-ascii"));
    std::memset(fallMemory.data()+3000,'a',64);
    assert(!Fall::BindFall(100,imageRead,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.text.issue,"unterminated"));
    put(108,800);put(364,880);put(258,840);put(484,920);
    put(300,5);put(304,860);put(312,0);
    put(492,0x00420001);assert(!Fall::BindFall(100,read,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.stage,"fall-validation")&&!std::strcmp(bindingDiagnostic.issue,"pointer-field"));put(492,0x00020001);
    // Failed reads report the exact map/descriptor/index, not just false.
    auto deniedNameRead=[&](std::uint32_t at,void* out,std::size_t size){return at!=860&&read(at,out,size);};
    assert(!Fall::BindFall(100,deniedNameRead,deniedNameRead,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.stage,"local-search")&&!std::strcmp(bindingDiagnostic.issue,"field-text"));
    assert(bindingDiagnostic.map==100&&bindingDiagnostic.descriptor==300&&bindingDiagnostic.index==0);
    assert(bindingDiagnostic.type==5&&bindingDiagnostic.offset==0); // failing descriptor, not the last valid match
    auto deniedBaseRead=[&](std::uint32_t at,void* out,std::size_t size){return at!=200&&read(at,out,size);};
    assert(!Fall::BindFall(100,deniedBaseRead,deniedBaseRead,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.issue,"map-read")&&bindingDiagnostic.map==200);
    assert(!bindingDiagnostic.owner[0]&&bindingDiagnostic.descriptor==0&&bindingDiagnostic.type==0);
    auto deniedEmbeddedName=[&](std::uint32_t at,void* out,std::size_t size){return at!=840&&read(at,out,size);};
    assert(!Fall::BindFall(100,deniedEmbeddedName,deniedEmbeddedName,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.stage,"embedded-class")&&bindingDiagnostic.map==250);
    assert(!bindingDiagnostic.owner[0]&&bindingDiagnostic.descriptor==0);
    put(204,4097);assert(!Fall::BindFall(100,read,read,&bindingDiagnostic).valid);
    assert(!std::strcmp(bindingDiagnostic.issue,"map-count")&&bindingDiagnostic.map==200);put(204,2);
    auto vector=Fall::Find(100,"m_vecVelocity",read,read);assert(vector.valid&&vector.offset==0x100&&vector.type==3);
    put(492,0x00420001);assert(!Fall::BindFall(100,read,read).valid);put(492,0x00020001); // pointer fields cannot be summed
    put(512,12);assert(!Fall::BindFall(100,read,read).valid);put(512,4); // wrong scalar metadata
    put(388,100);assert(!Fall::BindFall(100,read,read).valid);put(388,250); // embedded cycle/wrong class
    put(112,100);assert(!Fall::Find(100,"missing",read,read).valid);put(112,200); // bounded base cycle
    put(204,4097);assert(!Fall::BindFall(100,read,read).valid);put(204,2);
    Fall::Sample sample;sample.fallValid=true;sample.fall=300;sample.zValid=true;sample.z=-300;
    sample.groundValid=true;sample.ground=UINT32_MAX;
    Fall::Window fallWindow;for(unsigned i=0;i<400;++i)fallWindow.Add(sample);
    assert(fallWindow.calls==400&&fallWindow.valid==400&&fallWindow.groundNull==400&&fallWindow.relationValid==400&&fallWindow.mismatch==0);
    assert(fallWindow.emitted==48&&fallWindow.dropped==352);
    sample.z=-13.333334f;fallWindow.Add(sample);assert(fallWindow.mismatch==1);
    sample.fallValid=false;fallWindow.Add(sample);assert(fallWindow.unavailable==1&&fallWindow.mismatch==1);
    sample.fallValid=true;sample.fall=NAN;fallWindow.Add(sample);assert(fallWindow.unavailable==2);
    Fall::Window unknownWindow;
    for(unsigned i=0;i<400;++i)assert(!unknownWindow.Add(Fall::Sample{}));
    assert(unknownWindow.calls==400&&unknownWindow.unavailable==400&&unknownWindow.groundUnknown==400);
    assert(unknownWindow.emitted==0&&unknownWindow.suppressedUnavailable==400&&unknownWindow.dropped==0);
    namespace E=PortalNativePredictionErrorPolicy;
    E::Observation err;err.localPlayer=0x1000;err.entity=0x1000;err.frame=100;err.tick=10;err.ack=1;err.packet=1;
    err.contextValid=err.callerMatched=true;err.comparisonCalls=1;err.countKnown=true;err.errors=2;err.hadErrors=true;err.time=10;
    assert(E::Classify(err)==E::Outcome::Errors);
    auto badTime=err;badTime.time=NAN;assert(E::Classify(badTime)==E::Outcome::Invalid);
    auto missing=err;missing.comparisonCalls=0;missing.countKnown=false;missing.hadErrors=false;
    assert(E::Classify(missing)==E::Outcome::Invalid); // unknown is not zero errors
    missing=err;missing.comparisonCalls=2;assert(E::Classify(missing)==E::Outcome::Invalid);
    missing=err;missing.errors=-1;assert(E::Classify(missing)==E::Outcome::Invalid);
    missing=err;missing.hadErrors=false;assert(E::Classify(missing)==E::Outcome::Invalid);
    missing=err;missing.ack=0;missing.comparisonCalls=0;missing.countKnown=false;missing.hadErrors=false;
    assert(E::Classify(missing)==E::Outcome::NotChecked);
    missing.hadErrors=true;assert(E::Classify(missing)==E::Outcome::Invalid);
    auto clean=err;clean.errors=0;clean.hadErrors=false;assert(E::Classify(clean)==E::Outcome::NoErrors);
    E::PacketScope packet{0x4000,0x1000,1,1,true,true};
    packet.epoch=err.epoch=7;
    assert(E::InLifecycle(packet,7)&&E::InLifecycle(err,7));
    assert(!E::InLifecycle(packet,8)&&!E::InLifecycle(err,8)); // original returns after reset
    assert(!E::InLifecycle(packet,0));
    assert(E::NeedsLifecycleReset(false,0,0,true)); // disabled with retained ring but no capture
    assert(E::NeedsLifecycleReset(true,0x1000,0,true));
    assert(E::NeedsLifecycleReset(true,0x1000,0x2000,false));
    assert(!E::NeedsLifecycleReset(true,0x1000,0x1000,true));
    E::Prelude errorPrelude;auto invalidPrelude=err;invalidPrelude.countKnown=false;
    err.sequence=1;errorPrelude.Add(err);invalidPrelude.sequence=2;errorPrelude.Add(invalidPrelude);
    for(unsigned i=3;i<=20;++i){clean.sequence=i;errorPrelude.Add(clean);}
    assert(errorPrelude.count==8&&errorPrelude.rows[0].sequence==1&&errorPrelude.rows[1].sequence==2);
    assert(errorPrelude.rows[2].sequence==15&&errorPrelude.rows[7].sequence==20);
    for(unsigned i=21;i<=30;++i){err.sequence=i;errorPrelude.Add(err);}
    assert(errorPrelude.count==8&&errorPrelude.rows[0].sequence==23&&errorPrelude.rows[7].sequence==30);
    assert(E::Eligible(packet,0x4000,0x1000,0x1234,0x1234));
    assert(!E::Eligible(packet,0x5000,0x1000,0x1234,0x1234));
    assert(!E::Eligible(packet,0x4000,0x2000,0x1234,0x1234));
    assert(!E::Eligible(packet,0x4000,0x1000,0x1235,0x1234));
    err.active=true;err.capture=21;
    assert(E::InCapture(err,true,21));assert(!E::InCapture(err,true,22));assert(!E::InCapture(err,false,21));
    assert(E::PreludeMatches(err,0x1000,104,14));assert(!E::PreludeMatches(err,0x1000,105,15));
    assert(!E::PreludeMatches(err,0x1000,99,9));assert(!E::PreludeMatches(err,0x2000,104,14));
    E::Window errorWindow;
    err.nativeMessages=2;err.nativeFields=1;
    for(unsigned i=0;i<400;++i){err.tick=10+i;err.packet=1+i;errorWindow.Add(err);}
    assert(errorWindow.calls==400&&errorWindow.outcomes[3]==400&&errorWindow.errorSum==800&&errorWindow.localErrors==400);
    assert(errorWindow.emitted==48&&errorWindow.dropped==352&&errorWindow.entities[0].calls==400);
    assert(errorWindow.nativeMessages==800&&errorWindow.nativeFields==400&&errorWindow.positiveWithoutNativeField==0);
    auto silentError=err;silentError.nativeMessages=silentError.nativeFields=0;errorWindow.Add(silentError);
    assert(errorWindow.positiveWithoutNativeField==1);
    errorWindow.Add(missing);assert(errorWindow.calls==402&&errorWindow.outcomes[0]==1);
    E::Window invalidIdentity;auto nullEntity=err;nullEntity.entity=nullEntity.localPlayer=0;invalidIdentity.Add(nullEntity);
    assert(invalidIdentity.localCalls==0&&invalidIdentity.localErrors==0);
    E::Window manyEntities;for(unsigned i=0;i<18;++i){err.entity=0x2000+i*0x100;manyEntities.Add(err);}
    assert(manyEntities.calls==18&&manyEntities.entityOverflow==2&&manyEntities.localErrors==0);
    namespace EF=PortalNativePredictionErrorFingerprint;
    auto errorFixture=[](const char* hex){std::vector<unsigned char> out(std::strlen(hex)/2);for(std::size_t i=0;i<out.size();++i){auto d=[](char c){return c<='9'?c-'0':c-'a'+10;};out[i]=static_cast<unsigned char>((d(hex[i*2])<<4)|d(hex[i*2+1]));}return out;};
    auto network=errorFixture(PortalPredictionErrorTestBodies::Network),entityPost=errorFixture(PortalPredictionErrorTestBodies::Entity);
    auto transfer=errorFixture(PortalPredictionErrorTestBodies::Transfer),transferTable=errorFixture(PortalPredictionErrorTestBodies::TransferTable);
    assert(EF::EntityTarget(network.data(),597,0x1017bb40,0x10000000)==0x1004d5f0);
    assert(EF::EntityTarget(network.data(),596,0x1017bb40,0x10000000)==0);
    assert(EF::Relative(network.data(),UINT32_MAX,185,0x1017bb40)==0);
    assert(EF::TransferTarget(entityPost.data(),265,0x1004d5f0,0x10000000)==0x10180370);
    assert(EF::ValidTransfer(transfer.data(),150,transferTable.data(),16,0x10000000));
    namespace LogFP=PortalNativeLoggingFingerprint;
    auto exportLog=errorFixture(PortalNativeLoggingTestBodies::Export),coreLog=errorFixture(PortalNativeLoggingTestBodies::Core);
    assert(LogFP::Target(exportLog.data(),73,0x10006f90,0x10000000)==0x10006d30);
    assert(LogFP::Object(exportLog.data(),73,0x10000000)==0x1003cf08);
    assert(!LogFP::Target(exportLog.data(),72,0x10006f90,0x10000000));
    assert(LogFP::Core(coreLog.data(),383,0x10000000));
    coreLog[381]=8;assert(!LogFP::Core(coreLog.data(),383,0x10000000));coreLog[381]=16;
    for(auto at:{10,18,23,33,62}){std::uint32_t a=0;std::memcpy(&a,exportLog.data()+at,4);a+=0x20000000;std::memcpy(exportLog.data()+at,&a,4);}
    assert(LogFP::Target(exportLog.data(),73,0x30006f90,0x30000000)==0x30006d30);
    assert(LogFP::Object(exportLog.data(),73,0x30000000)==0x3003cf08);
    assert(!LogFP::Target(exportLog.data(),73,0x30006f90,0x10000000));
    exportLog[66]=0xe9;assert(!LogFP::Target(exportLog.data(),73,0x30006f90,0x30000000));
    namespace F=PortalNativeFieldPolicy;
    const char message[]=" 1 (1)C_TerrorPlayer::m_vecVelocity - net 800.0 predicted 799.5 delta 0.5\n";
    auto nativeField=F::Parse(message,sizeof(message),1);
    assert(nativeField.valid&&nativeField.ordinal==1&&nativeField.index==1);
    assert(!std::strcmp(nativeField.className,"C_TerrorPlayer")&&!std::strcmp(nativeField.fieldName,"m_vecVelocity"));
    assert(!std::strcmp(nativeField.value,"net 800.0 predicted 799.5 delta 0.5"));
    assert(!F::Parse(message,sizeof(message),2).valid); // another entity cannot inherit a field
    assert(!F::Parse(message,sizeof(message)-1,1).valid); // no terminating NUL
    assert(!F::Parse("not a native report",20,1).valid);
    const char zeroOrdinal[]=" 0 (1)C_BasePlayer::x - 0";
    assert(!F::Parse(zeroOrdinal,sizeof(zeroOrdinal),1).valid);
    const char chain[]=" 2 (1)m_Local.C_BasePlayer::m_flFallVelocity - float differs\r\n";
    assert(F::Parse(chain,sizeof(chain),1).valid);
    std::string longField=" 1 (1)C_BasePlayer::"+std::string(96,'x')+" - 1";
    assert(!F::Parse(longField.c_str(),longField.size()+1,1).valid);
    assert(F::Eligible(err,true,7,err.localPlayer));assert(!F::Eligible(err,false,7,err.localPlayer));
    assert(!F::Eligible(err,true,8,err.localPlayer));assert(!F::Eligible(err,true,7,err.localPlayer+4));
    bool logging=false;unsigned observedMessages=0,originalCalls=0;
    F::ForwardLog(logging,[&]{++observedMessages;},[&]{
        ++originalCalls;
        return F::ForwardLog(logging,[&]{++observedMessages;},[&]{++originalCalls;return 37;});
    });
    assert(observedMessages==1&&originalCalls==2&&!logging); // listener reentry must not become fresh evidence
    const int nativeResponse=F::ForwardLog(logging,[&]{++observedMessages;},[&]{++originalCalls;return 19;});
    assert(nativeResponse==19&&observedMessages==2&&originalCalls==3&&!logging);
    F::Window fieldWindow;
    for(unsigned i=0;i<400;++i) fieldWindow.Add(nativeField);
    assert(fieldWindow.messages==400&&fieldWindow.parsed==400&&fieldWindow.emitted==48&&fieldWindow.dropped==352);
    assert(fieldWindow.fields[0].occurrences==400&&fieldWindow.overflow==0);
    auto otherEntityField=nativeField;otherEntityField.index=2;fieldWindow.Add(otherEntityField);
    assert(fieldWindow.fields[0].occurrences==400&&fieldWindow.fields[1].occurrences==1);
    fieldWindow.Add(F::Message{});assert(fieldWindow.messages==402&&fieldWindow.unknown==1&&fieldWindow.parsed==401);
    F::Window overflowingFields;
    for(unsigned i=0;i<18;++i){auto m=nativeField;std::snprintf(m.fieldName,sizeof(m.fieldName),"field%u",i);overflowingFields.Add(m);}
    assert(overflowingFields.messages==18&&overflowingFields.parsed==18&&overflowingFields.overflow==2);
    assert(!EF::ValidTransfer(transfer.data(),150,transferTable.data(),15,0x10000000));
    transferTable[4]^=1;assert(!EF::ValidTransfer(transfer.data(),150,transferTable.data(),16,0x10000000));transferTable[4]^=1;
    for(auto at:{13,41,375,403,434,495,545}){std::uint32_t a=0;std::memcpy(&a,network.data()+at,4);a+=0x10000000;std::memcpy(network.data()+at,&a,4);}
    assert(EF::EntityTarget(network.data(),597,0x2017bb40,0x20000000)==0x2004d5f0);
    assert(EF::EntityTarget(network.data(),597,0x2017bb40,0x10000000)==0);
    network[185]=0xe9;assert(EF::EntityTarget(network.data(),597,0x2017bb40,0x20000000)==0);
    entityPost[263]=8;assert(EF::TransferTarget(entityPost.data(),265,0x1004d5f0,0x10000000)==0);
    namespace P = PortalNativePart1Policy;
    namespace O = PortalNativeOriginPolicy;
    namespace R = PortalNativeResetPolicy;
    R::Snapshot resetSnapshot;resetSnapshot.valid=true;resetSnapshot.ring={0x2000,16,13,3};resetSnapshot.value={1,2,3};
    for(int i=0;i<3;++i) resetSnapshot.samples[i]={i,10,{1,2,3},true};
    assert(R::ThreeClones(resetSnapshot));
    auto malformedReset=resetSnapshot;malformedReset.samples[1].time=9;
    assert(!R::ThreeClones(malformedReset));
    malformedReset=resetSnapshot;malformedReset.samples[1].position.x=NAN;
    assert(!R::ThreeClones(malformedReset));
    malformedReset=resetSnapshot;malformedReset.samples[2].valid=false;
    assert(!R::ThreeClones(malformedReset));
    malformedReset=resetSnapshot;malformedReset.samples[1].position.x=2;
    assert(!R::ThreeClones(malformedReset));
    auto tinyDifference=resetSnapshot;tinyDifference.value.x=0;
    for(auto& s:tinyDifference.samples)s.position.x=0;
    tinyDifference.samples[1].position.x=1e-30f;
    assert(!R::ThreeClones(tinyDifference)); // squared-distance underflow is not equality
    auto priorReset=resetSnapshot;priorReset.ring.count=6;priorReset.samples[1].time=9.966666f;
    assert(R::FirstInterval(priorReset));
    R::Observation reset;reset.player=0x1000;reset.watcher=0x12f8;reset.module=0x10000000;reset.callerRva=0x12345;
    reset.frame=100;reset.tick=10;reset.command=-1;reset.phase=R::Phase::Unscoped; // outside RunCommand/render is still observed
    reset.nativeTime=reset.nativeAfter=reset.sdkTime=10;reset.simulation=10;reset.before=priorReset;reset.after=resetSnapshot;
    reset.contextValid=reset.identity=reset.callerValid=reset.clockValid=true;reset.capture=19;reset.active=true;
    assert(R::Valid(reset));
    // Parent association must not let a nested/different player or wrong return site inherit a caller.
    assert(R::ParentState(reset)==R::ParentKind::Unavailable);
    auto parentReset=reset;parentReset.parentInstalled=true;
    assert(R::ParentState(parentReset)==R::ParentKind::Unscoped);
    parentReset.parent={0x1000,0x10000000,0x123456,"client.dll",true,true,true};parentReset.parentReturnMatched=true;
    assert(R::ParentState(parentReset)==R::ParentKind::Matched);
    R::Window parentWindow;parentWindow.Add(parentReset);
    parentReset.parent.callerRva=0x123457;assert(parentWindow.Add(parentReset));
    assert(parentWindow.parentStates[4]==2&&parentWindow.parentCallers[0].calls==1&&parentWindow.parentCallers[1].calls==1);
    parentReset.parent.player=0x2000;assert(R::ParentState(parentReset)==R::ParentKind::Mismatch);parentWindow.Add(parentReset);
    parentReset.parent.player=0x1000;parentReset.parentReturnMatched=false;
    assert(R::ParentState(parentReset)==R::ParentKind::Mismatch);
    parentReset.parentReturnMatched=true;parentReset.parent.callerValid=false;
    assert(R::ParentState(parentReset)==R::ParentKind::InvalidCaller);
    assert(R::Valid(parentReset)); // missing parent evidence does not invalidate verified child Reset
    parentWindow.Add(parentReset);assert(parentWindow.parentStates[2]==1&&parentWindow.parentStates[3]==1);
    parentReset.parent.callerValid=true;
    for(unsigned i=0;i<400;++i){parentReset.parent.callerRva=0x100000+i;parentWindow.Add(parentReset);}
    assert(parentWindow.calls==404&&parentWindow.parentStates[4]==402&&parentWindow.parentOverflow==386);
    assert(parentWindow.emitted==48&&parentWindow.dropped>0);
    auto tinyValueChange=reset;tinyValueChange.before.value.x=0;tinyValueChange.after.value.x=1e-30f;
    for(auto& s:tinyValueChange.before.samples)s.position.x=0;
    for(auto& s:tinyValueChange.after.samples)s.position.x=1e-30f;
    assert(R::Valid(tinyValueChange));
    R::Window exactValueChanges;exactValueChanges.Add(tinyValueChange);
    assert(exactValueChanges.valueChanged==1); // exact Reset evidence must not use a squared distance
    assert(R::InCapture(reset,true,19));assert(!R::InCapture(reset,false,19));assert(!R::InCapture(reset,true,20));
    assert(R::PreludeMatches(reset,0x1000,104,14));
    assert(!R::PreludeMatches(reset,0x1000,105,15));
    assert(!R::PreludeMatches(reset,0x1000,99,9));
    assert(!R::PreludeMatches(reset,0x2000,104,14));
    R::Window resetWindow;
    for(int i=0;i<400;++i){reset.frame=100+i;reset.tick=10+i;resetWindow.Add(reset);}
    assert(resetWindow.calls==400 && resetWindow.clones==400 && resetWindow.lostInterval==400);
    assert(resetWindow.invalid==0 && resetWindow.emitted==48 && resetWindow.dropped==352);
    assert(resetWindow.frameGroups==400 && resetWindow.tickGroups==400 && resetWindow.commandUnknown==400);
    assert(resetWindow.callers[0].calls==400 && resetWindow.callers[0].phases[0]==400);
    R::Window modeWindow;auto forced=reset;forced.resetLatchModeKnown=true;forced.resetLatchMode=2;
    for(unsigned i=0;i<400;++i){forced.tick=100+i;modeWindow.Add(forced);}
    assert(modeWindow.resetModes[3]==400&&modeWindow.emitted==48&&modeWindow.dropped==352);
    forced.resetLatchModeKnown=false;modeWindow.Add(forced);assert(modeWindow.resetModes[0]==1);
    forced.resetLatchModeKnown=true;forced.resetLatchMode=-7;modeWindow.Add(forced);assert(modeWindow.resetModes[4]==1);
    auto invalidReset=reset;invalidReset.after.samples[1].valid=false;resetWindow.Add(invalidReset);
    assert(resetWindow.calls==401 && resetWindow.invalid==1 && resetWindow.clones==400);
    invalidReset=reset;invalidReset.callerValid=false;resetWindow.Add(invalidReset);
    assert(resetWindow.unknownCaller==1 && resetWindow.invalid==2);
    R::Window callerOverflow;
    for(unsigned i=0;i<18;++i){reset.callerRva=0x12345+i;callerOverflow.Add(reset);}
    assert(callerOverflow.calls==18 && callerOverflow.callerOverflow==2);
    // Independently extracted slot106 wrapper (tail jump) and base ResetLatched body.
    const char* latchedWrapperFixture="56578bf98bb78806000085f674398d8e10100000e847caffff8d8efc0f0000e88cf9ffff56c786f00f00000000000089b6f40f0000e8865afcff83c404c78788060000000000008bcf5f5ee9e0ba0000";
    const char* latchedBaseFixture="538bd98b038b90e0010000ffd284c07525578b7b2485ff7e1c5633f68d6424008b43188b4c30088b118b4214ffd083c60c4f75ec5e5f5bc3";
    auto decodeFixture=[](const char* hex){std::array<unsigned char,80> out{};for(std::size_t i=0;hex[i*2];++i){auto d=[](char c){return c<='9'?c-'0':c-'a'+10;};out[i]=static_cast<unsigned char>((d(hex[i*2])<<4)|d(hex[i*2+1]));}return out;};
    auto latchedWrapper=decodeFixture(latchedWrapperFixture),latchedBase=decodeFixture(latchedBaseFixture);
    namespace LF=PortalNativeOriginFingerprint;
    assert(LF::LatchedBaseTarget(latchedWrapper.data(),80,0x1003b730)==0x10047260);
    assert(LF::LatchedBaseTarget(latchedWrapper.data(),80,0x3003b730)==0x30047260);
    assert(LF::LatchedBaseTarget(latchedWrapper.data(),79,0x1003b730)==0);
    assert(LF::LatchedBaseTarget(latchedWrapper.data(),80,0xfffffff0)==0);
    latchedWrapper[75]=0xe8;assert(LF::LatchedBaseTarget(latchedWrapper.data(),80,0x1003b730)==0);latchedWrapper[75]=0xe9;
    assert(LF::ValidLatchedBase(latchedBase.data(),56,0x18));
    assert(!LF::ValidLatchedBase(latchedBase.data(),55,0x18));
    assert(!LF::ValidLatchedBase(latchedBase.data(),56,0x1c));
    latchedBase[42]=0x18;assert(!LF::ValidLatchedBase(latchedBase.data(),56,0x18));latchedBase[42]=0x14;
    latchedBase[55]=0xc2;assert(!LF::ValidLatchedBase(latchedBase.data(),56,0x18));
    // These consumers reject malformed ring layouts before computing addresses.
    O::Ring ring{0x2000,4,3,3};
    assert(O::ValidRing(ring));
    assert(O::SampleAddress(ring,0)==0x2030 && O::SampleAddress(ring,1)==0x2000);
    assert(O::SampleAddress(ring,2)==0x2010 && O::SampleAddress(ring,3)==0);
    assert(O::SampleAddress(ring,-1)==0);
    assert(!O::ValidRing({0x2000,4,4,1}));
    assert(!O::ValidRing({0x2000,4,0,5}));
    assert(!O::ValidRing({0xfffffff0,2,0,1}));
    assert(!O::ValidRing({0x2000,5000,0,1}));
    assert(O::ValidRing({0,0,0,0}) && O::SampleAddress({0,0,0,0},0)==0);
    assert(O::MatchesScope(0x3000,0x3000,0x10a0,0x10a0,0x4000,0x4000,10,10));
    assert(!O::MatchesScope(0x3000,0x3100,0x10a0,0x10a0,0x4000,0x4000,10,10));
    assert(!O::MatchesScope(0x3000,0x3000,0x10c4,0x10a0,0x4000,0x4000,10,10));
    assert(!O::MatchesScope(0x3000,0x3000,0x10a0,0x10a0,0x4001,0x4000,10,10));
    assert(!O::MatchesScope(0x3000,0x3000,0x10a0,0x10a0,0x4000,0x4000,NAN,10));
    O::Selection selection;
    selection.called=selection.callerMatched=selection.outputsValid=selection.ringValid=true;
    selection.time=10;selection.amount=.1f;selection.effective=9.9f;selection.noMore=0;
    selection.ring={0,0,0,0};selection.fraction=0;
    assert(O::State(selection)==O::SelectionState::Empty);
    selection.returned=true;selection.ring=ring;selection.newerIndex=0;selection.olderIndex=1;
    selection.newer={0,10,{10,0,0},true};selection.older={1,9,{0,0,0},true};
    selection.newest=selection.newer;selection.samplesValid=true;selection.fraction=.9f;
    assert(O::State(selection)==O::SelectionState::Interval);
    selection.older.position={10,0,0};assert(O::State(selection)==O::SelectionState::Repeated);
    selection.olderIndex=0;selection.older=selection.newer;
    assert(O::State(selection)==O::SelectionState::Clamped);
    selection.returned=false;assert(O::State(selection)==O::SelectionState::NoSelection);
    selection.outputsValid=false;assert(O::State(selection)==O::SelectionState::Invalid);
    O::Observation origin;origin.installed=origin.called=origin.valid=true;
    origin.before={0,0,0};origin.after={1,0,0};origin.noMore=0;origin.selection=selection;
    O::Calls originCalls;originCalls.Add(origin);
    assert(originCalls.calls==1 && originCalls.changed==1 && originCalls.selectionInvalid==1);
    O::Calls originWindow;for(unsigned i=0;i<400;++i) originWindow.Merge(originCalls);
    assert(originWindow.calls==400 && originWindow.changed==400 && originWindow.selectionInvalid==400);
    const auto originClass=O::Classify(originCalls);
    origin.valid=false;O::Calls invalidOrigin;invalidOrigin.Add(origin);
    assert(invalidOrigin.invalid==1 && invalidOrigin.changed==0 && O::Classify(invalidOrigin)!=originClass);
    P::Calls originEdgeA,originEdgeB;originEdgeA.origin=originCalls;originEdgeB.origin=invalidOrigin;
    assert(P::MapClass(originEdgeA)!=P::MapClass(originEdgeB));
    selection.outputsValid=true;selection.returned=true;selection.olderIndex=1;
    selection.older={1,9,{0,0,0},true};selection.fraction=NAN;
    assert(O::State(selection)==O::SelectionState::Invalid);
    selection.fraction=.9f;selection.amount=NAN;
    assert(O::State(selection)==O::SelectionState::Invalid);
    selection.amount=.1f;selection.newerIndex=3;
    assert(O::State(selection)==O::SelectionState::Invalid);
    selection.newerIndex=0;selection.hermite=true;selection.oldestIndex=-1;
    assert(O::State(selection)==O::SelectionState::Invalid);
    selection.oldestIndex=2;selection.oldest={2,8,{-10,0,0},true};
    assert(O::State(selection)==O::SelectionState::Hermite);
    selection.samplesValid=false;assert(O::State(selection)==O::SelectionState::Invalid);
    selection.samplesValid=true;selection.newer.position.x=NAN;
    assert(O::State(selection)==O::SelectionState::Invalid);
    // A failed required newest read remains invalid even if native selection
    // returned false. Otherwise compressed logs hide this failure transition.
    O::Selection noSelection;
    noSelection.called=noSelection.callerMatched=noSelection.outputsValid=noSelection.ringValid=noSelection.samplesValid=true;
    noSelection.ring=ring;noSelection.time=10;noSelection.amount=.1f;noSelection.effective=9.9f;
    noSelection.fraction=0;noSelection.noMore=0;noSelection.newest={0,10,{10,0,0},true};
    assert(O::State(noSelection)==O::SelectionState::NoSelection);
    origin.valid=true;origin.selection=noSelection;O::Calls noSelectionCalls;noSelectionCalls.Add(origin);
    auto unreadNewest=noSelection;unreadNewest.samplesValid=false;unreadNewest.newest.valid=false;unreadNewest.failure=6;
    assert(O::State(unreadNewest)==O::SelectionState::Invalid);
    origin.selection=unreadNewest;O::Calls unreadNewestCalls;unreadNewestCalls.Add(origin);
    assert(unreadNewestCalls.selectionInvalid==1 && unreadNewestCalls.selectionFalse==0 && unreadNewestCalls.states[3]==0);
    assert(O::Classify(noSelectionCalls)!=O::Classify(unreadNewestCalls));
    P::Calls validSelectionEdge,invalidSelectionEdge;
    validSelectionEdge.origin=noSelectionCalls;invalidSelectionEdge.origin=unreadNewestCalls;
    P::DetailEdges selectionEdges;assert(selectionEdges.Take(validSelectionEdge,false));
    assert(!selectionEdges.Take(validSelectionEdge,false));assert(selectionEdges.Take(invalidSelectionEdge,false));
    // Native absolute operands must relocate exactly; instruction bytes do not.
    namespace FP=PortalNativeOriginFingerprint;
    unsigned char fingerprint[]={0x55,0xa1,0x00,0x20,0x00,0x10,0xc3};
    const unsigned reloc[]={2};
    assert(FP::Match(fingerprint,7,"55a100200010c3",0x10000000,reloc));
    fingerprint[5]=0x32;
    assert(FP::Match(fingerprint,7,"55a100200010c3",0x32000000,reloc));
    assert(!FP::Match(fingerprint,7,"55a100200010c3",0x10000000,reloc));
    fingerprint[6]=0xc2;assert(!FP::Match(fingerprint,7,"55a100200010c3",0x32000000,reloc));
    assert(!FP::Match(fingerprint,6,"55a100200010c3",0x32000000,reloc));
    assert(!FP::Match(nullptr,7,"55a100200010c3",0x32000000,reloc));
    assert(!FP::Match(fingerprint,7,"55a100200010c3",UINT32_MAX,reloc));
    // Independently extracted complete native Reset body: clear head/count,
    // three writer calls, exact global operands, void/no-stack-args ret.
    const char* resetHex="33c0568bf189460e8b460433c985c074658b15a8496c10d9420c5150518bced91c24e809e7ffff8b46046a0050518b0da8496c10d9410c8bced91c24e8efe6ffff8b5604a1a8496c10d9400c6a0052518bced91c24e8d6e6ffff0fb6461d8b56048d0c408b461403c903c9515250e8dd29430083c40c5ec3";
    std::array<unsigned char,120> resetBytes{};
    auto resetNibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<120;++i)resetBytes[i]=static_cast<unsigned char>((resetNibble(resetHex[i*2])<<4)|resetNibble(resetHex[i*2+1]));
    assert(FP::Match(resetBytes.data(),120,FP::kReset,0x10000000,FP::kResetRelocs));
    assert(!FP::Match(resetBytes.data(),119,FP::kReset,0x10000000,FP::kResetRelocs));
    for(unsigned at:{19u,48u,69u}) {std::uint32_t v=0;std::memcpy(&v,resetBytes.data()+at,4);v+=0x20000000;std::memcpy(resetBytes.data()+at,&v,4);}
    assert(FP::Match(resetBytes.data(),120,FP::kReset,0x30000000,FP::kResetRelocs));
    assert(!FP::Match(resetBytes.data(),120,FP::kReset,0x10000000,FP::kResetRelocs));
    resetBytes[35]^=1;assert(!FP::Match(resetBytes.data(),120,FP::kReset,0x30000000,FP::kResetRelocs));
    resetBytes[35]^=1;resetBytes[119]=0xc2;assert(!FP::Match(resetBytes.data(),120,FP::kReset,0x30000000,FP::kResetRelocs));
    R::Window malformedContext;auto badPhase=reset;badPhase.phase=static_cast<R::Phase>(99);malformedContext.Add(badPhase);
    assert(malformedContext.invalid==1 && malformedContext.phaseInvalid==1 && malformedContext.clones==0);
    // Independent disk fixture, with absolute operands relocated as the loader does.
    const char* part1Hex="558bec538b5d14568bf1c70301000000f686e000000001743d80be440100000075348b860402000083f8ff74298b15a8506c108bc881e1ff0f000003c98d4cca0485c97411c1e80c39410475098339000f85f7000000803d6cf86b10000f84ea00000080befa01000000578b7d0875108b068b90e00100008bceffd284c0745d8b860002000050e8a4a501008bc883c40485c97448a1a8496c10f30f10070f2e400c9ff6c4447a35e8c3a70100d95514f30f104514d91fa1a8496c10f30f5c401cf30f11078b0da8496c10f30f104920f30f59481cf30f58c8f30f110fd986a00000008b450cd91851d986a40000008d5618d958048bced986a8000000d958088b4510d986c4000000d918d986c8000000d95804d986cc000000d95808d907d91c2452e8f8ccffff8903a10c4a7210837830005f7506f6465c047406c703000000005eb8010000005b5dc210006a008bcee84ab8ffff5e33c05b5dc21000";
    std::array<unsigned char,350> part1{};
    auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<part1.size();++i) part1[i]=static_cast<unsigned char>((nibble(part1Hex[i*2])<<4)|nibble(part1Hex[i*2+1]));
    assert(P::ValidBody(part1.data(),part1.size(),0x10000000));
    assert(!P::ValidBody(part1.data(),349,0x10000000));
    assert(!P::ValidBody(nullptr,350,0x10000000));
    const auto diskPart1=part1;
    // These six operand locations are from the disassembly, not the validator.
    for(unsigned at : {47u,88u,150u,184u,199u,299u})
    {
        std::uint32_t value=0;std::memcpy(&value,part1.data()+at,4);
        value+=0x22000000;std::memcpy(part1.data()+at,&value,4);
    }
    assert(P::ValidBody(part1.data(),350,0x32000000));
    assert(!P::ValidBody(part1.data(),350,0x10000000));
    part1[47]^=1;assert(!P::ValidBody(part1.data(),350,0x32000000));
    part1=diskPart1;part1[348]=20;assert(!P::ValidBody(part1.data(),350,0x10000000));
    part1=diskPart1;part1[292]^=1;assert(!P::ValidBody(part1.data(),350,0x10000000));
    part1=diskPart1;part1[0]=0xe9;assert(!P::ValidBody(part1.data(),350,0x10000000));

    P::Observation stopped;
    stopped.inputTime=10;stopped.selectedTime=10;stopped.result=0;stopped.noMore=1;
    stopped.callerMatched=true;stopped.nativeValid=true;stopped.beforeOrigin={1,2,3};stopped.afterOrigin={1,2,3};
    stopped.beforeAngles=stopped.afterAngles={0,0,0};
    // STOP does not initialize old origin/angles; poison must never invalidate the sample.
    stopped.oldOrigin={NAN,NAN,NAN};stopped.oldAngles={NAN,NAN,NAN};
    P::Calls stops;stops.Add(stopped);
    assert(stops.count==1 && stops.stopped==1 && stops.invalid==0 && stops.oldOutputsValid==0);
    assert(stops.changed==0 && stops.noMoreTrue==1);
    auto continued=stopped;continued.result=1;continued.noMore=0;continued.selectedTime=9.98f;
    continued.oldOrigin={1,2,3};continued.oldAngles={0,0,0};continued.afterOrigin={2,2,3};
    P::Calls advancing;advancing.Add(continued);
    assert(advancing.continued==1 && advancing.oldOutputsValid==1 && advancing.changed==1);
    assert(advancing.timeAdjusted==1 && advancing.invalid==0 && advancing.lastOldOrigin.x==1);
    assert(P::Classify(stops,true)!=P::Classify(advancing,true));
    assert(P::Classify(stops,true)!=P::Classify(stops,false));
    auto invalid=continued;invalid.selectedTime=NAN;advancing.Add(invalid);
    assert(advancing.invalid==1 && advancing.lastSelected==9.98f);
    P::Calls unexpected;auto bad=stopped;bad.result=2;unexpected.Add(bad);
    assert(unexpected.unknownResult==1 && unexpected.oldOutputsValid==0);
    auto mismatched=stopped;mismatched.callerMatched=false;P::Calls mismatch;mismatch.Add(mismatched);
    assert(P::Classify(stops,true)!=P::Classify(mismatch,true));
    // An unknown caller must not contribute successful branch/change evidence.
    assert(mismatch.invalid==1 && mismatch.stopped==0 && mismatch.noMoreTrue==0);
    P::Observation local=continued;
    local.nativeValid=true;local.recvBeforeOrigin={40,0,0};local.recvAfterOrigin={40,0,0};
    P::Calls divergent;divergent.Add(local);
    assert(divergent.changed==1 && divergent.recvChanged==0 && divergent.corroborated==1);
    local.nativeValid=false;P::Calls unread;unread.Add(local);
    assert(unread.invalid==1 && unread.changed==0 && unread.continued==0);
    assert(P::MapBranch(false,false,-1,-1,-1,NAN,NAN)==P::MapState::NotCalled);
    assert(P::MapBranch(true,false,2,0,1,10,10)==P::MapState::Invalid);
    assert(P::MapBranch(true,true,2,2,1,10,10)==P::MapState::OutsideRange);
    assert(P::MapBranch(true,true,2,1,0,10,10)==P::MapState::Skipped);
    assert(P::MapBranch(true,true,2,1,1,10,10)==P::MapState::Eligible);
    assert(P::MapBranch(true,true,2,1,0,11,10)==P::MapState::RewindEligible);
    assert(P::MapBranch(true,true,2,1,0,NAN,10)==P::MapState::Invalid);
    // Missing identity is not a corrupt map and cannot hide the other target.
    assert(unsigned(P::MapBranch(true,true,2,-1,-1,10,10))==6);
    P::MapObservation missingOrigin;missingOrigin.called=missingOrigin.valid=missingOrigin.installed=true;
    missingOrigin.count=2;missingOrigin.beforeTime=missingOrigin.selected=10;
    missingOrigin.anglesIndex=1;missingOrigin.anglesNeeds=1;
    assert(missingOrigin.OriginState()==6 && missingOrigin.AnglesState()==4);
    auto missingAngles=missingOrigin;missingAngles.originIndex=0;missingAngles.originNeeds=0;
    missingAngles.anglesIndex=-1;missingAngles.anglesNeeds=-1;
    assert(missingAngles.OriginState()==3 && missingAngles.AnglesState()==6);
    // Independently extracted 145-byte Part2 PE fixture (ret12, six comparisons).
    const char* part2Hex="558bec8b5508f30f10020f2e81a00000009f56f6c4447a24f30f1042040f2e81a40000009ff6c4447a12f30f1042080f2e81a80000009ff6c4447b088b751083ce01eb038b75108b550cf30f10020f2e81c40000009ff6c4447a24f30f1042040f2e81c80000009ff6c4447a12f30f1042080f2e81cc0000009ff6c4447b0383ce0285f6740656e8948cfdff5e5dc20c00";
    std::array<unsigned char,145> part2{};
    for(unsigned i=0;i<part2.size();++i) part2[i]=static_cast<unsigned char>((nibble(part2Hex[2*i])<<4)|nibble(part2Hex[2*i+1]));
    P::NativeLayout layout;
    assert(P::DiscoverLocal(diskPart1.data(),350,0x10000000,part2.data(),145,layout));
    assert(layout.origin==0xa0 && layout.angles==0xc4 && layout.map==0x18);
    part2[13]^=4;assert(!P::DiscoverLocal(diskPart1.data(),350,0x10000000,part2.data(),145,layout));
    part2[13]^=4;part2[143]=16;assert(!P::DiscoverLocal(diskPart1.data(),350,0x10000000,part2.data(),145,layout));
    P::EntryBindings binding;
    assert(binding.Add(0,0x3030,0x8000,1,0x10a0,0x10c4));
    assert(binding.Add(1,0x10c4,0x8100,0,0x10a0,0x10c4));
    assert(binding.Add(2,0x10a0,0x8200,1,0x10a0,0x10c4));
    assert(binding.originIndex==2 && binding.anglesIndex==1 && binding.originWatcher==0x8200);
    assert(!binding.Add(3,0x10a0,0x8300,1,0x10a0,0x10c4)); // ambiguous data identity
    P::EntryBindings malformed;
    assert(!malformed.Add(0,0x10a0,0,1,0x10a0,0x10c4));
    assert(!malformed.Add(0,0x10a0,0x8100,2,0x10a0,0x10c4));
    P::Window fullWindow;P::Calls mapped;
    local.nativeValid=true;local.map.installed=true;local.map.called=true;local.map.valid=true;local.map.total=3;local.map.count=2;
    local.map.originIndex=2;local.map.anglesIndex=1;local.map.originNeeds=1;local.map.anglesNeeds=0;
    local.map.beforeTime=local.map.selected=10;mapped.Add(local);
    for(unsigned i=0;i<300;++i) fullWindow.Add(mapped,true,1);
    assert(fullWindow.mapCalls==300 && fullWindow.originBranches[2]==300 && fullWindow.anglesBranches[3]==300);
    assert(fullWindow.recvChanged==0 && fullWindow.changed==300 && fullWindow.corroborated==300);
    assert(P::MapClass(mapped)!=P::MapClass(divergent));
    P::DetailEdges detail;
    assert(detail.Take(mapped,false));assert(!detail.Take(mapped,false));
    assert(detail.Take(divergent,false));assert(detail.Take(divergent,true));
    PortalNativeCadence::Tracker mapTracker;mapTracker.Begin(17,1,1,1);
    PortalNativeCadence::Sample mapFrame;mapFrame.frame=1;mapFrame.tick=1;mapFrame.realTime=1;
    mapFrame.mapClass=P::MapClass(mapped);
    assert(mapTracker.Record(mapFrame).emit);++mapFrame.frame;
    assert(!mapTracker.Record(mapFrame).emit);++mapFrame.frame;
    mapFrame.mapClass=P::MapClass(divergent);assert(mapTracker.Record(mapFrame).emit);
    P::MapSnapshot beforeMap,afterMap;
    beforeMap.entries=0x5000;beforeMap.total=3;beforeMap.count=2;beforeMap.time=9.5f;
    beforeMap.originIndex=2;beforeMap.anglesIndex=1;beforeMap.originWatcher=0x8200;beforeMap.anglesWatcher=0x8100;
    afterMap=beforeMap;afterMap.time=10;afterMap.originNeeds=0;
    assert(P::ValidMapReturn(beforeMap,afterMap,10));
    afterMap.originWatcher=0x8300;assert(!P::ValidMapReturn(beforeMap,afterMap,10));
    afterMap=beforeMap;afterMap.time=9.9f;assert(!P::ValidMapReturn(beforeMap,afterMap,10));
    afterMap.time=NAN;assert(!P::ValidMapReturn(beforeMap,afterMap,10));
    assert(P::Outcome(P::Calls{},true)==0 && P::Outcome(unread,true)==1);
    assert(P::Outcome(stops,true)==2 && P::Outcome(divergent,true)==3);
    assert(P::Outcome(divergent,false)==1);
    // Position diagnostics must not report an angle-only change as origin motion.
    auto angleOnly=stopped;angleOnly.afterAngles={0,2,0};
    P::Calls angleOnlyCalls;angleOnlyCalls.Add(angleOnly);
    assert(P::Outcome(angleOnlyCalls,true)==2);
    auto absent=local;absent.map=missingOrigin;P::Calls absentCalls;absentCalls.Add(absent);
    P::Window missingWindow;missingWindow.Add(absentCalls,true,1);
    assert(missingWindow.originBranches[6]==1 && missingWindow.anglesBranches[4]==1 && missingWindow.mapInvalid==0);
    beforeMap.originIndex=afterMap.originIndex=-1;beforeMap.originWatcher=afterMap.originWatcher=0;
    afterMap.time=10;assert(P::ValidMapReturn(beforeMap,afterMap,10));
    auto invalidNative=local;invalidNative.oldOrigin={NAN,NAN,NAN};P::Calls invalidButCalled;invalidButCalled.Add(invalidNative);
    assert(invalidButCalled.invalid==1 && invalidButCalled.changed==0 && invalidButCalled.mapCalls==1);
    auto badNoMore=continued;badNoMore.noMore=2;P::Calls impossible;impossible.Add(badNoMore);
    assert(impossible.invalid==1 && impossible.continued==0);
    auto overflow=continued;overflow.afterOrigin={1e38f,1e38f,1e38f};P::Calls nonFiniteDelta;nonFiniteDelta.Add(overflow);
    assert(nonFiniteDelta.invalid==1 && nonFiniteDelta.changed==0);
    // Independent PE bytes: tests exercise the exact validators used by runtime,
    // including ABI cleanup, map participation bounds and insertion data field.
    const char* loopFixture="558bec51f30f10450c53568b7508f30f104e180f2fc8ba01000000578955fc762133c03946147e1a33c98d9b000000008b3e8bda66895c0f024083c10c3b46147cee33dbf30f114618895d08395e147e4b8b3e66837c1f020074268b4c1f08d9450c8b018b501851d91c24ffd285c0740933c06689441f02eb07c745fc000000008b45084083c30c8945083b46147cc18b45fc5f5e5b8be55dc208005f5e8bc25b8be55dc20800";
    const char* originFixture="8d86a00000008945f48d45f050518d4e18897df8c745f002000100e878dcffff";
    const char* anglesFixture="8d86c40000008945e88d45e4506a008d4e18895decc745e402000100e817dcffff";
    const char* insertFixture="558bec568bf18b460c8b4e048d5001573bd17e0b2bc140508bcee8a193ffffff460c8b460c8b7d088b0e2bc748894e1085c07e1b8d044003c08d147f03c08d0c91505183c10c51e8d452380083c40c8b168d0c7f8d048a85c074138b4d0c8b1189108b51048950048b49088948088bc75f5e5dc20800";
    auto verifyFixture=[&](const char* fixture,const char* expected)
    {
        std::array<unsigned char,180> bytes{};const auto size=std::strlen(fixture)/2;
        for(unsigned i=0;i<size;++i) bytes[i]=static_cast<unsigned char>((nibble(fixture[2*i])<<4)|nibble(fixture[2*i+1]));
        assert(P::ExactHex(bytes.data(),size,expected));
        assert(!P::ExactHex(bytes.data(),size-1,expected));
        for(unsigned i=0;i<size;++i)
        {bytes[i]^=1;assert(!P::ExactHex(bytes.data(),size,expected));bytes[i]^=1;}
        bytes[0]=0xe9;assert(!P::ExactHex(bytes.data(),size,expected));
    };
    verifyFixture(loopFixture,P::kLoop);verifyFixture(originFixture,P::kOriginBinding);
    verifyFixture(anglesFixture,P::kAnglesBinding);verifyFixture(insertFixture,P::kInsert);
    part2[143]=12;auto relocated=diskPart1;
    for(unsigned at : {47u,88u,150u,184u,199u,299u})
    {std::uint32_t value=0;std::memcpy(&value,relocated.data()+at,4);value+=0x22000000;std::memcpy(relocated.data()+at,&value,4);}
    assert(P::DiscoverLocal(relocated.data(),350,0x32000000,part2.data(),145,layout));
    assert(layout.origin==0xa0 && layout.angles==0xc4 && layout.map==0x18);
    assert(!P::DiscoverLocal(relocated.data(),350,0x10000000,part2.data(),145,layout));
    P::Window partWindow;
    partWindow.Add(stops,true,1);
    P::Calls same;stopped.inputTime=10.01f;same.Add(stopped);partWindow.Add(same,true,1);
    P::Calls back;stopped.inputTime=10.02f;stopped.selectedTime=9.9f;back.Add(stopped);partWindow.Add(back,true,1);
    partWindow.Add({},true,1);partWindow.Add({},false,1);
    assert(partWindow.frames==5 && partWindow.observedFrames==4 && partWindow.noCallFrames==1);
    assert(partWindow.calls==3 && partWindow.stopped==3 && partWindow.selectedStalled==1 && partWindow.selectedBackwards==1);
    assert(partWindow.outerWithoutPart1==1 && partWindow.notObservedFrames==1);

    // A time-only anomaly must survive compression even with unchanged movement/branch.
    P::Window times;
    PortalNativeCadence::Tracker temporalTracker;temporalTracker.Begin(16,100,10,1);
    PortalNativeCadence::Sample frame;frame.frame=1;frame.tick=10;frame.realTime=1;
    frame.frameTime=.003f;frame.speed=220;frame.originalValid=true;
    auto feed=[&](float input,float selected)
    {
        P::Calls c;auto v=continued;v.inputTime=input;v.selectedTime=selected;c.Add(v);
        const auto timeClass=times.CompareTime(c,true);
        frame.nativeClass=(P::Classify(c,true)<<12)|(timeClass<<23);
        frame.finalEye.x=frame.originalEye.x=float(frame.frame);
        auto r=temporalTracker.Record(frame);if(r.accepted) times.Add(c,true,1);
        ++frame.frame;return r;
    };
    assert(feed(10,9.90f).emit);
    assert(feed(10.01f,9.91f).emit); // unknown -> advancing
    assert(!feed(10.02f,9.92f).emit);
    assert(feed(10.03f,9.92f).emit); // advancing -> stalled, no eye/tick/branch change
    assert(feed(10.04f,9.90f).emit); // stalled -> backwards
    assert(times.selectedStalled==1 && times.selectedBackwards==1);
    for(int i=0;i<300;++i) feed(11.0f+i*.01f,i%2?9.92f:9.91f);
    assert(temporalTracker.GetSummary().emitted==128 && temporalTracker.GetSummary().suppressed>0);
    assert(times.frames==305 && times.calls==305 && times.selectedBackwards>100);
    frame.tick=50;
    assert(feed(20,10).emit); // terminal row is retained

    // Bytes independently copied from the installed PE32 client.dll disassembly.
    std::array<unsigned char,24> entry = {0x55,0x8b,0xec,0xd9,0x45,0x08,0x51,0xd9,0x1c,0x24,
        0xe8,0xc1,0x22,0xe1,0xff,0x84,0xc0,0x0f,0x95,0xc0,0x5d,0xc2,0x04,0x00};
    assert(ValidEntry(entry.data(),entry.size()));
    assert(!ValidEntry(entry.data(),23));
    assert(!ValidEntry(nullptr,24));
    entry[11]=0; assert(ValidEntry(entry.data(),24)); // relocated call target checked separately
    entry[22]=8; assert(!ValidEntry(entry.data(),24)); // wrong callee-cleanup ABI
    entry[22]=4; entry[0]=0xe9; assert(!ValidEntry(entry.data(),24)); // preexisting detour
    unsigned char parent[] = {0x55,0x8b,0xec,0x83,0xec,0x20,0x56,0x8b,0xf1,0x83,0xbe,0x3c,0x06,0x00,0x00,0x00,0x0f,0x85,0x96,0x00,0x00,0x00,0xf3,0x0f,0x10,0x86,0x5c,0x06,0x00,0x00,0x57,0x33,0xff,0x80,0xbe,0x7c,0x08,0x00,0x00,0x00,0xf3,0x0f,0x11,0x45,0xf8,0x75,0x14,0x8b,0x86,0xa4,0x08,0x00,0x00,0x50,0xe8,0xe5,0xea,0xff,0xff,0x8b,0x8e,0xc8,0x08,0x00,0x00,0x88,0x01,0x8d,0x55,0xfc,0x52,0x8d,0x45,0xec,0x50,0x8d,0x4d,0xe0,0x51,0x8d,0x55,0x08,0x52,0x8b,0xce,0xe8,0x66,0x49,0x01,0x00,0x85,0xc0,0x75,0x17,0x39,0x7d,0xfc,0x74,0x48,0x50,0x8b,0xce,0xe8,0x25,0x92,0x01,0x00,0x5f,0xb0,0x01,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00,0xf3,0x0f,0x10,0x86,0x5c,0x06,0x00,0x00,0x0f,0x2e,0x45,0xf8,0x9f,0xf6,0xc4,0x44,0x7b,0x05,0xbf,0x08,0x00,0x00,0x00,0x83,0x7d,0xfc,0x00,0x74,0x09,0x6a,0x00,0x8b,0xce,0xe8,0xf5,0x91,0x01,0x00,0x57,0x8d,0x45,0xec,0x50,0x8d,0x4d,0xe0,0x51,0x8b,0xce,0xe8,0x25,0xe2,0x00,0x00,0x5f,0xb0,0x01,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00};
    assert(ValidParent(parent,sizeof(parent)));
    parent[178]=0x90; assert(!ValidParent(parent,sizeof(parent)));
    assert(!ValidParent(nullptr,181));

    Calls calls;
    calls.Add(10,10,{1,2,3},{2,2,3},true,true);
    calls.Add(10.01f,10.01f,{2,2,3},{2,2,3},false,false);
    assert(calls.count==2 && calls.insideRender==1 && calls.returnedTrue==1);
    assert(calls.changed==1 && calls.maxDelta==1 && calls.firstBefore.x==1 && calls.lastAfter.x==2);
    calls.Add(10.02f,10.02f,{2,2,3},{8,2,3},true,false);
    assert(calls.lastAfter.x==8 && calls.lastRenderAfter.x==2 && calls.validInsideRender==1);
    calls.Add(NAN,10,{0,0,0},{0,0,0},true,true);
    assert(calls.count==4 && calls.invalid==1 && calls.lastAfter.x==8);
    Calls successful;successful.Add(1,1,{0,0,0},{1,0,0},true,true);
    const auto normalClass=Classify(successful,true,true,true,true,0);
    assert(normalClass!=Classify(successful,true,true,true,true,1));
    assert(normalClass!=Classify(successful,true,false,true,true,0));
    Window window;
    window.Add(successful,true,true,2,true,true);
    window.Add({},true,false,0,false,false);
    assert(window.frames==2 && window.noCall==1 && window.calls==1 && window.changed==1);
    assert(window.comparable==1 && window.different==1 && window.maxDifference==2);
    assert(window.changedOriginalFrozen==1 && window.changedFinalFrozen==1);

    StageKey a,b;
    assert(SameStage(a,b));
    a.valid=b.valid=true; a.scalars[0]=b.scalars[0]=1;
    assert(SameStage(a,b));
    b.scalars[0]=1.000001f; assert(!SameStage(a,b)); // never epsilon-collapse real movement
    b=a; b.integers[2]=1; assert(!SameStage(a,b));
    b=a; b.valid=false; assert(!SameStage(a,b));
    b=a; b.scalars[5]=NAN; assert(!SameStage(a,b));

    CommandBatch batch;
    for(int i=1;i<=8;++i) batch.Add(i,220.0f);
    assert(batch.Ready() && batch.count==8 && batch.first==1 && batch.last==8);
    assert(batch.minSpeed==220 && batch.maxSpeed==220);
    batch={}; batch.Add(10,733); batch.Add(11,750);
    assert(!batch.Ready() && batch.count==2 && batch.minSpeed==733 && batch.maxSpeed==750);
    std::cout << "PortalNativeInterpolation tests passed\n";
}
