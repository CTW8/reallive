# RealLive 登录与会话体验回归清单

更新时间：2026-02-24

适用范围：

- Android App（`android/`）
- Web Console（`server/web/`）
- Server Auth API（`server/src/routes/auth.js`）
- 回归记录模板：`auth-session-regression-report-template.md`

## 1. 目标

验证以下体验是否成立：

1. 用户登录后会话可持续（30 天有效期内无需重复登录）。
2. 临时网络波动或瞬时 401 不会把用户直接踢回登录页。
3. 仅在 refresh token 或会话明确失效时，才强制回登录页。
4. “登出其他设备”与“当前设备保持在线”行为正确。

## 2. 前置条件

1. Server、Web、Android 均使用最新代码构建。
2. 测试账号至少 1 个，建议准备 2 台设备（或 Android + Web）。
3. 确认服务端时间正确（避免 token 过期判断偏差）。

## 3. 回归矩阵（必须通过）

### A. 启动与持久登录

1. 冷启动 Android，首次登录成功后进入 Dashboard。
   预期：成功进入主页，token/refreshToken 已持久化。
2. 杀掉 App 并立即重开。
   预期：直接进入 Splash->Dashboard，不出现登录页。
3. 将系统时间推进到登录后第 29 天再启动。
   预期：仍可自动进入主页。
4. 将系统时间推进到登录后第 31 天再启动。
   预期：进入登录页（force reauth）。

### B. 刷新令牌与 401 恢复

1. Android 保持 Watch 页面在线 20 分钟。
   预期：不中断、不跳登录；若中间发生 401，页面可自动恢复。
2. Web 打开监控页面后断网 30 秒再恢复。
   预期：恢复后自动继续请求；不会因为一次刷新失败直接登出。
3. 人工使 access token 失效（保留 refresh token）。
   预期：请求触发 refresh，成功后继续使用，无登录页闪退。
4. 人工使 refresh token 无效（删除/篡改）。
   预期：下一次 401 后会强制回登录页。

### C. 跨页面稳定性（Android）

在已登录状态下依次进入：

1. Dashboard -> Watch -> PTZ -> 返回 Watch
2. Watch -> History -> Event Detail
3. Settings -> Security -> TwoFactor -> Upgrade Plan -> Storage -> Profile
4. Snapshot Gallery

预期：

1. 页面切换过程中出现瞬时接口失败不应直接清登录。
2. 仅当会话校验明确失败时才跳转登录。

### D. 多设备会话管理

1. 设备 A 与设备 B 同账号登录。
2. 在设备 A 执行“Logout Others / 登出其他设备”。
   预期：设备 A 继续在线；设备 B 在下一次鉴权请求后被登出。
3. 设备 A 打开 Active Sessions。
   预期：当前设备标记正确，数量与后台一致。

## 4. 失败判定

以下任一出现判定为失败：

1. 网络抖动后立即清空登录态并跳登录页。
2. refresh 端点 5xx/超时时，仍被强制登出。
3. 会话有效期内（<30 天）每次重启都要求重新登录。
4. 登出其他设备后，当前设备也被误踢。

## 5. 诊断日志建议

Android 关键日志：

- `WatchTelemetry`
- `RealLiveNativePlayer`
- `okhttp.OkHttpClient`

Server 关键日志：

- `POST /api/auth/refresh` 返回码
- `GET /api/auth/sessions`
- `POST /api/auth/sessions/revoke-others`

## 6. 回归结论模板

建议每次提交后记录：

1. 构建版本（Android/Web/Server commit）
2. 回归通过项数量（A/B/C/D）
3. 失败项与复现步骤
4. 是否允许发布（Yes/No）
