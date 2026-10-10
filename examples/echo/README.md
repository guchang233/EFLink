# EF Link echo 示例

在两名玩家的 Better-Endfield 中同时导入示例包和 EF Link，加入同一个房间。
向 `eflink.example.echo` 发送任意模块消息，消息体最多 1023 字节。
示例在命名通道上广播请求，其他安装了示例的玩家定向返回应答。
响应通过 Host 日志和后续消息返回的 `responses_received` 计数确认。

示例只使用公开 C ABI，不包含网络栈或 Unity 调用。
接收回调运行在网络线程，实际玩法请把消息复制到自己的游戏线程队列。
`shutdown` 必须注销通道后再释放回调上下文。

从项目根目录运行 `tools/build.cmd` 会一起构建此示例。
SDK 包含示例源码、头文件及可导入的 `EFLink-Echo-1.0.0.zip`。
