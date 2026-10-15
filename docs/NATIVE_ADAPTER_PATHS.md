# 原生适配路径：离线证据与接入约束

本表记录 2026-10-15 对当前 1.5.3 本地文件的静态核对。方法名代表候选路径，不代表已验证能力；iFix 分支、间接调用、原生对象生存期和官方服务响应仍需收尾实机验证。当前没有生产原生玩法适配器，所有执行、隔离、应用和恢复能力仍未支持。

| 需求 | 已核对的路径 | 对实现的约束 |
| --- | --- | --- |
| 房间角色资料 | `ClientCharInfo` 的实际资料构造函数可持有独立的武器/装备数据，使用原生 runtime client ID | 不调用满级/完美抽卡工厂。好友工厂会写入全局客户端容器，不能当成私有资料构造。仍须核对技能、属性和物品引用 |
| 角色原生分配 | `_CreateCharacter` 使用 `EntityAllocator.AllocateObject`、`CharacterRootComponent.InitServerData`、`Entity.Init`、`ObjectContainer.Register` | `_CreateCharacter` 自身从全局 CharBag 查找，不能直接用于私有房间资料。底层路径须保持私有资料、ID、归属及逐对象清理，不得替换账号背包 |
| 临时队伍 | `ClientSquad` 存在 `_ReportToServer`；队伍切换同时影响个人正式队伍 | 不直接把临时队伍当成官方上报隔离层，不无条件调用 Pop/ClearAll 清理其他系统的临时队伍 |
| 个人世界实体撤出 | `HideAllTypeOfEntity` → `EntityDataStorage.HideEntityByType` → 数据隐藏变化 → `_UnApply` → `RemoveNodeFromData`；节点撤出链可到 `Entity.InternalWithdraw` | 这是实体撤出候选，不只是 Renderer 开关；但类型掩码仅含 Interactive、Enemy、Npc，不能宣称已隔离角色、技能、投射物和所有官方事件 |
| 个人世界恢复 | `RevertHideAllTypeOfEntity` → 撤销本次隐藏句柄 → 数据隐藏变化 → `_Apply` → `CreateNodeFromData` | 只能撤销模组自己持有的原生句柄；官方数据继续归个人世界保存。不能把旧快照值覆盖到更新后的官方状态上；需要完整观察证明恢复，函数返回不算成功 |
| 采矿操作 | `NatureResourceManager.DoOperation(ObjectPtr<Entity>, ENatureOperation, resourceId)` 检查角色和资源后进入原生操作/技能系统 | 目标资源及操作者均须从当前生命周期绑定取得，不能来自报文中的 native ID。`StartFarm` 的常规调用链依赖当前主控角色，不能把房客操作偷换成房主角色 |
| 受击与耐久 | `OnReceiveAttack` → `_OnIntReceiveAttack`，含预测耐久、原生受击表现及 StartFarm；正式属性回调另进入 `_OnHPRatioChanged` | 预测耐久与正式属性分开。黑板中的施法者/技能/施法实例只作原生来源关联，不能把“收到受击”当成官方破坏事实 |
| 客机房间对象应用 | 正式耐久变化路径会播放效果、发出关卡事件；运行时破坏路径会进入 `_ReqDoodadCommonBreak` 的网络请求链 | `fromServer=false` 或 client ID 不是账号隔离证明。未经对象域、原生事件及发送路径的完整隔离，不得回放到客机，更不能仅关闭发送标志或直接写耐久补齐 |

实现顺序仍是：私有原生角色和共享对象归属 → 全类别输入/事件/官方发送隔离 → 房主原生执行与真实观察关联 → 隔离对象的原生结果应用 → 离房观察恢复。不能因为已找到某个方法就省略其他环节，也不能以这一离线表替代矿石端到端双客户端验收。

原始本地报告、类型索引及反汇编保存在忽略的 `build/cooperation-stage1/ore-reference`，不会随模组发布。工具见 [OFFLINE_NATIVE_ANALYSIS.md](OFFLINE_NATIVE_ANALYSIS.md)。V1/V2 ABI 保持不变，`full_sync_verified=false`。
