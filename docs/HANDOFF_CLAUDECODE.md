# EF Link → Claude Code 交接

截至 **2026-10-10 20:45（Asia/Shanghai）**。状态：**目标未完成**。
这是独立的 Endfield 联机模组，不是 Minecraft 模组。后续 MC 桥接暂不实现。
用户要求先交接，因此本轮停止推进新实验；不能把交接理解为完成目标。

## 给接手者的第一条指令

先完整阅读 `D:\EF Link\docs\SERVER_GOAL.md`，尤其目标、验收标准、
dev70–79 的进展与事故，再读本文件、查看实际工作区和当前运行状态。
接着继续实现真实共享世界，不要退回“副本怪＋隐藏私人怪”的方案。

工作区 `D:\EF Link`，分支 **codex/eflink-native**。大量用户与代理改动未提交，
部分新文件还未跟踪；本轮未提交、未推送。不要 reset、清理未跟踪目录或
覆盖既有改动。环境默认 cwd 是 `D:\MC x ENDFIELD`，命令应明确指定 EF Link。

## 最终目标和已定方向

房客真正进入以房主为主机的同一个服务器世界：同一套实体和 ID，同一权威
结算，原生读条进出，私人世界完整挂起与恢复。双方保持各自官方账号在线。
房间内构造标准服务器 protobuf，由游戏自己的解析器解析，交给客户端原生
`GameplayNetwork._Handle_*` / `NetBus` 流水线创建、生成、移除对象。

不走私服重定向、LR-C、登录或加密绕过；
`legacyReplicaAdapterAvailable=false`、`nativeFeedAdapter=true` 保持。
房主换图要逐客询问是否跟随；玩家同图/跨图传送、动作平滑、GUI 和屏幕位置/
距离显示都属于目标。完整验收必须是 **两份真实 EF 客户端**。
核心得到 active、模拟回执、协议解析成功、单元测试通过都不能代替验收。

## 接手时的实际运行状态

交接前重新只读查询并记录 `reports/authority-handoff-runtime.json`：

| 项目 | 房主：用户主游戏 | 房客：隔离测试副本 |
|---|---|---|
| EF PID | 64096，19:12:14 冷启动 | 65616，11:23:38 启动 |
| 联机模块 | eflink.dev79 | eflink.dev82 |
| 房间 | 763792363，名称 1，端口 47615 | 同一房间，192.168.2.3:47615 |
| 地图/关卡 | map01 / map01_lv001 | map01 / map01_lv001 |

此前两端入场 phase=active、阻塞为空；当前连接仍存在。但这是旧适配器入场状态，
不是最终原生共享服务器验收。两端命中统计仍是房客发送 5、房主采纳 5。
客机当前 native feed：entered=14、left=14、fed=9、deferred_native=2、
missing=0、errors=0、ops_suppressed=357、unknown/零主体未归属记录为空。
14 条房主状态包含 5 条死亡记录，存活 9 条；客机 7 个实例、2 个原生数据节点。

主游戏 EndCraft `endcraft.gameplay33` 之前查询仍 active，但
character_hidden=false、creative_protection=false。不要声称 EndCraft 已彻底关闭。
本轮没有修改它；主游戏原有 MC 桥接随冷启动运行，也未修改。

开发诊断 worldfeed14–21 已禁用；交接前特别确认 **worldfeed21 enabled=false，
框架 error 为空**。transport3 已停止并禁用；transport1 旧计数观察已停止，
transport2 房主只读采集已到期并停止/清空。没有故意留下正在采集的新诊断。
不要重新初始化已经在此进程加载后禁用过的模块 ID。

dev81 构建过但未装载；dev82 包含 dev81 候选修正。dev83 未构建/注册。
新内核 DLL、C++ 接口、场景回执状态机 **都没有接入或加载到 EF**。
已注册的 dev82 包不可覆盖；源码比它多一个状态显示修正及下面的新工作。

## 必须遵守的实机约束

- **房主账号仅做只读观察。注入/拦截实验先在客机测试账号进行。**
- **房主换版本前必须整体重启游戏。** 主进程装过 dev70–77 共 8 个版本后，
  热切 dev79 曾在 Unity GC 存活扫描崩溃，具体根因未完全证明。
  不要为了测试反复热切房主。客机也应控制累计热装载量，必要时冷重启。
- 所有触碰原生游戏的开发诊断排队到 **游戏线程**；RPC 线程直接原生施法曾崩溃。
- 每次装载使用全新模块 ID；同 ID 不能重初始化，不能覆盖已注册 DLL。
- 不操作游戏界面/鼠标/键盘，不用 CUA 控制游戏。用户明确说电脑控制在 EF 有 bug。
  Shell/RPC/开发工具可用；登录、UAC、正常换图和战斗由用户手动完成。
- 客机账号闲置可能自动登出；`local_valid=false` 在离线不采样时不等于登出。
- 不修改房主单实例配置，不重跑隔离 prepare.py 覆盖索引和配置。

## 这一轮已做：dev80–82

### 1. 原生异步卸载操作的源头抑制

新增 `src/authority/link_native_op_policy.h`，记录最多 8192 个已下发/退休的
来源实体 ID。`leaveNative` 在注入 LEAVE 之前先登记退休 ID，覆盖注入作用域
结束后的异步 AbilitySystem/技能解绑/Buff 清理。
仅已知来源且战斗实例为 0 时，在 `ServerReporter.CreateServerOp<T>` 源头抑制，
不事后删包，不全面屏蔽主体 0，恢复为非零官方战斗身份后允许正常上报。

重要：`OwnerInfo[0]` 是来源实体 ID、`[1]` 是战斗实例；
`CS_BATTLE_OP.owner_id` 对应战斗实例，不可当场景实体 ID。
10 项策略检查通过。客机临时原生实体实验卸载 20 次创建被抑制、该阶段官方
战斗上报 0；临时数据/实例已经清理。**不代表所有真实入/离房漏报均已消除。**

主要文件：`link_native_feed.h`、`link_world.h`、`link_module.cpp`、
`link_world_injector.cpp`；辅助 `tools/native_pb.py`、
`capture-native-lifecycle.py`、`verify-retired-native-ops.py`。

### 2. 混合官方视野消息的拆分

新增 `src/authority/link_official_view.h`。旧逻辑在 ENTER 含角色或 LEAVE 含
非怪物时整包放行，私人怪物也会漏进来。现在拆分私人怪物字段与其余原始字段，
其余消息仍交原流程，未知字段/原始字节保留，恢复只重放暂存部分。
临时替换 NetResponse 使用游戏解析器和 GC 根；原接收函数只调用一次。
11 项检查通过，dev82 已包含。**特定混合消息的原生实机行为尚未专门验收。**
原有暂存上限 **512 条且存在静默截断风险**，不能宣称完整私人世界恢复。

构建脚本执行检查由 `if errorlevel 1` 修成要求退出码等于 0，避免 Windows
负异常退出码被当成功。初次错误样本导致的失败没有计为通过，修正后重建。

### 3. 原生数据提交与可见实例分开，修复 LAN 入场阻塞

客机只读发现 ID 2100390312 距玩家约 66m，原生数据 committed=true、模板正确，
只是没有可见实例。旧入场规则会永远等，并每 5 秒重复 ENTER。
dev82 对 ID/模板/提交/零官方战斗身份核对后，把这种节点计为 deferred_native，
不再反复下发；真正缺失/可见实例未就绪仍阻塞。
客机从 dev80 正常离房后加载全新 dev82，房主保留 dev79。
真实 LAN 重连约 20 秒后 active，missing=0；5 次房客命中房主采纳。

这仍是旧命中转发/替身路线，**不等于统一权威服务器战斗已实现**。
源码另清理 Started 实例的残留 “awaiting native spawn” 状态；该后续修正
未单独构建装载，不能说正在运行的 dev82 已含它。

## 这一轮已做：尚未接入游戏的 Zig/C++ 核心

### 4. 房主原生场景导入及 ABI

`server/src/Room.zig`：可信房主 `SC_SELF_SCENE_INFO` 整批导入，共享登记
角色、怪物、交互物、NPC、召唤物，保留实体 ID/战斗身份/代次及脚本、掉落、
解锁区域、场景模式。深复制并验证冲突、预留容量后发布；清理旧房主对象，
保留客机队伍。战斗序号按连接保持连续，分配器避免 ID 冲突/回绕 0。
`beginNativeHostScene` 使用真实 scene instance，核心保留逐客跟随同意。

`server/src/SceneSession.zig`：change → enter → CS load finish → self bootstrap
→ 可信原生应用证明 → active。证明目前只在合成测试提供，真实适配器未供给。

`server/src/NativeAuthority.zig`、`include/EndfieldLink/NativeAuthority.h`：
独立内核 DLL 的 `EFLinkNative_GetApiV1`，create/destroy/add/import/分配已批准角色/
begin/sceneLoaded/nativeApplied/poll/remove。8 成员、64 条队列、单体 1MiB；
深复制输入、调用者缓冲区读出，短缓冲保留消息，编码成功后发布。
所有上下文访问由调用者串行化。房主模板必须可信，网络声称不能代替原生证明。

最新追加 guest `remove_member`：清理客机实体/战斗索引/SceneSession 和其队列；
过渡 token 按上下文全局递增，成员 ID 重用也不复用 token；最大值拒绝回绕。
房主移除须整个上下文销毁。**实际断线回调还没调用这个 API。**
当前 x64 API 表 88 字节（10 个函数指针），追加字段保留旧顺序；接口仍为实验 V1，
接入者必须做版本/size/函数指针校验。

### 5. C++ DLL 绑定及原生加载回执路由候选

新增 `src/authority/native_authority_client.h`：RAII、绝对 DLL 路径及受控搜索、
ABI 校验、互斥锁、先销毁上下文再卸载 DLL；poll 的查询/分配/读取同锁完成，
载荷复制为调用者所有。没有 Unity 或网络调用。

新增 `src/authority/native_scene_ack_route.h`：只在未来真实原生切图处理器
分发后启动的状态机，旧 LoadAtPos 不启动。token 绑定 world/transition/scene/
privateGeneration/room/member/sceneNum。处理顺序、重复回执、错误场景、60 秒
超时；失败/断线继续阻止共享加载回执流向官方，直到可信私人返回完成。
显式 beginPrivateReturn 后只允许保留的私人 sceneNum 回执走原路；回执完成和
代次相符后才恢复 idle，旧代次不能释放隔离。

**这些头文件还没有接入 `link_module.cpp`，没有安装 HGNetSession 发送路由钩子。**
状态机本身不取消原生异步加载、不核验真实私人世界恢复。sceneNum 相同也可能有
陈旧回调，未来适配器必须绑定过渡、取消旧任务并提供可靠证明，不能直接相信网络输入。

新增 `tests/native_authority_client_test.cpp` 和
`tools/build-native-authority-client-test.cmd`。实测 C++ 调用实际 Zig DLL，
消息序号 838/24/25、回执顺序、断线清理/重用、私人返回、错误场景/超时。
**没有调用游戏玩法处理器，没有实现真实原生读条。**

## 只读研究和准确入口

客机 `worldfeed21` 新增有界 metadata 查询和扩展 methodCode 白名单，排队游戏线程，
最多 512 方法/256 字段/2048 字节代码，只记录名字、类型、偏移、GameAssembly RVA，
不读取账户字段值。构建成功，只客机加载，查询后禁用。

- GameplayNetwork ChangeSceneBegin RVA 73151456；EnterScene 76989776，条件调用
  GameLevelLoader.OnSrvEnterScene 76989920；SelfSceneInfo 56636224。
- SelfSceneInfo 路径包括 SquadManager.ServerSpawnSquad 61054016、
  CharBagSystem.OnSelfSceneInfo 55809008、关卡脚本状态/属性/触发器/阶段、
  DungeonManager.OnSelfSceneInfo 102161312。有 IFix 分支，静态代码不等于实际执行。
- `GameplayNetwork.OnSceneLoaded` **发送加载完成后还要清 loading 标记**。
  不能跳过整个回调来“拦回执”，应在发送入口决定路由而保留回调运行。
- `HGNetSession.SendMessage(IMessage)` RVA **47453792 / 0x2d41660**，入口已经
  被既有观察钩子 JMP 改写，不能拿捕获头部作为未修改函数序言再装钩子。
  后续代码有 IFix 0x32/0x33 与 `[r14+0x60]` 原子计数；其字段名/含义还没核实，
  不要把它与 CreateServerOp 战斗序号混同。需要继续按元数据对应字段查。
- CharBag：GetCharInfo 49150464、TryGetCharInfo 57153296、
  get_clientCharInfos 73950096、CreateClientFriendCharInfo 106934824、
  CreateClientCharInfo 106933716；尚未修改或调用这些创建入口。
- EntityManager 的 136 个方法中有各类非角色 EnterView，没有找到 CharsEnterView；
  别假定 SC_OBJECT_ENTER_VIEW chars 能直接生成他人角色，需要原生 Squad/CharBag 流程。
- WorldInfo 元数据有当前 map/level，但未找到真实 scene instance getter；
  相同地图名称/关卡编号不能当成房主真实 scene_id。

元数据报告见 `reports/authority-{HGNetSession,WorldInfo,CharBagSystem,EntityManager}-metadata-21.json`
及 `authority-HGNetSession-SendMessage-21.json/.asm`。报告目录被 git 忽略。

## 实机证据的边界与没做完的事

1. **原生全场景切换未实现。** 生产适配器仍走 local LoadAtPos；
   SC_CHANGE_SCENE_BEGIN / SC_ENTER_SCENE_NOTIFY / SC_SELF_SCENE_INFO 完整流程未接管，
   CS_SCENE_LOAD_FINISH 没有改走共享会话。
2. **Zig 内核未接入真实连接。** 当前 room/session 仍是 C++ 原路线。
   新 DLL 只在独立测试进程使用；没有活的 EFLinkNet→内核→原生适配器桥。
   原生消息 1MiB 与既有可靠通道约 256KiB 的容量差还需要有界分片/重组设计。
3. **完整权威结算未实现。** 房客先本地得到伤害数，再由房主限界/采纳；
   怪物打玩家仍在被打方本地结算。需要可信技能意图和单一权威技能/伤害/死亡/复活链。
4. **真实原生玩家 ID/队伍未打通。** 怪物用了房主 ID，但他人角色仍有 0xEF
   逻辑 ID/手工替身路线；账号、角色、CharBag/Squad scope 映射待做。
5. **私人世界全挂起/恢复未实现。** 当前过滤只覆盖部分怪物视野，缓存上限 512；
   老私人怪物曾因 dev70–73 恢复留下 battleInst=0，正常官方换图刷新还需验证。
   自己的 Boss/子玩法残留曾造成菜单假死，必须尊重完整原生退出/返回流程。
6. NPC、交互物、掉落奖励、任务、Boss/副本脚本、全世界实体生命周期仍未实现；
   核心保存其载荷不等于执行世界规则。房客私人触发限制也不能替代真正同一世界。
7. **进出房所有漏报尚未证明消除。** dev82 入场 30 秒观察仍有 14 条官方
   CS_BATTLE_OP，仅类型计数没有载荷，无法归因。之后 25 秒空闲载荷观察为 0；
   dev80 离房前后 12 秒为 0，但没覆盖整个返回加载。不能据此宣布零漏报。
8. **不卡顿未验收。** 入场曾一帧约 2044ms；此前短空闲窗口约 0.4–2.3ms，
   最新交接快照 recent max 房主约 18.3ms、客机约 17.5ms。要检查实际动作/战斗，
   不能用空闲窗口低耗时代替连续平滑证明。
9. 房主换图询问的拒绝/接受/过期、同/跨图传送、断线/加载中失败/返回等双端
   验收都还没完成。已要求用户正常换到另一大地图、客机先拒绝；用户尚未回复
   已操作。先前房主五分钟只读采集已停止，后续操作前须重新开启有界采集。

此前 dev70–79 的 11/11 原生怪物生成、私人怪物原生移除恢复与打击记录有效，
但范围有限，详见 SERVER_GOAL。不能把这些扩展成上述未完成能力。

## 已验证的构建与检查

- dev80/81/82 最终 C++ 构建无警告；对应策略、视野拆分、会话、地图询问与
  DLL/API 检查通过。生产运行的 dev82 不包含后续新内核接入候选。
- `tools/build-native-server.ps1 -Step all` 最新 12/12 构建步骤成功；
  16 项 Zig 核心测试保持通过（最后增量构建部分步骤命中缓存）。
- 交接前 20:44 重新执行 `build/native_authority_client_test.exe`，**19 项通过，退出码0**。
- 同时重新执行 `tests/native_authority_abi_test.py`，**41 项通过，退出码0**。
- worldfeed21 构建成功。它只采集元数据/代码，无行为注入验收。

以上新核心检查全部是合成 protobuf / 进程外调用，明确不是两台真实客户端验收。

```powershell
Set-Location -LiteralPath 'D:\EF Link'
& '.\tools\build-native-server.ps1' -Step all
& '.\tools\build-native-authority-client-test.cmd'
& 'C:\Users\sow\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' '.\tests\native_authority_abi_test.py'
```

DLL 在 `build/native-server/bin/eflink-native-authority.dll`。
Zig 在 `build/toolchains/zig-x86_64-windows-0.17.0/zig.exe`。
原生模块构建 `tools\build-world-update.cmd eflink.devNN`；当前脚本会构建 GUI，
不要沿用老交接“始终只链接旧 GUI obj”的假设。开发查询用
`tools\build-world-feed.cmd eflink.worldfeedNN`。

## RPC、索引和装载方法

Python 路径如上。`tools/rpc.py` 的函数：
`request(body, moduleID, indexPath)`；body=None 是框架只读 status。
结果通常在 `reply['body']`；游戏字段在 `body['game']`，不叫 native。
不要输出索引 token 或任何认证数据。

| 用途 | 路径 |
|---|---|
| 房主实际索引 | D:\MC x ENDFIELD\.tools\framework\third-party\index.json |
| 房主 ownership-record | D:\MC x ENDFIELD\.tools\install-manifest.json |
| 房主镜像索引 | C:\Users\sow\AppData\Local\BetterEndfield\third-party\index.json |
| 客机实际索引 | D:\EF Link\test-environments\dual-instance\framework-guest\third-party\index.json |
| 客机镜像索引 | D:\EF Link\test-environments\dual-instance\profiles\guest\AppData\Local\BetterEndfield\third-party\index.json |

房主开房 body：`{"action":"host","room_name":"1","max_players":8,"port":47615}`。
客机加入：`{"action":"join","address":"192.168.2.3:47615"}`，同机也可 127.0.0.1。
离房 `{"action":"leave"}`。目前已经连接，不要为交接无故离房。

装载：先 `tools/set-enabled.py OLD off --index INDEX`，再
`tools/register-dev.py --id NEW --dll build/NEW.dll --index INDEX`。
房主必须带 `--ownership-record`，且先整体冷重启；客机同一已存在的不可变包
可用 `--reuse`，同步镜像也用 `--reuse`。首次创建包别误用 reuse。
包目录 `build/dev/<ID>/native/windows-x64/<ID>.dll`。
下一候选联机模块可用 dev83，下一新诊断 worldfeed22；先检查实际索引是否已被后来占用。

游戏路径：主 `D:\Arknights Endfield\Endfield.exe`（已有 XInput 代理）；
客 `D:\EF Link\test-environments\dual-instance\game-guest\Endfield.exe`。
客机启动脚本 `test-environments/dual-instance/launch-guest.ps1` 使用 Light Help
及隔离 AppData/环境。只有测试副本移除了 Unity 单实例限制。重启可能 UAC，用户处理。

## 本地报告与旧辅助脚本

本轮重要证据均在忽略的 `reports/`，交接后别清理：

- authority-retired-native-ops-dev80.json、authority-fed-native-ops-dev80.json：临时源头实验。
- authority-lan-missing-native-20.json：66m 原生数据节点。
- authority-lan-dev80-leave.json：有限窗口离房观察。
- authority-lan-dev82-entry.json、authority-lan-dev82-both.json：LAN 入场/两端状态。
- authority-lan-dev82-current-wire.json 与 -analysis.json：25 秒客机观察。
- authority-lan-host-scene-wire.json：五分钟无所选场景载荷，不是换图成功证据。
- authority-continuation-runtime.json、authority-handoff-runtime.json：续作/最新运行状态。
- metadata-21 与 SendMessage-21：上述最新只读研究。

前任 scratchpad：
`E:\UNITYT~1\claude\D--EF-Link\4d1df5d4-e457-4ff3-af7b-ea95d1a15497\scratchpad\`
内 swap.py/feedcmd.py/opscan.py/pb.py 可参考，原件保留。pb 编码器已复制到
tools/native_pb.py。参考源码在忽略的 references；没有打包游戏资产。
LR-S 协议编码器 AGPL 来源见 server/vendor/lr-proto/NOTICE.md，发布时保留许可。

## 建议下一步顺序

1. 只读核对客户端、索引和工作区，完整读 SERVER_GOAL；保留目前双方状态。
2. 在用户准备正常官方换图之前重新开启房主既有模块的限时只读采集，取得真实
   SC_CHANGE_SCENE_BEGIN/SC_ENTER_SCENE_NOTIFY/SC_SELF_SCENE_INFO，核对
   actual scene_id、角色/队伍、脚本上下文。旧采集已结束，不能假设会收到新操作。
3. 将真实连接接入 C++ 内核 binding：身份来自认证连接而非载荷自报，校验
   房间/世界/成员/过渡/代次，补齐有界分片和重组。补地图同意/超时/中断 API。
4. 客机游戏线程接原生切图/bootstrap，先解决自己的及他人的 CharBag/Squad
   原生创建和 scope。只在真正原生 change/enter 分发后启动新回执状态机。
5. 在 HGNetSession 发送源头路由 CS_SCENE_LOAD_FINISH，保留 OnSceneLoaded
   完整执行；正确处理既有共享钩子链、IFix、返回序号和陈旧异步回调。
6. 实现完整私人世界挂起、无丢失缓存、恢复/失败回滚和原生返回证明，再让状态机
   释放官方流量。不能凭本地 bool 或网络 ack 宣称恢复。
7. 接可信技能意图→同一权威结算→原生下行；消除 guest damage claims/替身，
   然后做交互/NPC/掉落/任务/Boss/副本规则及全部共享生命周期。
8. 新版本先客机验证；房主新版本须用户冷启动。按 SERVER_GOAL 五项双端验收
   留证，包含异常/拒绝/晚入场/断线/返回和持续动作，缺一项目标均未完成。

不要因为 SDK/状态机/核心测试已通过就发布“已经实现 MC 那样真实共享世界”。
