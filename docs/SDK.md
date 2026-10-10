# EF Link SDK 1 / 2

## 世界接口（1.2.0-dev）

`EFLink_GetApiV2` 提供 `transport`（原 V1 表）、`get_world_json` 与
`request_teleport`。检查 `version == 2`、`struct_size >= sizeof(EFLink_ApiV2)`。
V1 调用方继续可用。

先调用 `get_world_json(nullptr, 0)` 得到含结尾 NUL 的所需字节数，分配自己的
缓冲区后再次调用；若返回所需长度增大，扩容重试。容量不足时不写入。
JSON 包含 `session`（epoch、revision、就绪状态）、`world`（地图、稳定实体编号）
与 `native`（应用与传送状态），不暴露 Unity 地址。

`request_teleport(peer)` 为异步提交，返回成功不等于抵达；查看
`native.teleport` 与 `native.teleports_completed`。游戏接口仅在游戏线程执行。
`eflink.world.v1` 为保留通道；额外领域适配使用自己的命名通道。

世界 JSON 中 `session.ready` 只表示已收到有效且新鲜的快照；真正完成世界入场
必须检查 `session.entry.active`。`session.entry.phase` 和 `blockers` 说明加载或
原生接管缺口。当前游戏适配器能力不完整，因此不会报告完整入场，客机战斗结算
处于阻止状态。第三方消费者也必须按此区别使用数据。

原生接入的实验范围见源码包 `docs/WORLD.md`。稳定实体键、地图标识与世界版本
为后续外部适配提供边界，本 SDK 不包含 MC 连接。

EF Link 的模块 ID 为 `eflink.core`，支持 Windows x64。ABI 版本与发行版本分别管理：1.2.0-dev 导出兼容的 V1 传输接口与 V2 世界接口，报文协议仍为 1。

在你的 `module.json` 中声明 `"dependencies": ["eflink.core"]`，然后获取函数表：

```cpp
#include <windows.h>
#include <EndfieldLink/EndfieldLink.h>

HMODULE library = GetModuleHandleW(L"eflink.core.dll");
auto entry = library ? reinterpret_cast<EFLink_GetApiV1Fn>(
    GetProcAddress(library, EFLINK_ENTRY_V1)) : nullptr;
const EFLink_ApiV1* api = entry ? entry() : nullptr;
bool compatible = api && api->version == EFLINK_API_VERSION_1 &&
    api->struct_size >= sizeof(EFLink_ApiV1);
```

同名通道在房间的各个参与者之间互通。使用自己 mod 的命名空间，例如 `myproject.objects.v1`。多个订阅者可以同时打开同名通道；同一进程中的不同名称发生散列冲突时返回 `EFLink_Busy`。

```cpp
static void EFLINK_CALL Receive(void* context, uint16_t sender,
                                const void* data, uint32_t size) {
    // 数据只在此次回调期间有效。复制到你自己的队列，再在游戏线程使用。
}
uint32_t channel = 0;
api->open_channel("myproject.objects.v1", Receive, nullptr, &channel);
api->send(channel, EFLINK_EVERYONE, payload, length, EFLink_Reliable);
```

| 操作 | 说明 |
|---|---|
| `get_session` | 离线/连接中/房主/成员角色、房间名、本机 ID、玩家数 |
| `list_peers` | 名字、干员模型路径、地图、位置、朝向、角色状态；返回总数，最多写入 capacity 条 |
| `open_channel` / `close_channel` | 注册/注销命名通道 |
| `send` | 可靠 TCP 消息 ≤256 KiB，或不可靠 UDP 消息 ≤1024 字节 |
| `add_peer_listener` / `remove_peer_listener` | 玩家加入/离开通知 |
| `host_room` / `join_room` / `leave_room` | 与 GUI 相同的异步房间控制 |

所有字符串均为 UTF-8。输入指针借用到调用返回为止。会话和玩家结构的 `struct_size` 必须先设置为 `sizeof(...)`。房主 ID 固定为 1，成员从 2 开始；ID 会被重新使用，不能当成永久玩家身份。

`host_room` 接受 JSON，例如 `{"room_name":"朋友的房间","password":"","max_players":8,"port":47615}`。返回 `EFLink_Ok` 表示请求已接受；需要用 `get_session` 确认异步开房/加入结果。界面和诊断消息显示连接错误。断开并重新加入是新的会话。

函数可以从任意线程调用，但接收回调和玩家事件回调运行在网络线程。回调中可以发送、查询和注销自身，但应迅速返回；不可直接调用 Unity、等待网络线程或等待其他线程持有的资源。关闭通道和注销监听器会等正在执行的回调完成，返回后不再开始该注册的新回调。

模块停用时，先注销你自己的通道和监听器，再释放其上下文。EF Link 停用时会清理所有注册；重新启用后需要重新注册。关闭房间不清理通道，所以一般的离线/重连无需重新注册。

`has_state` 表示最近 3 秒内收到姿态，`same_map` 表示地图匹配。未知的延迟为 0；本机成员的 `rtt_ms` 是到房主的往返时间，不能把所有玩家的 0 都解释为零延迟。

通道提供消息运输，序列化格式、对象所有权、权威状态、版本协商、持久化与重连后完整状态应由使用者定义。可靠消息有序传送，但不跨断线持久化，也没有业务层确认。房间密码是局域网房间锁，数据通道不加密。

`examples/echo` 是可直接编译的第三方模块：在同一房间安装该示例的玩家之间发送 echo 请求并返回应答；它没有任何游戏资源依赖。
