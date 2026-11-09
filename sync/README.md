# 共享会话 API 原型

为 Windows 和 Android 提供设备独立的使用状态与共同倒计时。V1/V2 仅保留旧客户端兼容，v0.4.0 起两端使用 V3。

## 本地运行

Python 3.11+；Linux 通常自带 IANA 时区数据，Windows 缺少时区数据时可在虚拟环境安装 `tzdata`。在仓库根目录执行：

```powershell
python sync/server.py --init-token
python sync/server.py
python sync/test_server.py
```

第一条创建 `sync/.token`，不会打印令牌。本机自行读取文件内容填入客户端，不提交 Git、不截图分享。默认只监听 `127.0.0.1:8765`，数据库为 `sync/session.sqlite`，令牌和数据库均已忽略。模拟器使用 `http://10.0.2.2:8765`，远程真机需要 HTTPS。

## 部署到服务器

将 `server.py` 上传到服务器，用专用系统用户运行，并指定持久数据库和权限受限的令牌文件：

```sh
python3 server.py --init-token --token-file /path/to/private/token
python3 server.py --database /path/to/private/session.sqlite --token-file /path/to/private/token --timezone Asia/Shanghai
```

现有 HTTPS 反向代理需同时转发 `/v1/`、`/v2/` 和 `/v3/` 到本地 API 对应路径（或整个 `/eyebreak/` 前缀），保留 `Authorization`，禁用 API 缓存，请求体限制 64 KiB，设置连接/读取超时。访问入口需要有效 TLS 证书；客户端不忽略证书错误。不要直接公开原型 Python HTTP 端口。

这是单用户、单进程参考服务；公网正式使用还需要部署层速率限制、进程管理、数据库备份。客户端不预置服务地址或访问令牌。

### Ubuntu systemd 部署文件

`deploy/` 提供服务、每日备份定时器和 Nginx 路径代理示例。服务以专用 `eyebreak` 用户运行，程序放在 `/opt/eyebreak/`，数据库放在 `/var/lib/eyebreak/session.sqlite`，令牌放在 `/etc/eyebreak/token`（仅 root 和服务用户可读）。服务仅监听 `127.0.0.1:8765`，不直接对公网开放。

部署 `eyebreak-sync.service`、`eyebreak-backup.service` 和 `eyebreak-backup.timer` 到 `/etc/systemd/system/` 后执行 `systemctl daemon-reload`，启用同步服务和备份 timer。`backup.py` 使用 SQLite 在线备份接口，保留最近 14 份备份；实际运营还应将备份复制到其他存储。

Nginx 在已有 HTTPS 站点中代理 `/eyebreak/`，两端填写的服务器根地址应为 `https://sync.example.com/eyebreak`，不要附加 `/v1`。应在 Nginx HTTP 配置层设置限速，并配置 TLS 自动续期及成功续期后的 Nginx reload。已有站点修改前需要备份配置，并通过 `nginx -t` 后 reload。

若 HTTP 证书验证被域名拦截页或代理干扰，可改用 DNS-01 验证。手动添加 TXT 取得的证书并不会因启用了 Certbot timer 就自动续期；还需配置 DNS 服务商 API 认证 hook，或每次到期前重新完成手动验证。DNS API 凭据应单独保存为 root 可读文件，不提交源码。

`GET /health` 不需要令牌，保留旧版 `{"status":"ok","protocol":1}` 响应兼容，不表示仅支持 v1。公网对应 `/eyebreak/health`；会话和统计接口仍必须携带令牌。离线区间回填和旧本地历史导入尚未实现。

## 协议 V3：状态独立、计时共同

`GET /v3/sessions/{sessionId}/devices/{deviceId}` 读取共同计时和本机状态，POST 同一路径加 `/commands`。响应 `protocol:3`。`snapshot.state/deadlineMs/remainingMs/sittingMs/workMs/settings/usage` 是会话共同数据；`deviceState/deviceActive/available/deviceRevision` 只描述请求设备，`otherActive/activeDevices` 描述是否还有其他设备使用。设备 ID 沿用安装 UUID，无需用户更换房间或令牌。

聚合规则为逻辑 OR：任一在线设备 active 时共同 WORKING；全部 inactive 时共同 AWAY，三项按 5:1 回补并封顶。每段共同工作只统计一次。转换前先结算到服务器当前时刻，按设备租约、倒计时和休息截止点依次分段，避免迟到轮询误计。约每秒 heartbeat；30 秒租约到期撤销该设备 active，其他设备仍 active 时不回补。

`observe` 携带 available、platform；安卓可携带 resume=true 表示本次亮屏恢复。Windows available=true 只表示可用，不表示新输入；本机新输入另发 continue。rest/pause/end_work 撤销本机 active，不能让其他设备休息或把其状态改为工作。客户端保留未确认的离开事件，先补发 off 再恢复 on。操作附 expectedDeviceRevision 保护本机输入状态，其他设备操作不制造本机 CAS 冲突；设置、跳过自动休息和重置久坐等共同操作仍比较 expectedRevision。未知/过期版本返回 409 最新快照，客户端不重放旧输入。

自动倒计时到点仍全局 RESTING（默认 25 秒），暂停使用累计，清除原使用标记；结束后亮屏且租约有效的安卓恢复本机 active，Windows 等本机新输入。自动休息是原先约定的固定时长例外，不因亮屏而跳过。restCycle 在休息开始时增加，restFinished 在完成时记录对应序号，永久随快照提供；一个客户端查询不会消耗另一个客户端的结束提醒。锁屏手机用已收到的截止时间本地闹钟提醒，再以完成序号补发、去重。

首次 V3 建立时冻结 V1/V2 已有统计和提醒设置为初始历史；新共同周期从 20 分钟开始，旧独立倒计时不强行合并。旧接口继续使用各自旧表，不能控制新版状态。新表 coordinated_sessions / coordinated_operations 与旧数据同时进入 SQLite 在线备份。用户操作去重保存 24 小时，周期 observe 去重保存 5 分钟，限制每秒心跳带来的存储增长。

## 协议 v2：旧版 v0.3.0 兼容

`GET /v2/sessions/{sessionId}/devices/{deviceId}` 读取本设备；`POST` 同一路径加 `/commands` 执行本设备命令。设备 ID 为持久的安装 UUID，不使用硬件标识，也不让用户手填。首次设备为 `WAITING`，不会继承其他设备正在工作的状态。原有 v1 命令、CAS 和 UUID 去重规则保留；版本号按设备隔离，修改共享设置会刷新各设备版本号。

客户端每 5 秒 POST `{"action":"observe","available":true,"platform":"windows","expectedRevision":0,"operationId":"unique-uuid"}`；安卓使用 `platform:"android"`，亮屏为 true、熄屏为 false。Windows 锁屏 / 睡眠为 false，解锁为 true；true 只更新可用性，不替代本机输入证据，Windows 继续必须另发 `continue`。`observe` 不受旧 revision 限制，以免锁屏事件被 CAS 丢掉，但仍按 operationId 去重。客户端一次仅发一个请求，将未确认的离开事件排队，不能用后来亮屏覆盖尚未提交的熄屏。

响应为 `protocol:2`、`serverNowMs`、本设备 `snapshot` 和 `events`；快照继承 v1 结构，并增加 `available`、`leaseUntilMs`、`platform`、`deviceUsage`（本设备每日累计）。`usage` 是会话内全部设备的工作区间并集，重叠区间只算一次。所有设备请求都只推进各自状态，手机自动继续不会改变电脑的状态。关闭手机屏幕时自动休息仍保留 REST_END；结束后手机保持回补，待手机亮屏且在线再继续。电脑等待状态持续到本机新输入或点击继续。

可用性租约为 30 秒；心跳中断最多估计 30 秒后转入回补，重连本身不恢复 Windows。此机制限制后台被杀或离线的误计，不能回填失联期间的真实使用区间。共享设置、旧 v1 统计、各设备状态和日区间并集均保存在同一 SQLite 事务中。首次升级冻结 v1 既有统计为迁移前历史，不以旧活动状态初始化新设备。备份应在服务升级前执行；新建表不删除旧表，v1 路由继续可用。

操作去重保存最近 24 小时，避免每五秒心跳使数据库永久增长；同一 ID 同一内容在此窗口内重试不重复执行。请求仍上限 64 KiB、响应上限 1 MiB；每会话最多 32 个设备。

## 协议 v1（旧版兼容）

所有请求使用 `Authorization: Bearer <token>`，不将令牌放入 URL。会话标识为 8–80 位字母、数字、下划线或横线；同一令牌供本人的各设备使用，不是多用户登录系统。

`GET /v1/sessions/{sessionId}`：读取或首次创建工作中的会话，返回 HTTP 200：

```json
{
  "serverNowMs": 1792382400000,
  "snapshot": {
    "revision": 0, "state": "WORKING", "updatedAtMs": 1792382400000,
    "deadlineMs": 1792383600000, "remainingMs": 1200000,
    "sittingMs": 0, "workMs": 0, "sittingSent": false, "workSent": false,
    "lastNight": "", "zone": "Asia/Shanghai",
    "settings": {
      "restSeconds": 25, "sittingMinutes": 60, "workMinutes": 120,
      "nightHour": 23, "nightMinute": 0,
      "sittingEnabled": true, "workEnabled": true, "nightEnabled": true
    },
    "usage": {"2026-10-19": 0}
  },
  "events": []
}
```

`usage` 可以为空。服务器统一时区、按本地午夜分日。时间戳为 UTC Unix 毫秒；客户端用服务器锚点加单调时钟显示。服务端需有正常系统校时。

`POST /v1/sessions/{sessionId}/commands`，JSON 请求体：

```json
{"operationId":"a-unique-uuid-per-action","expectedRevision":0,"action":"rest"}
```

支持 `pause`、`continue`、`rest`、`skip`、`stood_up`、`end_work`、`settings`。修改设置附带上面的完整 `settings` 对象；音频 URI、主题、声音开关和悬浮窗设置留在各设备本地。

- `rest`：立即休息，进入 `AWAY`，三项 5:1 回补，使用时长停止增长、不扣减。
- 自动到期：`WORKING → RESTING → WAITING`；通知/轮询/校时不能自行继续，只有新操作发送 `continue`。
- `pause`：冻结提醒，停止累计；允许 `PAUSED → AWAY`。
- `skip`：仅自动休息时可用；`end_work` 清零提醒并暂停，不改历史。
- 所有客户端共享一个结算入口，不会两设备各累计一份。

成功返回 200 和最新快照；版本冲突返回 409 及同格式快照，刷新后让用户重新操作。相同 `operationId` 和相同请求重试不重复执行；同 ID 不同请求返回 400。未认证 401、路径无效 404、字段/状态无效 400。请求最大 64 KiB，Android 快照最大 1 MiB。服务不打印 HTTP 访问日志或令牌。

测试覆盖鉴权、跨天、重启保留、暂停回补、休息结束等待、并发冲突、幂等和双设备不重复统计。

原型暂未实现：多用户/配对、离线区间合并、旧本地历史导入。
