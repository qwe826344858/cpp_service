# 使用 Ubuntu 22.04 作为基础镜像
FROM ubuntu:22.04

# 避免交互式前端提示
ENV DEBIAN_FRONTEND=noninteractive

# 替换为国内源（可选，为了加速下载，这里暂时使用默认源，如果速度慢可以替换）
# RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 更新包列表并安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    gdb \
    vim \
    pkg-config \
    libboost-all-dev \
    libwebsocketpp-dev \
    libssl-dev \
    libgtest-dev \
    libbenchmark-dev \
    python3 \
    python3-pip \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 安装 ONNX Runtime (下载预编译库)
RUN wget https://github.com/microsoft/onnxruntime/releases/download/v1.14.1/onnxruntime-linux-x64-1.14.1.tgz \
    && tar -zxvf onnxruntime-linux-x64-1.14.1.tgz \
    && cp -r onnxruntime-linux-x64-1.14.1/include/* /usr/local/include/ \
    && cp -r onnxruntime-linux-x64-1.14.1/lib/* /usr/local/lib/ \
    && ldconfig \
    && rm -rf onnxruntime-linux-x64-1.14.1.tgz onnxruntime-linux-x64-1.14.1

# 安装 websocket-client 用于测试脚本
RUN pip3 install websocket-client

# 编译 Google Test (Ubuntu 的 libgtest-dev 仅包含源码)
RUN cd /usr/src/gtest && cmake . && make && cp lib/libgtest*.a /usr/lib

# 设置工作目录
WORKDIR /workspace

# 默认命令为 bash，方便进入容器进行操作
CMD ["/bin/bash"]
