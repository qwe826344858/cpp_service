#pragma once
#include <string>
#include <memory>
#include <vector>
#include "vad_engine.h"
#include <websocketpp/common/connection_hdl.hpp>

class Session {
public:
    Session(std::string id, websocketpp::connection_hdl hdl);
    ~Session();

    // 处理原始字节流 (通常是 PCM 16bit Little Endian)
    // 返回 JSON 格式的通知消息，如果无状态变化则返回空字符串
    std::string process_audio(const std::vector<uint8_t>& raw_data);

    std::string get_id() const { return id_; }
    websocketpp::connection_hdl get_hdl() const { return hdl_; }

private:
    std::string id_;
    websocketpp::connection_hdl hdl_;
    std::unique_ptr<IVadEngine> vad_engine_;
    
    // 上一次的状态，用于检测状态跳变
    VadState last_state_;
};
