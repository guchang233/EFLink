# 原生适配路径：离线证据与接入约束

本表记录 2026-10-15 对当前 1.5.3 本地文件的静态核对。方法名代表候选路径，不代表已验证能力；iFix 分支、间接调用、原生对象生存期和官方服务响应仍需收尾实机验证。`.84` 已编译接入原生采矿适配器候选，所有生产执行、隔离、应用和恢复认证仍为 false，玩法仍未支持。

| 需求 | 已核对的路径 | 对实现的约束 |
| --- | --- | --- |
| 通用原生构建资料 | 实体持有 `EntityTemplateData`，其 `BaseTemplateData.name` 与资源组件的 doodad 配置属于不同层级；`get_objectType` 是抽象属性 | 通过真实模板对象做虚拟派发，核对实体和模板类型；不能把矿石玩法配置直接当作实体创建键，也不能直接调用没有原生入口的抽象声明 |
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

调用桥的离线测试使用 `tests/support/native_abi_fixture.cpp` 自行编译的微型导出 DLL。测试仅在独立测试进程内按明确路径加载该夹具，不加载安装目录的 GameAssembly，也不安装到游戏。它验证元数据选择、抽象声明拒绝、实际实现派发、值尺寸和 GC 根清理；这不能代替真实游戏 ABI 或世界隔离验收。

`.84` 的 `NativeMineEntry` 从 HostWorld 的根绑定解析操作者和资源，核对私有 cookie、当前原生资源编号；通过真实闭合 `ObjectPtr<Entity>` 的 `.ctor(Entity)` 构造引用，Mining 枚举值由实际元数据读取，最终调用 `NatureResourceManager.DoOperation`。没有猜测泛型值偏移、写耐久或伪造攻击消息。Host 签发的不可复制许可只允许一次调用，并在原生参数构造之后重新检查成员、场景、原生归属及认证；RPC 和配置不能创建这种许可。函数返回 true 只表示调度接受，不能生成 Witness。生产候选未实现原生回调归属、客机对象创建/隔离/应用及个人恢复，也没有认证；不会因为接入 Session 自动开放。

`.86` 实现 `NativeEntityWithdrawal` 候选，调用原生 HideAllTypeOfEntity / RevertHideAllTypeOfEntity，只保留并撤销自己获得的句柄。掩码取自运行时枚举，管理器和 EntityDataStorage 由统一 GC 根保活；观察精确存储、分配计数器和句柄字典，不能用函数返回当成撤销证明。官方对同一存储的数据更新仍由原生游戏处理，没有旧值覆盖或账号结果补造。该入口需要编译适配器的隔离/恢复/双端认证，生产认证均为 false；World 的原生存储观察及卸载屏障已接入，入房流程尚未调用此片段。它只覆盖 Interactive、Enemy、Npc，不包含角色、技能、投射物及官方上报隔离，句柄退休也不证明个人世界完整恢复。完整适配仍缺，不开放共享操作。

`.88` 实现原生私有 ClientCharInfo 数据构造候选，直接调用 SROLE_DATA_CHAR 构造函数，不经好友/抽卡工厂或全局 CharBag 容器。该原生构造函数本身会补最大好感度、对虚拟管理员模板做本账号性别转换，并沿用输入武器/基质实例编号；候选在私有对象中恢复实际观察的好感度、为武器/基质生成本机 runtime client ID 并重接父子引用，通过原生方法重算属性。构造导致模板或成长值变化则拒绝发布，不把本账号角色替代房客。装备数据由原生构造函数创建并生成其本地 ID；尚需实际完整装备/技能链验证。

私有资料构造支持的原生作用域暂限从实际枚举读取的 Main；未知作用域拒绝，不把 All/None 作为隔离空间。数据租约关联房间、场景代数、域、成员/成员代数及槽位，使用方未释放租约时不能清理；重连或未知版本不能继续访问旧租约。当前尚未创建角色 Entity，也未接入公开构造资料传输/来源验证或完整官方上报边界，全部生产认证仍为 false。

原生 InitDeckData 会挂接卡技能并可能订阅战斗事件；释放不能只丢 GC 根。候选使用原生 CopyTo 和 System.Array.GetValue 获取受保活的卡技能快照，核对 m_charInfo 后逐一 Detach，再调用原生解绑。静态检查发现解绑先清字段再移除委托，因此字段 false、HashSet 为空和函数返回均不足以证明清理。必须有独立原生事件注册/修饰器观察；生产观察器尚未接入，默认不会释放未获证明的资料，也不会重派清理来掩盖不确定结果。
