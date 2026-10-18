#include "../src/Portal/PortalNativeCadence.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace PortalNativeCadence;
int main()
{
    Tracker tracker;
    tracker.ObserveCommand(5179, 90);
    tracker.ObserveCommand(0, 91); // extra mouse sample is not a new command
    tracker.ObserveCommand(5178, 91); // prediction replay cannot rewind input identity
    assert(tracker.LastCommand() == 5179);
    assert(tracker.LastCommandFrame() == 90);
    tracker.Begin(25, 5179, 500, 10.0f);
    assert(tracker.LastCommand() == 5179);

    Sample s;
    s.frame = 100; s.tick = 500; s.realTime = 10.0f;
    s.frameTime = 0.01f; s.speed = 550.0f;
    s.originalValid = true; s.originalEye = {0,0,62};
    s.finalEye = s.originalEye;
    assert(tracker.Record(s).emit);
    s.frame++;
    auto frozen = tracker.Record(s);
    assert(frozen.finalFrozen && frozen.emit);
    assert(std::fabs(tracker.GetSummary().maxFinalFreezeMs - 10.0f) < .001f);
    assert(!tracker.Record(s).accepted); // duplicate render cannot inflate duration
    s.frame++; s.realTime += .01f;
    assert(!tracker.Record(s).emit); // same state: compressed
    s.frame++; s.tick++;
    s.originalEye.x = s.finalEye.x = 18.333f;
    auto step = tracker.Record(s);
    assert(step.emit && !step.finalFrozen);
    assert(std::fabs(step.finalDelta - 18.333f) < .001f);
    assert(std::fabs(tracker.GetSummary().maxFinalFreezeMs - 20.0f) < .001f);
    assert(tracker.GetSummary().maxFinalStep > 18.0f);

    s.frame++; s.originalValid = false;
    auto missing = tracker.Record(s);
    assert(!missing.originalComparable); // do not manufacture raw position evidence
    s.frame++; s.frameTime = NAN;
    assert(!tracker.Record(s).accepted);
    s.frameTime = .01f; s.tick = 540;
    assert(tracker.Record(s).completed);
    assert(!tracker.Active());
    tracker.Begin(0, 5200, 600, 20.0f); // normal-fall capture has no portal txn
    s.frame = 200; s.tick = 600; s.realTime = 26.0f;
    assert(tracker.Record(s).completed); // wall-clock cap protects paused sessions
    tracker.Reset();
    assert(tracker.LastCommand() == 0);
    assert(!tracker.Active());
    tracker.Begin(26, 5201, 700, 30.0f);
    s.tick = 700; s.realTime = 30.0f; s.originalValid = true;
    for (int i=0; i<500; ++i)
    {
        s.frame=300+i;
        if (i%2) s.finalEye.x += 1.0f;
        s.originalEye=s.finalEye;
        tracker.Record(s);
    }
    assert(tracker.GetSummary().emitted <= 128);
    assert(tracker.GetSummary().suppressed > 0);
    s.frame++; s.tick=740;
    auto terminal=tracker.Record(s);
    assert(terminal.completed && terminal.emit); // terminal evidence survives budget
    tracker.Reset();
    RowBudget rows;
    for (int i=0;i<160;++i) assert(rows.Take());
    assert(!rows.Take());
    assert(rows.used==160 && rows.dropped==1);
    tracker.Begin(27,5300,800,40.0f);
    s.frame=1000; s.tick=800; s.realTime=40; s.eyeStateValid=true; s.eyeHistory=true;
    tracker.Record(s);
    s.frame++; s.finalEye.x+=1; s.originalEye=s.finalEye;
    tracker.Record(s);
    s.frame++; s.finalEye.x+=1; s.originalEye=s.finalEye;
    s.eyeHistory=false; s.eyeReleased=true;
    assert(tracker.Record(s).emit); // release boundary even without tick/freeze edge
    tracker.Reset();tracker.Begin(28,5400,900,50);
    s={};s.frame=2000;s.tick=900;s.realTime=50;s.frameTime=.01f;s.speed=10;s.originalValid=true;
    tracker.Record(s);
    s.frame++;s.finalEye.x=s.originalEye.x=1;tracker.Record(s);
    s.frame++;s.finalEye.x=s.originalEye.x=2;
    assert(!tracker.Record(s).emit); // healthy same-tick moving sample is suppressed
    s.frame++;s.finalEye.x=s.originalEye.x=3;s.nativeClass=1;
    assert(tracker.Record(s).emit); // native-only event must survive eye-cadence compression
    for(int i=0;i<300;++i)
    {
        s.frame++;s.finalEye.x=s.originalEye.x+=1;s.nativeClass=i%2;tracker.Record(s);
    }
    assert(tracker.GetSummary().emitted==128&&tracker.GetSummary().suppressed>0);
    s.frame++;s.tick=940;
    assert(tracker.Record(s).emit); // native transitions cannot consume terminal reservation
    std::cout << "PortalNativeCadence tests passed\n";
}
