#include "server.h"
#include <iostream>
#include <functional>
#include "json.hpp"
#include "base64.h"

using json = nlohmann::json;

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
    if (msg->get_opcode() == websocketpp::frame::opcode::text) {
        try {
            auto payload = msg->get_payload();
            auto j = json::parse(payload);
            
            // Expected format:
            // { 
            //     "uid":"abc_123", 
            //     "connect_session":"...", 
            //     "current_session":"...", 
            //     "data":{ "bussin":{}, "audio":"base64..." } 
            // }

            if (j.contains("data") && j["data"].contains("audio")) {
                std::string audio_b64 = j["data"]["audio"];
                std::vector<uint8_t> audio_data = base64::decode(audio_b64);
                
                AudioTask task;
                task.hdl = hdl;
                task.data = std::move(audio_data);
                
                if (j.contains("uid")) task.uid = j["uid"];
                if (j.contains("connect_session")) task.connect_session = j["connect_session"];
                if (j.contains("current_session")) task.current_session = j["current_session"];

                task_queue_.push(std::move(task));
            }
        } catch (std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        }
    }
    else if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
        AudioTask task;
        task.hdl = hdl;
        const std::string& payload = msg->get_payload();
        task.data = std::vector<uint8_t>(payload.begin(), payload.end());
        task_queue_.push(std::move(task));
    }
}

void AudioServer::worker_loop() {
    std::cout << "Worker thread started." << std::endl;
    while (running_) {
        // 从队列取任务
        AudioTask task = task_queue_.pop(); 
        
        // 查找 Session
        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            auto it = sessions_.find(task.hdl);
            if (it == sessions_.end()) continue;
            session = it->second;
        }

        // Update Metadata
        if (!task.uid.empty() && session->get_id() != task.uid) {
             std::cout << "[Session " << session->get_id() << "] Updating UID to " << task.uid << std::endl;
             session->set_id(task.uid);
        }
        if (!task.connect_session.empty()) {
            session->set_connect_session(task.connect_session);
        }
        if (!task.current_session.empty()) {
            session->set_current_session(task.current_session);
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
