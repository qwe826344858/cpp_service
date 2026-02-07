#pragma once
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include "vad_iterator.h"

// VAD 状态枚举
enum class VadState {
    SILENCE,
    START_SPEAKING,
    SPEAKING,
    END_SPEAKING
};

struct VadResult {
    VadState state;
    float probability; // 语音概率
    std::string timestamp;
};

// VAD 引擎抽象基类
class IVadEngine {
public:
    virtual ~IVadEngine() = default;

    // 输入 PCM 音频数据 (float 格式)
    // 返回本次处理的 VAD 状态结果
    virtual VadResult process_frame(const std::vector<float>& audio_frame) = 0;
    
    // 重置状态
    virtual void reset() = 0;
};

// Silero VAD 引擎实现 (适配 VadIterator)
class SileroVadEngine : public IVadEngine {
public:
    explicit SileroVadEngine(const std::string& model_path, int sample_rate = 16000, int window_frame_ms = 32) 
        : vad_iterator_(model_path, sample_rate, window_frame_ms) {
        
        // 计算需要的窗口大小 (samples)
        // VadIterator 内部: window_size_samples = windows_frame_size * (sample_rate / 1000)
        // 默认 32ms * 16 = 512 samples
        window_size_samples_ = window_frame_ms * (sample_rate / 1000);
        buffer_.reserve(window_size_samples_);
    }

    VadResult process_frame(const std::vector<float>& audio_frame) override {
        VadResult result;
        result.state = VadState::SILENCE;
        result.probability = vad_iterator_.get_last_probability();

        // 1. 将新数据添加到缓冲区
        buffer_.insert(buffer_.end(), audio_frame.begin(), audio_frame.end());

        // 2. 如果缓冲区数据足够一个窗口，进行处理
        
        bool was_triggered = vad_iterator_.is_triggered();
        bool is_triggered = was_triggered;
        
        while (buffer_.size() >= window_size_samples_) {
            // 提取一个窗口的数据
            std::vector<float> chunk(buffer_.begin(), buffer_.begin() + window_size_samples_);
            
            // 从缓冲区移除
            buffer_.erase(buffer_.begin(), buffer_.begin() + window_size_samples_);
            
            // 调用 VadIterator 处理
            vad_iterator_.predict(chunk);
            result.probability = vad_iterator_.get_last_probability();
            
            // 更新触发状态
            is_triggered = vad_iterator_.is_triggered();
            
            // 如果状态发生变化，我们可以立即决定结果
            if (!was_triggered && is_triggered) {
                result.state = VadState::START_SPEAKING;
            } else if (was_triggered && !is_triggered) {
                result.state = VadState::END_SPEAKING;
            } else if (is_triggered) {
                result.state = VadState::SPEAKING;
            }
            
            was_triggered = is_triggered;
        }

        // 如果没有状态跳变，根据当前状态返回
        if (result.state == VadState::SILENCE && is_triggered) {
            result.state = VadState::SPEAKING;
        }
        
        // Debug: force print probability for debugging
        // if (result.probability > 0.01) std::cout << "DEBUG VAD Prob: " << result.probability << " Trig: " << is_triggered << std::endl;

        // 尝试获取 timestamps 更新 (如果有)
        auto stamps = vad_iterator_.get_speech_timestamps();
        if (!stamps.empty()) {
            result.timestamp = stamps.back().c_str();
        }

        return result;
    }

    void reset() override {
        vad_iterator_.reset();
        buffer_.clear();
    }

private:
    VadIterator vad_iterator_;
    std::vector<float> buffer_;
    size_t window_size_samples_;
};
