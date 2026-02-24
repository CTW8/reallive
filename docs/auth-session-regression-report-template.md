# RealLive 登录与会话回归报告（模板）

关联清单：`auth-session-regression-checklist.md`

## 1. 基本信息

1. 执行日期：
2. 执行人：
3. Android 版本/机型：
4. Web 浏览器版本：
5. Server Commit：
6. Android Commit：
7. Web Commit：

## 2. 结果总览

1. A 启动与持久登录：`PASS / FAIL`
2. B 刷新令牌与 401 恢复：`PASS / FAIL`
3. C 跨页面稳定性：`PASS / FAIL`
4. D 多设备会话管理：`PASS / FAIL`
5. 发布建议：`GO / NO-GO`

## 3. 分项记录

### A. 启动与持久登录

1. A1 冷启动首次登录：`PASS / FAIL`  
   备注：
2. A2 杀进程重启免登录：`PASS / FAIL`  
   备注：
3. A3 第 29 天免登录：`PASS / FAIL`  
   备注：
4. A4 第 31 天强制登录：`PASS / FAIL`  
   备注：

### B. 刷新令牌与 401 恢复

1. B1 Watch 20 分钟稳定：`PASS / FAIL`  
   备注：
2. B2 Web 断网恢复不登出：`PASS / FAIL`  
   备注：
3. B3 access 过期可自动 refresh：`PASS / FAIL`  
   备注：
4. B4 refresh 失效后强制登录：`PASS / FAIL`  
   备注：

### C. 跨页面稳定性

1. C1 Dashboard->Watch->PTZ->Watch：`PASS / FAIL`  
   备注：
2. C2 Watch->History->Event Detail：`PASS / FAIL`  
   备注：
3. C3 Settings 全链路：`PASS / FAIL`  
   备注：
4. C4 Snapshot Gallery：`PASS / FAIL`  
   备注：

### D. 多设备会话管理

1. D1 双设备同时登录：`PASS / FAIL`  
   备注：
2. D2 A 执行 Logout Others 后 B 被踢：`PASS / FAIL`  
   备注：
3. D3 Active Sessions 展示正确：`PASS / FAIL`  
   备注：

## 4. 失败项明细

对每个失败项填写：

1. 场景编号（如 B2）
2. 复现步骤
3. 实际结果
4. 预期结果
5. 相关日志（Android/Server/Web）
6. 严重级别（P0/P1/P2）
7. 责任模块（Android/Web/Server/Pusher/Puller）

## 5. 修复跟踪

1. 修复任务链接：
2. 修复提交：
3. 回归结果：
4. 关闭时间：

