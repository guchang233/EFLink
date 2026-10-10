# Campofinale 参考笔记（2026-10-10）

来源：`D:\BaiduNetdiskDownload\ef server\Server\Campofinale.exe`，即开源私服
Campofinale 1.2.2-dev（作者 SuikoAkari，非商用许可）。日志中标注的客户端版本为
`WINDOWS-0.8.25`（测试版，约 2025-12）。

处理方式：只做静态阅读。从 .NET 单文件包中拆出程序集，用 ILSpy 反编译后存放在
`references/Campofinale-decompiled/`（已被 git 忽略，不提交）。没有运行任何 exe。
`Patch/`、mitmproxy、dispatch 服务是把客户端重定向到私服并绕过登录的那一套，
不使用。

## 与已有参考的分工

`references/LR-S-current`（Zig，2026-10，对应当前客户端）已经覆盖了**换场景**：
`SC_CHANGE_SCENE_BEGIN_NOTIFY → SC_ENTER_SCENE_NOTIFY`，客户端回
`CS_SCENE_LOAD_FINISH` 后，服务端再发 `SC_SELF_SCENE_INFO` 和
`SC_OBJECT_ENTER_VIEW`。它的怪物 `common_info.type=3`，与我们实测一致。
换场景这一块以 LR-S-current 和官方抓包为准。

Campofinale 版本旧，比如怪物 `type=4`、交互物 `type=5`，这些值不可照抄。但
LR-S-current **没有**实现的几块，它都有：战斗上报处理、宝箱/掉落、关卡脚本、
刷怪波次。下面列出的消息全部存在于当前协议
（`server/vendor/lr-proto/endfield.proto`），字段以当前协议为准。

## 1. 战斗上报：伤害、死亡、召唤（`HandleCsBattleOp.cs`）

服务端按 `op_type` 分派 `CS_BATTLE_OP.client_data.op_list`：

| 上报类型 | 内容 | 服务端动作 |
|---|---|---|
| `TriggerAction` / `DamageAction` | `BATTLE_DAMAGE_DETAIL{target_id=1, value=4, cur_hp=6, is_crit=3, part_inst_id=7}` | 目标扣血，回发 `SC_ENTITY_PROPERTY_CHANGE` |
| `TriggerAction` / `HealAction` | `target_id`, `value` | 目标加血 |
| `TriggerAction` / `SpawnEnemyAction` | `BATTLE_SPAWN_ENEMY_ACTION_DETAIL{source_id, enemy_lv, pos, rotation, scene_num_id}` | 按关卡数据中 `levelLogicId==source_id` 生成怪物，并发 `ENTER_VIEW` |
| `EntityDie` | `BATTLE_ENTITY_DIE_OP_DATA{entity_inst_id, scene_num_id}` | 移除实体，按掉落表生成掉落物 |
| `EntityValueModify` / 技能开始、结束 | 角色 hp/ultimatesp | 回发 `SC_CHAR_SYNC_STATUS` |

对我们的意义：

- **房客打房主怪的伤害，客户端本来就会写成上报**（`target_id`、`value`、`cur_hp`），
  也就是我们在 `ServerReporter.CreateServerOp` 源头拦下、不发给官方的那部分。
  拦截时可以先读出 `DamageAction` 的明细，再作为命中转交房主结算。这样命中就有了
  原生来源，不必另行测量。
- **怪物召唤小怪、脚本刷怪**也走上报（`SpawnEnemyAction`）。房主那边由官方服务器
  下发，房客在房间里时，必须由房主世界下发，不能被房客自己的客户端触发。
- **死亡**同样由客户端上报，服务端再决定掉落。房主怪死亡应只以房主为准。

## 2. 血量推送：`SC_ENTITY_PROPERTY_CHANGE`

`{inst_id=1, info=BATTLE_INFO{hp=1, ultimatesp=2}}`，服务端用它把权威血量推给
客户端。当前我们用 `SetHpDirectly` 写房客侧怪物血量，可以改成注入这条原生消息，
走客户端自己的处理流程（UI、血条、死亡判定）。**需要先实测**：当前客户端对怪物是否
处理这条消息。

## 3. 宝箱、交互物、掉落物

- 进入视野：`SCENE_INTERACTIVE{common_info, origin_id, properties(map<int32,
  DYNAMIC_PARAMETER>), meta, battle_info, bond_id, client_meta}`。`properties` 的键是
  数字，由交互物模板里的 `propertyKeyToIdMap` 把属性名映射成编号
  （`EntityInteractive.GetPropertyIndex`）。
- 开宝箱：客户端发 `CS_SCENE_INTERACTIVE_EVENT_TRIGGER{scene_num_id, id,
  event_name="open_chest", properties}`。服务端依次回 `SC_SCENE_INTERACTIVE_EVENT_TRIGGER`
  （空）、`SC_SCENE_UPDATE_INTERACTIVE_PROPERTY{scene_num_id, id, properties{4: int 1}}`，
  发奖励，再发 `SC_SCENE_DESTROY_ENTITY{scene_num_id, id, reason}` 和收集计数同步。
- 机关开关：`event_name="set_state_true"` 对应属性 `1` 置 1。
- 掉落物（`SceneManager.CreateDrop`）：一个交互物实体，模板取物品的 `modelKey`，属性为
  `is_collected`(bool)、`item_id`(string)、`count`(int)、`item_inst`(string)，用
  `ENTER_VIEW` 下发。拾取是 `CS_SCENE_INTERACT_SP_INTERACTIVE{op_type=PickDropPackItem}`，
  服务端发奖励后 `DESTROY_ENTITY`。

对我们的意义：房主开的宝箱、房主怪的掉落，要在房客侧显示成"已开/已拾取"，就是给房客
注入 `UPDATE_INTERACTIVE_PROPERTY` / `DESTROY_ENTITY`。**奖励一律不能伪造**：房客的
背包属于房客的官方账号，只能由官方服务器发放。

## 4. 关卡脚本（任务、机关、Boss 战触发）

脚本状态（`Scene.GetActiveScript`、`HandleCsSceneSetLevelScriptActive`）：`2` 未激活、
`3` 激活、`4` 已开始；`disableLevelscripts` 时为 `1`。实体的 `belongLevelScriptId`
指向的脚本未激活（`state<=2`）时，该实体不下发。客户端用
`CS_SCENE_SET_LEVEL_SCRIPT_ACTIVE` / `_START` 请求，服务端回
`SC_SCENE_LEVEL_SCRIPT_STATE_NOTIFY{scene_num_id, script_id, state, is_complete}`。

对我们的意义：房主世界里有些怪只在脚本激活后才出现（Boss、事件战）。房客侧要么同步
房主的脚本状态，要么只跟随房主实际在场的怪物（当前做法）。脚本状态直接影响房客官方
账号的任务进度，**不能注入**，只能观察。

## 5. 刷怪波次（挑战、据点战）

`CS_SCENE_MONSTER_SPAWNER_BEGIN_WAVE{scene_num_id, spawner_id, wave_id, client_timestamp}`
→ 服务端按 `SpawnerConfig.waveMap[wave].groupMap[*].actionMap[*]` 从 `enemyLibrary` 取
`enemyId`/`enemyLevel`/位置，生成怪物（ID 为随机数，不是关卡 ID），再回
`SC_SCENE_MONSTER_SPAWNER_BEGIN_WAVE`。启动刷怪器用 `SC_SCENE_MONSTER_SPAWNER_START
{scene_num_id, spawner_id, enabled_hidden_wave_key_list}`。

对我们的意义：波次怪的 ID 由服务端分配，两个账号不同。房客侧只能按房主下发的 ID 生成，
我们现有的"用房主服务器 ID 注入"已经适用。

## 6. 其他细节

- 怪物等级 = 场景难度表 `monsterBaseLevel` + 关卡数据中的 `level`
  （`Scene.Load`）。我们直接取房主客户端里的 `EnemyServerData` 等级，无需自行计算。
- 怪物属性 `attrs` 按 `enemyAttributeTemplate` 的等级相关和等级无关两部分填写。
  我们注入时不带 `attrs`，客户端已能正常生成；如出现属性异常，再补这一项。
- 同场景传送（`HandleCsSceneTeleport`）只回 `SC_SCENE_TELEPORT`，客户端完成后发
  `CS_SCENE_TELEPORT_FINISH`；跨场景则走 `EnterScene`，即 `ENTER_SCENE_NOTIFY`。

## 结论：换地图不能单靠注入

无论 Campofinale 还是 LR-S，换场景的最后一步都是**客户端向服务器回
`CS_SCENE_LOAD_FINISH`**。房客连的是官方服务器，如果我们给房客注入
`SC_CHANGE_SCENE_BEGIN_NOTIFY`/`SC_ENTER_SCENE_NOTIFY`，房客客户端会把一个官方服务器
从没发起过的场景加载完成消息发给官方服务器，造成账号状态不一致。

因此跨地图跟随应由房客的官方账号**真实地**完成场景切换：通过房客已解锁的传送点，或
官方允许的其他方式。场景切换完成后，再由原生下行接管怪物。同一地图内的跟随仍沿用现有的
`LoadAtPos` 加载流程。
