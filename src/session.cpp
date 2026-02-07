#include "session.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include "json.hpp"
#include "base64.h"

using json = nlohmann::json;

// ==========================================
// Session 实现
// ==========================================

Session::Session(std::string id, websocketpp::connection_hdl hdl) 
    : id_(id), hdl_(hdl), last_state_(VadState::SILENCE) {
    // 确保 model 目录在运行时的相对路径正确 (通常是 build/../model)
    std::string model_path = "../model/silero_vad.onnx";

    // 使用 Silero VAD 引擎 (基于 ONNX Runtime)
    vad_engine_ = std::make_unique<SileroVadEngine>(model_path);
    std::cout << "[Session " << id_ << "] Created with Original VAD (SileroVadEngine)" << std::endl;
}

Session::~Session() {
    std::cout << "[Session " << id_ << "] Destroyed" << std::endl;
}

std::string Session::get_current_timestamp_us() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return std::to_string(micros);
}

std::string Session::build_vad_response(const std::string& vad_state, const std::string& audio_b64, const std::string& new_session) {
    json j = {
        {"uid", id_},
        {"connect_session", connect_session_},
        {"current_session", current_session_},
        {"data", {{"vad_state", vad_state}, {"vad_audio", audio_b64}}}
    };
    if (!new_session.empty()) {
        j["new_session"] = new_session;
    }
    return j.dump();
}

std::string Session::build_begin_response(const std::string& audio_b64) {
    return build_vad_response("VAD_BEGIN", audio_b64, new_session_);
}


std::string Session::build_end_response(const std::string& audio_b64) {
    return build_vad_response("VAD_END", audio_b64, "");
}

std::string Session::build_speaking_response(const std::string& audio_b64) {
    return build_vad_response("SPEAKING", audio_b64, "");
}

std::string Session::build_silence_response() {
    return build_vad_response("SILENCE", "", "");
}



std::string Session::process_audio(const std::vector<uint8_t>& raw_data) {
    // 1. 转码: PCM 16bit -> Float
    // raw_data 是字节流，每两个字节是一个 int16
    std::vector<float> float_audio;
    float_audio.reserve(raw_data.size() / 2);

    for (size_t i = 0; i < raw_data.size(); i += 2) {
        if (i + 1 < raw_data.size()) {
            int16_t sample = static_cast<int16_t>(raw_data[i] | (raw_data[i+1] << 8));
            float_audio.push_back(sample / 32768.0f);
        }
    }

    // 2. VAD 处理
    VadResult res = vad_engine_->process_frame(float_audio);

    // 3. 状态变更检测与消息生成
    std::string json_resp = "";
    bool state_changed = false;
    VadState current_state = res.state;

    // Handle direct transition SILENCE -> SPEAKING
    if (current_state == VadState::SPEAKING && last_state_ == VadState::SILENCE) {
        current_state = VadState::START_SPEAKING;
    }

    if (current_state == VadState::START_SPEAKING) {
        std::cout << "[Session " << id_ << "] VAD START_SPEAKING detected!" << std::endl;
        
        // Start buffering logic
        audio_buffer_.clear();
        audio_buffer_.insert(audio_buffer_.end(), raw_data.begin(), raw_data.end());
        
        // Generate new session timestamp
        new_session_ = get_current_timestamp_us();

        {
            std::string audio_b64 = base64::encode(raw_data.data(), raw_data.size());
            json_resp = build_begin_response(audio_b64);
        }
        
        last_state_ = VadState::SPEAKING;
    }
    else if (current_state == VadState::SPEAKING) {
        // Continue buffering
        audio_buffer_.insert(audio_buffer_.end(), raw_data.begin(), raw_data.end());
        
        {
            std::string audio_b64 = base64::encode(raw_data.data(), raw_data.size());
            json_resp = build_speaking_response(audio_b64);
        }

        last_state_ = VadState::SPEAKING;
    }
    else if (current_state == VadState::END_SPEAKING) {
        std::cout << "[Session " << id_ << "] VAD END_SPEAKING detected!" << std::endl;
        
        // Final buffer append
        audio_buffer_.insert(audio_buffer_.end(), raw_data.begin(), raw_data.end());

        std::string audio_b64 = base64::encode(audio_buffer_.data(), audio_buffer_.size());
        json_resp = build_end_response(audio_b64);

        // Clear buffer
        audio_buffer_.clear();
        
        last_state_ = VadState::SILENCE;
    }
    else { // SILENCE
        // Clear buffer if we were somehow buffering in silence (safety)
        if (!audio_buffer_.empty()) audio_buffer_.clear();
        json_resp = build_silence_response();
        
        last_state_ = VadState::SILENCE;
    }

    return json_resp;
}
