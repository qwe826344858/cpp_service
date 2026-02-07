# C++ VAD Service

这是一个基于 C++17、WebSocket++ 和 Boost.Asio 开发的高性能语音活动检测 (VAD) 服务。本项目采用了现代化的 Docker 容器化开发流程，确保了开发与编译环境的一致性。

核心功能：
- 基于 Silero VAD (ONNX Runtime) 的语音检测
- WebSocket 实时流式音频处理
- 实时 VAD 状态反馈（开始说话、说话中、结束说话）
- 自动音频分段与回传（Base64 编码）

## 目录
- [环境要求](#环境要求)
- [快速开始](#快速开始)
  - [启动开发环境](#1-启动开发环境)
  - [编译项目](#2-编译项目)
  - [运行服务](#3-运行服务)
- [WebSocket 协议](#websocket-协议)
- [测试](#测试)
- [项目结构](#项目结构)

## 环境要求

- **Docker Desktop** (推荐最新版本)
- **Docker Compose**

无需在本地安装 C++ 编译器或 Boost 库，所有依赖均封装在 Docker 镜像中。

## 快速开始

### 1. 启动开发环境

在项目根目录下，使用 Docker Compose 启动开发容器：

```bash
docker compose up -d --build
```

该命令会构建一个包含所有必要依赖（Boost, WebSocket++, CMake, GTest, ONNX Runtime 等）的镜像，并启动容器。
- 源代码目录会自动挂载到容器内的 `/workspace`。
- 服务端口 `9002` 已映射到主机。

### 2. 编译项目

进入运行中的容器：

```bash
docker compose exec dev bash
```

在容器内的终端中，执行标准的 CMake 构建流程：

```bash
# 创建并进入构建目录
mkdir -p build && cd build

# 生成 Makefile
cmake ..

# 开始编译 (使用多核加速)
make -j$(nproc)
```

### 3. 运行服务

编译成功后，直接运行生成的可执行文件：

```bash
./vad_server
```

你应该会看到类似以下的输出，表明服务已在 9002 端口启动：
```
[info] asio listen on: 9002
```

## WebSocket 协议

客户端通过 WebSocket 连接到 `ws://localhost:9002`。

### 发送数据
客户端发送 16kHz、16-bit、单声道 PCM 原始音频数据的二进制流。

### 接收数据
服务端返回 JSON 格式的消息，包含 VAD 状态和处理后的音频数据。

**通用响应格式**：
```json
{
    "uid": "会话ID",
    "connect_session": "连接ID",
    "current_session": "当前处理会话时间戳",
    "new_session": "新会话时间戳 (仅 VAD_BEGIN 时存在)",
    "data": {
        "vad_state": "状态枚举",
        "vad_audio": "Base64编码的PCM音频数据"
    }
}
```

**状态说明**：
1.  **VAD_BEGIN**: 检测到语音开始。
    - `vad_audio`: 包含触发 VAD 的首个音频块。
    - `new_session`: 包含新语音段的起始时间戳。
2.  **SPEAKING**: 语音持续中。
    - `vad_audio`: 包含当前的音频块（流式传输）。
3.  **END_SPEAKING**: 检测到语音结束。
    - `vad_audio`: 包含该语音段的完整累积音频。
4.  **SILENCE**: 静音状态。
    - `vad_audio`: 空字符串。

## 测试

本项目提供 Python 脚本用于验证 VAD 功能。

### 1. 生成测试音频
首先生成一个包含静音和语音的测试文件 `test_long.pcm`：

```bash
python3 gen_test_audio.py
```

### 2. 运行测试客户端
运行客户端脚本，它会读取 `test_long.pcm` 并发送给服务器：

```bash
python3 test/test_client.py
```

该脚本会输出服务器返回的 VAD 状态日志，例如：
```
< Received: {"data":{"vad_state":"VAD_BEGIN", ...}}
< Received: {"data":{"vad_state":"SPEAKING", ...}}
< Received: {"data":{"vad_state":"VAD_END", ...}}
```

## 项目结构

```
cpp_service/
├── CMakeLists.txt       # CMake 构建定义
├── Dockerfile           # 开发环境镜像定义
├── docker-compose.yml   # 容器编排配置
├── README.md            # 项目文档
├── gen_test_audio.py    # 测试音频生成脚本
├── include/             # 头文件
│   ├── safe_queue.h     # 线程安全队列
│   ├── server.h         # WebSocket 服务类定义
│   ├── session.h        # 会话管理与 VAD 逻辑
│   ├── vad_engine.h     # VAD 引擎接口
│   └── sherpa_vad_detector.h # (保留) Ported VAD 引擎
├── src/                 # 源代码
│   ├── main.cpp         # 程序入口
│   ├── server.cpp       # WebSocket 事件处理实现
│   ├── session.cpp      # 音频处理与状态机实现
│   └── sherpa_vad_detector.cpp # (保留) Ported VAD 实现
└── test/                # 测试脚本
    └── test_client.py   # Python 测试客户端
```

## 常见问题

**Q: 修改了代码需要重启容器吗？**
A: 不需要。代码目录是挂载进去的，你只需要在容器内重新运行 `make` 即可生效。

**Q: 如何停止开发环境？**
A: 在宿主机运行 `docker compose down`。
