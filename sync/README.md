# 共享会话 API 原型

为 Android 原型提供可运行的共享会话参考接口，需自行部署。Windows 客户端暂未接入。

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

现有 HTTPS 反向代理将 `/v1/` 转发到 `http://127.0.0.1:8765/v1/`，保留 `Authorization`，禁用 API 缓存，请求体限制 64 KiB，设置连接/读取超时。访问入口需要有效 TLS 证书；客户端不忽略证书错误。不要直接公开原型 Python HTTP 端口。

这是单用户、单进程参考服务；公网正式使用还需要部署层速率限制、进程管理、数据库备份。客户端不预置服务地址或访问令牌。

## 协议 v1

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

原型暂未实现：多用户/配对、离线区间合并、Windows 原生端接入、旧历史导入和正式运维。
