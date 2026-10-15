# 原生路径的离线分析

`tools/inspect-il2cpp-metadata.py` 仅读取磁盘文件。它不加载 GameAssembly DLL、不调用游戏函数、不附加进程、不启动游戏，也不读取运行中的账号资料。结果中的声明和文件 RVA 用于查找待核对的原生路径；`abi_verified=false`，不能据此开放玩法。

布局参考为 MIT 授权的 [Il2CppDumper 声明](https://github.com/Perfare/Il2CppDumper/blob/4741d46ba9cd6159c5d853eb9d6fc48b4bfa2b1a/Il2CppDumper/Il2Cpp/MetadataClass.cs)，固定提交及许可随工具保留。当前客户端的 metadata v29 含扩展头和额外类型声明字；工具必须显式选择 `endfield29-extended`，未知字的语义保持未知，不作为指针、尺寸或 ABI 依据。未知类型索引明确输出 unresolved，不猜测类型。

```powershell
python tools/inspect-il2cpp-metadata.py '<游戏目录>/Endfield_Data/il2cpp_data/Metadata/global-metadata.dat' `
  --layout endfield29-extended --binary '<游戏目录>/GameAssembly.dll' `
  --class '^Beyond.Gameplay.(CharBagSystem|Core.SquadManager|Core.EntityManager)$' `
  --method 'Create|Remove|Release' --output build/offline-native-factories.json

python -m unittest discover -s tests -p test_offline_metadata.py -v
```

PE 解析要求 Windows x64、唯一映射的原始节范围、与元数据一致的连续方法 token 数，以及唯一的 CodeGenModule 方法表。所有非空方法指针必须落在文件的可执行节；出现歧义、错误计数、越界或不支持的布局立即报错，不尝试载入 DLL 修复。

加入 `--native-types` 时，工具在 PE 原始数据中查找唯一的 v29 元数据注册表，核对**全部**类型声明的 by-value 索引、原生类型种类及定义归属，并检查类型/字段表的文件范围。该模式可解析封闭泛型、数组、指针、byref、泛型参数、嵌套类及父类，并输出原始字段偏移和静态标志。偏移不调整、不作为可调用 ABI；线程静态等负偏移原样保留。所有输出仍为 `abi_verified=false`。只凭默认声明模式无法解析的类型不能被猜成某个角色、物品或值类型。

当前文件的 225,789 个原生类型索引全部完成离线解析，无 unresolved 类型，最长类型名 895 字符。19 项合成文件测试覆盖唯一注册表、全量身份交叉核对、非法数据指针、泛型循环、泛型参数、嵌套归属及原始偏移；并未运行原生创建或清理函数。

原生路径核对已排除两种直接复用方式：`CreateClientFriendCharInfo` 写入客户端角色和装备容器，而 `RemoveClonedCharInfo` 清理的是克隆映射及另一组角色容器，无法成对清理；`ClientSquad` 带有 `_ReportToServer` 路径，不能因为名称含 Client 就当作不产生官方上报的隔离队伍。接下来须核对私有角色资料、底层原生分配、实际角色/技能引用和逐对象释放，再与统一隔离层整合；尚未实现完整矿石闭环。

2026-10-15，本地磁盘文件解析出 58,110 类型、453,234 方法、170 程序集。与已有只读实机记录核对了 461 个方法名/参数数量（其中 354 个完整类型签名可直接解析），无缺失；16 个此前实机读取的方法 RVA 与磁盘解析完全一致。11 项合成文件测试覆盖布局、边界、所有者/参数范围、错误代码指针、重复模块和未知类型。该证据只验证离线索引工具，不证明创建角色、原生执行、官方消息隔离或恢复安全。

本地 metadata SHA256 为 `0076743397acadf03d3b0064343a963c7c88863b8160526d397e4b3efb96f02e`。完整本地分析输出保存在忽略的 `build/cooperation-stage1/ore-reference`；不将游戏二进制、完整游戏元数据或反汇编输出提交到仓库。当前开发 DLL 仍为 `.79`，这次工具改动不改变游戏运行行为。
