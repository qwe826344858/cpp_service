#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <map>
#include <memory>
#include <thread>
#include <string>

#include "session.h"
#include "safe_queue.h"

// 定义服务器类型
typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;

// 任务包
struct AudioTask {
    connection_hdl hdl;
    std::vector<uint8_t> data;
};

class AudioServer {
public:
    AudioServer();
    ~AudioServer();

    void run(uint16_t port);
    void stop();

private:
    // WebSocket 回调
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, server::message_ptr msg);

    // 工作线程逻辑
    void worker_loop();

private:
    server srv_;
    std::thread worker_thread_;
    bool running_;

    // 线程安全队列
    SafeQueue<AudioTask> task_queue_;

    // 会话管理
    typedef std::map<connection_hdl, std::shared_ptr<Session>, std::owner_less<connection_hdl>> SessionMap;
    SessionMap sessions_;
    std::mutex session_mutex_;
};
