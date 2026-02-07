---
name: "cpp-vad-service"
description: "管理 C++ VAD (语音活动检测) 服务。当用户需要启动、停止、测试或检查 VAD 服务状态时调用此技能。"
---

# C++ VAD 服务管理技能

本技能允许你管理高性能的 C++ 语音活动检测 (VAD) 服务。该服务基于 Silero VAD (ONNX)，通过 WebSocket 对实时音频流进行语音检测。

## 功能列表

1.  **启动服务**: 使用 Docker Compose 启动 VAD 服务。
2.  **停止服务**: 停止 VAD 服务。
3.  **运行测试**: 执行 Python 测试客户端以验证 VAD 功能。
4.  **查看日志**: 检查服务运行日志。

## 使用指南

### 1. 启动服务
使用以下命令在后台构建并启动服务：
```bash
docker compose up -d --build
```
*启动后请等待几秒钟让服务完成初始化。*

### 2. 检查服务状态
验证容器是否正在运行：
```bash
docker ps | grep cpp_service
```

### 3. 运行测试
**前提条件**: 服务必须处于运行状态（参考"启动服务"）。

运行内置的 Python 测试客户端（发送 `test_long.pcm`）：
```bash
python3 test/test_client.py
```
*预期输出*: 你应该在输出中看到 `VAD_BEGIN`、`SPEAKING` 和 `END_SPEAKING` 的 JSON 消息。

### 4. 查看日志
调试或查看服务端处理日志：
```bash
docker compose logs -f vad_server
```

### 5. 停止服务
清理资源并停止服务：
```bash
docker compose down
```

## 协议概览
- **URL**: `ws://localhost:9002`
- **输入**: 16kHz, 16-bit, 单声道 PCM 二进制流。
- **输出**: JSON 消息。
  - `VAD_BEGIN`: 检测到语音开始 (包含 `new_session` 时间戳 + 音频数据)。
  - `SPEAKING`: 语音持续中 (包含当前音频块)。
  - `END_SPEAKING`: 语音结束 (包含该段完整音频)。
  - `SILENCE`: 静音状态 (无音频)。
