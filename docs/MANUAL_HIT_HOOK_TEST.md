# 原生命中归属：手动钩子检查

开发模块：`eflink.coop.stage1.18`。本次只验证本机原生伤害修改器能否关联到房间施法实例，不需要双开或第二个账号。

此开发版拒绝旧的客机伤害数值消息。房主原生伤害复核适配器尚未实现，`native_damage_verified=false`，房间可能显示 `host_adapter_incomplete` / `host_native_damage_verification`，不会获得完整合作权限。该状态是预期的开发限制。技能观察成功不表示联机伤害结算通过。

## 手动加载与采样

如果已有房间，先在模组菜单离房，等待个人世界恢复完成。在 PowerShell 中执行以下命令切换开发包：

```powershell
& 'D:\EFML\build\native\efml.exe' disable eflink.coop.stage1.16
& 'D:\EFML\build\native\efml.exe' add --dev 'D:\EF Link\build\dev\eflink.coop.stage1.18'
& 'D:\EFML\build\native\efml.exe' enable eflink.coop.stage1.18
```

如果还有其他 EF Link 游戏模块启用，先停用它们，保持只有一个 EF Link 游戏模块启用。保留其他模组原来的设置。

进入游戏后用现有菜单按键 `=` 建立只有自己的房间，保持跟随房主世界和战斗同步开启。开始只读采样：

```powershell
Set-Location 'D:\EF Link'
python tools/capture-hit-observation.py --module eflink.coop.stage1.18 --seconds 120 --output build/cooperation-stage1/manual-hit-18.json
```

脚本只请求本机模块的 `status`，不发出攻击、传送、开房或换模块命令。诊断文件只保存公开的钩子和房间状态，不保存 RPC 令牌、房间密码或账号登录资料。如果 `python` 不可用，使用现有 Python 环境运行同一脚本。

## 游戏里的操作（约 1–2 分钟）

1. 对附近普通怪物正常普攻 3–5 次，每次稍作停顿。
2. 使用一次当前角色的战技，等待技能自然结束。
3. 再使用一次技能，随后切换角色；用新角色普攻一次。
4. 离开模组房间，正常移动，等待采样结束。

用熟悉的普通怪物即可，不需要 Boss 或传送。正常游戏攻击仍走原生流程；观察代码不主动制造攻击。

## 判断结果

- `player_cast_observer_ready`、`damage_hook_ready` 应为 `true`。房间初始化后 `snapshot_ready`、`entry.local_scene_ready` 应为 `true`。
- 攻击后 `player_casts.events_sent` 增长；`hit_provenance.matched` 是否增长是核心证据。累积计数不要求从零开始，应比较采样前后。
- `hit_provenance.last.cast` 是房间施法编号，不能因技能名相同串到其他角色；切人后 `actor` 应改变。“最后施法”和“最后命中”可能来自不同时间，不要求它们在每次采样中都相等。
- 若 `missing` 增长，保留 `hit_provenance.error` 和采样文件。可能是原生施法资料、来源身份、命中单元编号或同步命中时机尚未适配，不能凭空补一个编号。
- 召唤物、投射物、多目标和持续伤害仍需分别适配与验收；本次不承诺它们通过。正常结束后的迟到命中可以查映射，打断后的网络声明会被拒绝。
- 离房后 `player_casts.tracked`、`hit_provenance.bindings` 应清零，房间角色与显示对象应清除，`cleanup.restoration_pending` 应为 `false`。累积计数和 `last` 保留历史诊断。

完成后告诉我普攻、战技、切人是否做完，以及有无异常。文件在 `D:\EF Link\build\cooperation-stage1\manual-hit-18.json`，我可以直接读取它。

## 返回上一个开发版

如需回到 `.16`，先手动离房并等待恢复完成，再执行：

```powershell
& 'D:\EFML\build\native\efml.exe' disable eflink.coop.stage1.18
& 'D:\EFML\build\native\efml.exe' enable eflink.coop.stage1.16
```

`.16` 是之前实测过的可恢复开发版，其旧的数值伤害路径仍未达到第一阶段验收标准。
