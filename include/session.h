#pragma once
#include <string>
#include <memory>
#include <vector>
#include "vad_engine.h"
#include <websocketpp/common/connection_hdl.hpp>
#include "sherpa_vad_detector.h"

class Session {
public:
    Session(std::string id, websocketpp::connection_hdl hdl);
    ~Session();

    // 处理原始字节流 (通常是 PCM 16bit Little Endian)
    // 返回 JSON 格式的通知消息，如果无状态变化则返回空字符串
    std::string process_audio(const std::vector<uint8_t>& raw_data);

    std::string get_id() const { return id_; }
    void set_id(const std::string& id) { id_ = id; }
    void set_connect_session(const std::string& s) { connect_session_ = s; }
    void set_current_session(const std::string& s) { current_session_ = s; }
    websocketpp::connection_hdl get_hdl() const { return hdl_; }

private:
    std::string get_current_timestamp_us();
    std::string build_vad_response(const std::string& vad_state, const std::string& audio_b64, const std::string& new_session);
    std::string build_begin_response(const std::string& audio_b64);
    std::string build_end_response(const std::string& audio_b64);

private:
    std::string id_;
    std::string connect_session_;
    std::string current_session_;
    std::string new_session_; // Generated at START_SPEAKING

    websocketpp::connection_hdl hdl_;
    std::unique_ptr<IVadEngine> vad_engine_;
    
    // 上一次的状态，用于检测状态跳变
    VadState last_state_;

    // In-memory buffer for VAD segments
    std::vector<uint8_t> audio_buffer_;

};
