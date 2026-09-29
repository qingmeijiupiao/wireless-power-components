# 按钮/急停公共组件迁移验证

日期：2026-09-29。

## 范围

- 新迁移：battery_level、blackbox_service、espnow_remote、espnow_service_remote。
- 统一引用：公共 espnow_link，并将任务栈改为 Kconfig 配置；消费工程保持 5120 字节。
- SH1106 未迁移，原始驱动保留于急停工程；其他产品硬件与交互组件留在本地。

## 已通过

- 原按钮完整 ESP-IDF v6.0 编译。
- 急停启用 ESTOP_VALIDATE_SHARED_REMOTE 的构建，覆盖遥控依赖；仍使用屏幕入口。
- 急停恢复 ESTOP_VALIDATE_SHARED_REMOTE=OFF 后默认屏幕构建通过。
- 主机测试运行真实 blackbox_service 源码：默认 INFO 过滤、配置标签、WARN/ERROR、ANSI 清理、递归标签排除、非法配置及重复初始化行为。
- 主机测试运行真实协议编码源码：关闭请求、开启拒绝响应、容量不足/空指针、电量边界、带负电流/温度的 40 字节实时数据报文，匹配固定字节结果。
- 对照按钮 Git HEAD：电量算法、遥控客户端、业务消息处理及 payload 编码源码内容保持一致。
- 构建元数据确认两个工程均解析到相邻公共仓库路径，SH1106 解析到急停本地目录。
- git diff --check 无格式错误。

## 边界

没有烧录、无线对端联调或深睡硬件回归。主机日志测试使用单线程平台替身，不验证 FreeRTOS 并发或 Flash 时序。
新组件仍通过相邻本地路径引用，未提交/发布；CI 发布前需先发布公共仓库并改为固定 Git 提交。
