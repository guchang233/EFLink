# 复用与独立性

新项目目录为 `D:\EF Link`，模块 ID 为 `eflink.core`。
它可单独构建和导入，不依赖 EndCraft、Minecraft 或旧模组的 WebUI。

| 已有实现 | 本项目的使用方式 |
|---|---|
| 原工作区 `native/link` | 复用协议、局域网网络、角色姿态接入和 C ABI，增加验证、原生菜单和生命周期修复 |
| Better-Endfield | 使用第三方模块 ABI、运行时解析器和共享钩子链，避免重复注入相机钩子 |
| Better-Endfield 相机 / 第一人称项目 | 保留其相机逻辑，联机模块只采样干员并显示远端模型 |
| Endfield Native Sandbox 的 Dear ImGui 源码 | 复用已有固定版本的原生 UI 依赖，编写独立 DirectComposition 菜单 |

发行源码包含构建所需的 ABI 头文件、ImGui 和 JSON 单头文件，构建不会读取旧工作区。
运行包不含开发用 RPC 入口、游戏资产、登录资料或本机日志。
复用许可、来源及版本见根目录 `THIRD-PARTY-NOTICES.md`。

开发时在现有 Better-Endfield 第三方模块索引中追加了独立的 EF Link 测试项；
它们不是源项目的一部分。同 ID DLL 已载入后无法替换，正式包升级需正常重启游戏。

## 实例隔离

`EFLINK_PROFILE_DIR` 可设为绝对目录，使该进程的偏好保存到该目录的
`settings.json`。未设置时使用 `%LOCALAPPDATA%\EFLink\settings.json`。
测试环境集中放在 `test-environments/`，不进入源码或发行 ZIP。
这个变量只隔离 EF Link 偏好；游戏、启动器和 Better-Endfield 的数据隔离
必须由实际双开工具配置，不能仅靠这个变量宣称完整隔离。
