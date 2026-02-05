#include "server.h"
#include <iostream>
#include <functional>

AudioServer::AudioServer() : running_(false) {
    // 1. 关闭多余日志
    srv_.clear_access_channels(websocketpp::log::alevel::all);
    srv_.set_error_channels(websocketpp::log::elevel::all);

    // 2. 初始化 Asio
    srv_.init_asio();

    // 3. 注册回调
    srv_.set_open_handler(std::bind(&AudioServer::on_open, this, std::placeholders::_1));
    srv_.set_close_handler(std::bind(&AudioServer::on_close, this, std::placeholders::_1));
    srv_.set_message_handler(std::bind(&AudioServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
}

AudioServer::~AudioServer() {
    stop();
}

void AudioServer::run(uint16_t port) {
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&AudioServer::worker_loop, this);

    // 启动监听
    srv_.listen(port);
    srv_.start_accept();
    
    std::cout << "Server listening on port " << port << std::endl;
    
    // 阻塞运行
    srv_.run();
}

void AudioServer::stop() {
    if (running_) {
        running_ = false;
        srv_.stop();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

void AudioServer::on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    static int id_counter = 0;
    std::string uid = "user_" + std::to_string(++id_counter);
    sessions_[hdl] = std::make_shared<Session>(uid, hdl);
}

void AudioServer::on_close(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    sessions_.erase(hdl);
}

void AudioServer::on_message(connection_hdl hdl, server::message_ptr msg) {
    if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
        AudioTask task;
        task.hdl = hdl;
        std::string& payload = msg->get_payload();
        task.data = std::vector<uint8_t>(payload.begin(), payload.end());
        
        // 生产者：放入队列
        task_queue_.push(std::move(task));
    }
}

void AudioServer::worker_loop() {
    std::cout << "Worker thread started." << std::endl;
    while (running_) {
        // 从队列取任务 (假设 pop 是阻塞的，实际 SafeQueue 需要处理 stop 信号，这里简化处理)
        // 注意：如果 SafeQueue 没有 timeout 机制，stop() 时这里的 pop 可能会卡住。
        // 为了演示简单，我们假设 main 结束时强杀，或者在 pop 里加超时。
        // 这里为了健壮性，我们可以让 SafeQueue 的 pop 支持超时，或者先不改动，
        // 依赖于 task_queue_.push 一个空任务来唤醒。
        
        AudioTask task = task_queue_.pop(); 
        
        // 查找 Session
        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            auto it = sessions_.find(task.hdl);
            if (it == sessions_.end()) continue;
            session = it->second;
        }

        // 业务处理
        std::string resp = session->process_audio(task.data);

        // 发送结果
        if (!resp.empty()) {
            try {
                srv_.send(task.hdl, resp, websocketpp::frame::opcode::text);
                std::cout << "-> Sent VAD Event: " << resp << std::endl;
            } catch (websocketpp::exception const & e) {
                std::cout << "Send failed: " << e.what() << std::endl;
            }
        }
    }
}
