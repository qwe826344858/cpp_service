#include "session.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <sstream>

// ==========================================
// Mock VAD Engine 实现 (基于能量检测)
// ==========================================
class MockVadEngine : public IVadEngine {
public:
    VadResult process_frame(const std::vector<float>& audio_frame) override {
        // 1. 计算 RMS (均方根) 能量
        float sum_squares = 0.0f;
        for (float sample : audio_frame) {
            sum_squares += sample * sample;
        }
        float rms = std::sqrt(sum_squares / audio_frame.size());

        // 2. 简单的阈值判断
        // 假设静音环境 RMS < 0.01, 说话时通常 > 0.05
        const float SPEECH_THRESHOLD = 0.02f; 
        
        VadResult result;
        result.probability = rms; // 用 RMS 模拟概率

        // 简单的状态机去抖动 (实际 Silero 会复杂得多)
        if (rms > SPEECH_THRESHOLD) {
            if (speech_frames_count_ < 5) {
                speech_frames_count_++;
                result.state = (speech_frames_count_ >= 3) ? VadState::START_SPEAKING : VadState::SILENCE;
            } else {
                result.state = VadState::SPEAKING;
            }
            silence_frames_count_ = 0;
        } else {
            if (speech_frames_count_ > 0) {
                silence_frames_count_++;
                if (silence_frames_count_ > 10) { // 连续 10 帧静音才算结束
                    result.state = VadState::END_SPEAKING;
                    speech_frames_count_ = 0;
                } else {
                    result.state = VadState::SPEAKING; // 保持说话状态 (Hangover)
                }
            } else {
                result.state = VadState::SILENCE;
            }
        }
        
        // 模拟计算耗时
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        return result;
    }

    void reset() override {
        speech_frames_count_ = 0;
        silence_frames_count_ = 0;
    }

private:
    int speech_frames_count_ = 0;
    int silence_frames_count_ = 0;
};

// ==========================================
// Session 实现
// ==========================================

Session::Session(std::string id, websocketpp::connection_hdl hdl) 
    : id_(id), hdl_(hdl), last_state_(VadState::SILENCE) {
    vad_engine_ = std::make_unique<MockVadEngine>();
    std::cout << "[Session " << id_ << "] Created" << std::endl;
}

Session::~Session() {
    std::cout << "[Session " << id_ << "] Destroyed" << std::endl;
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
    
    // 只有状态发生重要变化时才通知 (START, END)
    // 或者如果在 SPEAKING 状态，也可以定期发中间状态
    if (res.state != last_state_) {
        std::stringstream ss;
        ss << "{";
        ss << "\"uid\": \"" << id_ << "\", ";
        
        if (res.state == VadState::START_SPEAKING) {
            ss << "\"event\": \"vad_start\", \"prob\": " << res.probability;
            json_resp = ss.str() + "}";
            last_state_ = VadState::SPEAKING; // 更新为 SPEAKING 以便后续保持
        } 
        else if (res.state == VadState::END_SPEAKING) {
            ss << "\"event\": \"vad_end\", \"prob\": " << res.probability;
            json_resp = ss.str() + "}";
            last_state_ = VadState::SILENCE;
        }
        // 处理从 SILENCE -> SPEAKING 的直接跳变 (如果 Mock 逻辑太快)
        else if (res.state == VadState::SPEAKING && last_state_ == VadState::SILENCE) {
             ss << "\"event\": \"vad_start\", \"prob\": " << res.probability;
             json_resp = ss.str() + "}";
             last_state_ = VadState::SPEAKING;
        }
    }

    return json_resp;
}
