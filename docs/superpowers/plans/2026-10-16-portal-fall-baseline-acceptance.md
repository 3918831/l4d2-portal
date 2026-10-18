# Portal fall 修复验收与技术资产收敛 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to execute this approved staged plan. Steps use checkbox syntax. Manual game acceptance gates are explicit user-owned dependencies; do not bypass them to finish implementation in one turn.

**Goal:** 将第27轮已证实有效的fall修复形成经过回归、可维护且可复用的代码和技术文档资产，并提交推送到当前远端分支。

**Architecture:** 保持原生有符号fall赋值和原生网络通知，不增加镜头补偿。先冻结已验证DLL补旧场景回归，再收敛默认值/状态报告/诊断体积，最后对候选产物短回归及累计修改审查后上库。

**Tech Stack:** Windows x86 C++17、现有L4D2 localserver Hook/运行态datamap与字节指纹、C++策略测试、Node流式证据审计、Markdown。

**Spec:** `docs/portal-floorwall-test27-fall-write-ab-analysis.zh-CN.md` 第6–7节，以及用户2026-10-16确认“每一步提供测试方法，实测完成后形成重要技术资产并上库”。

## Global Constraints

- 当前分支 `codex/portal-official-mechanism`；保留现有未提交修改，不切换分支或另造基线。
- 本轮先复用build `pe-6AD1B572`，禁止在第28轮回归前改变DLL或默认值。
- 不修改origin/velocity/ground/movetype/camera/Reset；不重启旧noclip、movement mutation、出口push、PortalMaskRepair。
- 人工测试由用户完成，控制台指令以英文分号结束；不要求高速循环过程中绑定/按主观标记。
- 不把缺测当通过，不把提交首tick纠错当必须清除的故障，不将旧卡模型/brush局部开洞混入本次修复。
- 用户已授权最终上库；只在回归、审查通过后执行普通commit/push，不force push、不纳入本机权限/原始大日志/游戏二进制或无关修改。

## Review Focus

1. 原生通知能力失配或字段不可写：必须拒绝，不能静默裸写；Task2保留/补齐对应自动测试。
2. 连续高频穿越、新事务替代旧作用域：不应漏接或残留；Task1上下循环、Task2生命周期测试、Task3短回归。
3. 落地、死亡、movetype/身份变更后的作用域停止：不得影响后续普通下落；Task1落地/普通下落、Task2策略测试。
4. 地面路径没有applied但合法退出：状态报告不得误导用户；Task2状态报告测试与Task3墙门回归。
5. 多轮累计脏工作区：不得将整个diff视作已验证功能；Task4按文件/功能审计，明确保留、删除和排除项。

## Task 1：冻结产物，补齐游戏回归

**Files:** 新增 `docs/portal-fall-baseline-regression-test28.zh-CN.md`；读取第27轮分析与现有产物；不修改C++。

**Interfaces:** 消费已部署DLL及现有控制台命令；产出用户日志、主观/落地结果和逐项通过/缺测/失败结论，作为Task2启动门槛。

- [x] 比较源/游戏DLL SHA-256与build记录；Expected: 两者均为 `EE512EF9C4CE0A765ABEAE62B7F0292022076EAE507290B8867F37DA109D1B4B`。
- [x] 核实现有命令注册，写出完整带分号指令、场景顺序、退出和日志保存方式。
- [x] 用户完成第28轮实测，提供日志与四组反馈。
- [x] 主代理流式审计：53次绑定/260次写入读回无拒绝，未见旧fall链回归；3500及完整反向动作缺测明确转Task3，特殊窗口另列。详见第28轮分析文档。

## Task 2：回归通过后进行最小收敛

**Files:** `src/Portal/PortalFallWrite.cpp`（默认值、状态报告）；`src/Portal/PortalNativeDiagnostics.cpp`与`src/Util/Logger/PortalFileLog.h`（按需诊断边界）；对应 `tests/PortalFallWriteTests.cpp`、`tests/PortalFileLogPolicyTests.cpp` 和测试runner。不在本轮预先删除代码。

**Interfaces:** 消费Task1验收结论；继续提供 `portal_server_fallfix <0|1>`、原生能力验证和按需诊断；产出新的候选DLL及可辨认build/hash。

- [x] 根据Task1结果固定增量为默认开启、reason澄清、重型原生诊断按需；NoBrushFound单独记录，不混修。
- [x] 为默认开关、Reset后报告、关闭诊断不影响修复、拒绝通知不写入和连续新commit作用域补齐自动化断言，默认写入/重型探针默认行为RED→GREEN。
- [x] 保留原生指纹/datamap交叉验证、合法caller、本地身份、先通知后写、有符号候选和读回；显式portal_diag_native 1恢复按需取证。
- [x] 29/29 runner、Debug x86构建通过；独立增量审查无阻塞发现，真实诊断切换的集成测试缺口作为Minor保留。
- [x] 游戏未运行时备份旧DLL并部署，源/目标hash一致；build pe-6AD2590C；完整指令见第29轮文档。

## Task 3：候选产物短回归

**Files:** 新建对应候选版测试/分析MD；不为了日志清零继续改变算法。

**Interfaces:** 消费Task2候选DLL，产出可上库验收结果。

- [x] 默认开启、墙门已记录动作、地板→墙面及3500循环日志通过；2026-10-18用户确认反向墙门跳跃/双向高速与关闭无敌后的出口立即落地两项都测过且正常。区分日志证据与人工补充，不把矩阵缺项默认为通过。
- [x] 按需开关关闭/开启/再关闭各120/95/127次成功写入，关闭时无高频原生明细；四个详细地板→墙面窗口支持旧链未复发。
- [x] 记录pe-6AD2590C最终身份与核心通过/覆盖限制；无新问题证据。按用户本次请求先整理文档/审查，不提前提交。

## Task 4：技术资产与上库

**Files:** 更新 `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`；新增聚焦本修复的原理/原生ABI/生命周期/故障与验收说明；整理适合上库的小型自动测试与证据摘要。

**Interfaces:** 消费Task3通过结论、Task2代码和多轮历史证据；产出审查过的代码/文档commit及远端当前分支同步结果。

- [x] 新建portal-signed-fall-continuity-technical-asset.zh-CN.md，覆盖因果链、语义/接缝、通知、负值、生命周期、回退、性能、适用范围及不支持热卸载的继承限制。
- [x] 对HEAD cd636ba累计已跟踪diff和未跟踪src/tests分组审查；纳入/排除清单见portal-fall-baseline-worktree-audit.zh-CN.md。没有暂存、没有删除其它个人研究。
- [x] 2026-10-18重新29/29 runner、Debug x86通过；两轴静态审查无新阻塞代码缺陷，冷却文档P2已修复；流式工具重放及文档链接复核。原始日志/二进制/本机配置不作为待提交内容。
- [ ] 在当前分支提交重大阶段基线并普通push到已核实upstream；核对本地/远端提交一致后报告commit与保留问题。

## 当前暂停点

第29轮日志及补充人工确认均已通过；文档/累计审查已完成，已测DLL哈希不变。正在完成明确清单的暂存复核与最终commit/push。origin当前尚无同名远端分支，将普通推送创建codex/portal-official-mechanism并设置upstream；不改bsp分支，不force push，不建PR。实际完成结果记入本机执行账本和最终回复。
