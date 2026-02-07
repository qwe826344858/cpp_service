# C++ VAD Service

这是一个基于 C++17、WebSocket++ 和 Boost.Asio 开发的高性能语音活动检测 (VAD) 服务。本项目采用了现代化的 Docker 容器化开发流程，确保了开发与编译环境的一致性。

## 目录
- [环境要求](#环境要求)
- [快速开始](#快速开始)
  - [启动开发环境](#1-启动开发环境)
  - [编译项目](#2-编译项目)
  - [运行服务](#3-运行服务)
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
- Dockerfile 维护在 `dockerfiles/` 目录下。

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

### 4. 运行 VAD 测试

测试 VAD 模型加载与推理是否正常：

```bash
./test_vad
```

你应该会看到类似以下的输出，表明服务已在 9002 端口启动：
```
[info] asio listen on: 9002
```

## 测试

本项目提供了一个 Python 客户端脚本用于功能验证。

**注意**：你可以在宿主机运行测试脚本（需要安装 `websocket-client`），也可以在容器内运行。

在容器内运行测试：

```bash
# (确保你仍在容器内)
cd /workspace

# 运行测试客户端
python3 test/test_client.py
```

该脚本会模拟一个客户端连接到服务器，发送模拟音频数据，并接收 VAD 检测结果。

## 项目结构

```
cpp_service/
├── CMakeLists.txt       # CMake 构建定义
├── Dockerfile           # 开发环境镜像定义
├── docker-compose.yml   # 容器编排配置
├── README.md            # 项目文档
├── include/             # 头文件
│   ├── safe_queue.h     # 线程安全队列
│   ├── server.h         # WebSocket 服务类定义
│   ├── session.h        # 会话管理与 VAD 逻辑
│   └── vad_engine.h     # VAD 引擎接口
├── src/                 # 源代码
│   ├── main.cpp         # 程序入口
│   ├── server.cpp       # WebSocket 事件处理实现
│   └── session.cpp      # 音频处理与状态机实现
└── test/                # 测试脚本
    └── test_client.py   # Python 测试客户端
```

## 常见问题

**Q: 修改了代码需要重启容器吗？**
A: 不需要。代码目录是挂载进去的，你只需要在容器内重新运行 `make` 即可生效。

**Q: 如何停止开发环境？**
A: 在宿主机运行 `docker compose down`。
