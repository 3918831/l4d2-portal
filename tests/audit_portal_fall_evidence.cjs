// Streaming captured-evidence audit, NOT a live game/subjective acceptance test.
const fs=require('node:fs'),rl=require('node:readline');
const input=process.argv.find(x=>x.startsWith('--input='))?.slice(8);
if(!input){console.error('Usage: node tests/audit_portal_fall_evidence.cjs --input=<captured.log> [--compact] [--assert-evidence (test27 only)]');process.exit(2);}
const phases=['server-pre','server-player-pre','server-walk-pre','server-post','client-command-pre','client-player-pre','client-walk-pre','client-command-post'];
const fields=s=>Object.fromEntries([...s.matchAll(/(?<![\w-])(\w+)=([^\s()]+)/g)].map(m=>[m[1],/^(?:[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?|0x[\da-f]+)$/i.test(m[2])?Number(m[2]):m[2]]));
const close=(a,b)=>Number.isFinite(a)&&Number.isFinite(b)&&Math.abs(a-b)<.05;
const countBy=(rows,key)=>Object.fromEntries([...new Set(rows.map(r=>r[key]))].map(k=>[k,rows.filter(r=>r[key]===k).length]));
const caps=new Map(),binds=new Map(),writes=new Map(),writeSummaries=new Map(),tags={},commands=[],hooks=[],probes=[],placements=[];
let line=0,build='',enabled='false',current=0;
(async()=>{
 for await(const s of rl.createInterface({input:fs.createReadStream(input),crlfDelay:Infinity})){
  ++line;
  if(s.includes('[PortalFileLog] build'))build=fields(s).id;
  if(s.includes('[PortalBsp][Mutation] event=activate reason=portal-placement-complete'))placements.push(line);
  if(s.includes('command=portal_server_fallfix ')){const f={...fields(s),line};enabled=f.enabled;commands.push(f);}
  const tag=s.match(/\[(Portal(?:Native|FallWrite)\w*)\]/)?.[1];if(!tag)continue;
  tags[tag]=(tags[tag]||0)+1;const f={...fields(s),line};
  if(tag==='PortalFallWriteHook')hooks.push(f);
  if(tag==='PortalFallWriteBind')binds.set(f.committedCmd,f);
  if(tag==='PortalFallWrite'){if(!writes.has(f.committedCmd))writes.set(f.committedCmd,[]);writes.get(f.committedCmd).push(f);}
  if(tag==='PortalFallWriteSummary')writeSummaries.set(f.committedCmd,f);
  if(tag==='PortalNativeCadenceBegin'){current=f.capture;caps.set(current,{...f,enabled,rows:[],phases:[],messages:[],resets:[],origins:[],frames:[],cvars:{}});}
  const c=caps.get(f.capture??current);if(!c)continue;
  if(tag==='PortalNativeFallState')c.rows.push(f);
  if(tag==='PortalNativeFallStateSummary')c.phases.push(f);
  if(tag==='PortalNativeFallStateProbe')probes.push(f);
  if(tag==='PortalNativeCadenceSummary')c.summary={...f,freeze:s.match(/freezeMs\(([^)]*)\)/)?.[1]};
  if(tag==='PortalNativeCadenceCvar')c.cvars[f.name]=f.valid==='true'?f.value:null;
  if(tag==='PortalNativePredictionField')c.messages.push({...f,value:s.match(/\bvalue='([^']*)'/)?.[1]});
  if(tag==='PortalNativeOriginReset')c.resets.push(f);
  if(tag==='PortalNativeOriginResetSummary')c.resetSummary=f;
  if(tag==='PortalNativeOriginFrame')c.origins.push(f);
  if(tag==='PortalNativeCadenceStage'){
   // Exact aliases omit tick/flags: hydrate from the same capture/frame stage,
   // never confuse a compressed row with an unavailable player snapshot.
   c.stageFrames??=new Map();
   const full=f.sameAs?{...c.stageFrames.get(`${f.frame}:${f.sameAs}`),...f}:f;
   c.stageFrames.set(`${f.frame}:${f.stage}`,full);
   if(f.stage==='main-view'&&f.prelude==='false')c.frames.push(full);
  }
 }
 const results=[];
 for(const c of caps.values()){
  const bycmd=new Map();for(const r of c.rows){if(!bycmd.has(r.cmd))bycmd.set(r.cmd,{});const p=bycmd.get(r.cmd);(p[r.phase]??=[]).push(r);}
  const paired=[...bycmd].filter(([cmd,p])=>phases.every(n=>p[n]?.length===1)).map(([cmd,p])=>({cmd,...Object.fromEntries(phases.map(n=>[n,p[n][0]]))}));
  const air=paired.filter(p=>p.cmd>c.committedCmd&&phases.every(n=>p[n].ground===4294967295&&p[n].moveType===2));
  const firstGround=c.frames.find(f=>f.flags&1)?.tick??Infinity;
  const late=t=>t>c.tick+2&&t<firstGround;
  const lateResets=c.resets.filter(r=>r.prelude==='false'&&late(r.tick)&&r.moveType===2&&!(r.flags&1));
  const frameTicks=new Map(c.frames.map(f=>[f.frame,f.tick]));
  const lateOrigins=c.origins.filter(o=>o.prelude==='false'&&late(frameTicks.get(o.frame)));
  const errs=c.messages.filter(m=>m.prelude==='false'),fall=errs.filter(m=>m.field==='m_flFallVelocity');
  const bind=binds.get(c.committedCmd),wr=writeSummaries.get(c.committedCmd),detail=writes.get(c.committedCmd)||[];
  const r={capture:c.capture,txn:c.txn,kind:c.kind,enabled:c.enabled,line:c.line,endLine:c.summary?.line,cmd:c.committedCmd,tick:c.tick,complete:c.summary?.reason,
   phases:c.phases.length,rows:c.rows.length,validRows:c.rows.filter(r=>r.fallValid==='true').length,missing:[...bycmd].filter(([cmd,p])=>!phases.every(n=>p[n]?.length===1)).map(([cmd,p])=>({cmd,phases:phases.filter(n=>!p[n]?.length)})),
   repeated:[...bycmd].filter(([cmd,p])=>Object.values(p).some(a=>a.length>1)).length,
   paired:paired.length,air:air.length,fallFinalEqual:air.filter(p=>close(p['server-post'].fall,p['client-command-post'].fall)).length,
   fallStartEqual:air.filter(p=>close(p['server-pre'].fall,p['client-command-pre'].fall)).length,
   moveZEqual:air.filter(p=>close(p['server-player-pre'].moveVelocityZ,p['client-player-pre'].moveVelocityZ)).length,
   finalVelocityZEqual:air.filter(p=>close(p['server-post'].velocityZ,p['client-command-post'].velocityZ)).length,
   serverWalkOfficial:air.filter(p=>close(p['server-walk-pre'].fall,-p['server-player-pre'].moveVelocityZ)).length,
   serverNegativeFall:air.filter(p=>p['server-walk-pre'].fall<0).length,
   errors:countBy(errs,'field'),lateFallErrors:fall.filter(e=>late(e.tick)).length,fallErrorLines:fall.slice(0,2).map(e=>({line:e.line,value:e.value,tick:e.tick})),
   resets:c.resets.filter(r=>r.prelude==='false').length,lateResets:lateResets.length,lateResetsWithFall:lateResets.filter(r=>fall.some(e=>e.frame===r.frame&&e.tick===r.tick&&e.line<r.line)).length,
   resetDetailDropped:c.resetSummary?.dropped,resetAllCalls:c.resetSummary?.calls,
   resetTicks:countBy(c.resets.filter(r=>r.prelude==='false').map(r=>({dt:r.tick-c.tick})),'dt'),
   lateOrigins:lateOrigins.length,lateOriginChanged:lateOrigins.filter(o=>o.changed>0).length,lateOriginStates:countBy(lateOrigins,'state'),firstGroundTick:Number.isFinite(firstGround)?firstGround-c.tick:null,
   moveTypes:countBy(c.rows,'moveType'),frameMoveTypes:countBy(c.frames,'moveType'),freeze:c.summary?.freeze,nativeChanged:c.summary?.changed,
   bind:bind?{line:bind.line,bound:bind.bound,enabled:bind.enabled,epoch:bind.epoch}:null,
   writes:wr?{line:wr.line,reason:wr.reason,calls:wr.calls,candidates:wr.candidates,changed:wr.changed,notified:wr.notified,applied:wr.applied,readback:wr.readback,rejected:wr.rejected}:null,
   writeDetailCount:detail.length,negativeWrites:detail.filter(w=>w.applied==='true'&&w.candidateValue<0).length,cvars:c.cvars,
   picks:air.slice(0,2).map(p=>({cmd:p.cmd,...Object.fromEntries(phases.map(n=>[n,{line:p[n].line,fall:p[n].fall,z:p[n].velocityZ,mz:p[n].moveVelocityZ}]))})),
   writeExamples:detail.slice(0,2),lateResetExamples:lateResets.slice(0,2).map(r=>({line:r.line,tick:r.tick,frame:r.frame})),
  };results.push(r);
 }
 const summary={bytes:fs.statSync(input).size,lines:line,build,captures:results.length,kinds:countBy(results,'kind'),hooks,commands:commands.map(c=>({line:c.line,enabled:c.enabled})),
  allBindings:[...binds.values()].length,failedBindings:[...binds.values()].filter(b=>b.bound!=='true').length,
  allWrites:[...writeSummaries.values()].reduce((n,r)=>n+r.applied,0),writeFailures:[...writeSummaries.values()].reduce((n,r)=>n+r.rejected,0),tags};
 const compact=process.argv.includes('--compact');
 console.log(JSON.stringify(compact?{bytes:summary.bytes,lines:line,build,captures:results.length,kinds:summary.kinds,placements,bindings:summary.allBindings,failedBindings:summary.failedBindings,allWrites:summary.allWrites,writeFailures:summary.writeFailures,
  cvarVariants:[...new Set(results.map(c=>JSON.stringify(c.cvars)))]}:summary));
 const pick=Number(process.argv.find(x=>x.startsWith('--capture='))?.split('=')[1]);
 for(const r of results){if(pick&&r.capture!==pick)continue;const {picks,writeExamples,cvars,lateResetExamples,...short}=r;console.log(JSON.stringify(compact?{capture:r.capture,txn:r.txn,kind:r.kind,enabled:r.enabled,air:r.air,fallStartEqual:r.fallStartEqual,fallFinalEqual:r.fallFinalEqual,lateFallErrors:r.lateFallErrors,lateResets:r.lateResets,resetTicks:r.resetTicks,dropped:r.resetDetailDropped,originStates:r.lateOriginStates,changed:r.lateOriginChanged,writes:r.writes,freeze:r.freeze}:process.argv.includes('--detail')?r:short));}
 const b=results.filter(c=>c.kind==='floor-wall'&&c.enabled==='true');
 const a=results.filter(c=>c.kind==='floor-wall'&&c.enabled==='false'&&c.air>0);
 const sum=(xs,k)=>xs.reduce((n,c)=>n+c[k],0);
 console.log(JSON.stringify({comparison:{A:{n:a.length,air:sum(a,'air'),fallEqual:sum(a,'fallFinalEqual'),lateResets:sum(a,'lateResets'),lateFallErrors:sum(a,'lateFallErrors'),origins:sum(a,'lateOrigins'),changed:sum(a,'lateOriginChanged')},B:{n:b.length,air:sum(b,'air'),fallEqual:sum(b,'fallFinalEqual'),lateResets:sum(b,'lateResets'),lateFallErrors:sum(b,'lateFallErrors'),origins:sum(b,'lateOrigins'),changed:sum(b,'lateOriginChanged')}}}));
 const strictA=a.filter(c=>c.writes?.reason==='native-ground');
 const nextPlacement=b.length?placements.find(n=>n>b[0].line):undefined;
 const samePairB=b.filter(c=>c.line<(nextPlacement??Infinity));
 console.log(JSON.stringify({sensitivitySamePairNaturalLanding:{A:{captures:strictA.map(c=>c.capture),air:sum(strictA,'air'),fallEqual:sum(strictA,'fallFinalEqual'),lateFallErrors:sum(strictA,'lateFallErrors'),lateResets:sum(strictA,'lateResets'),origins:sum(strictA,'lateOrigins'),changed:sum(strictA,'lateOriginChanged')},B:{captures:samePairB.map(c=>c.capture),air:sum(samePairB,'air'),fallEqual:sum(samePairB,'fallFinalEqual'),lateFallErrors:sum(samePairB,'lateFallErrors'),lateResets:sum(samePairB,'lateResets'),origins:sum(samePairB,'lateOrigins'),changed:sum(samePairB,'lateOriginChanged')}}}));
 if(process.argv.includes('--assert-evidence')){
  const pass=build==='pe-6AD1B572'&&a.length>=5&&b.length>=5&&a.some(c=>c.fallFinalEqual<c.air&&c.lateResetsWithFall>0)
   &&b.every(c=>c.complete==='capture-complete'&&c.air>0&&c.fallFinalEqual===c.air&&c.writes?.applied>0&&c.writes.applied===c.writes.readback&&c.writes.rejected===0&&c.lateResets===0&&c.lateFallErrors===0);
  console.log('captured-causal-evidence='+ (pass?'PASS':'FAIL')+' (not subjective/full-game acceptance)');process.exitCode=pass?0:1;
 }
})();
