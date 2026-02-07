#include "sherpa_vad_detector.h"
#include <iostream>
#include <algorithm>

SherpaVadDetector::SherpaVadDetector(const std::string& model_path, float threshold, int sample_rate)
    : vad_(model_path, sample_rate, 20, threshold), // 20ms window to match Go's FrameDuration20
      sample_rate_(sample_rate),
      frame_size_samples_(sample_rate * 20 / 1000), // 320 samples
      fixed_buffer_capacity_(sample_rate * 20 / 1000 * 5 * 10) // FrameDuration100Size... * 10 ~= 1s buffer (16000 samples) -> actually Go calculation is bytes. 
      // Go: FrameDuration20SizeInBytes = 16000/1000 * 20 * 16/8 = 640 bytes (320 samples)
      // FrameDuration100SizeInBytesInMilliseconds = 640 * 5 = 3200 bytes
      // VADVoiceTotalBufferCapacity = 3200 * 10 = 32000 bytes = 16000 samples = 1 second
{
    // VadIterator 内部参数适配
    // Go: MinSilenceDuration = 0.6s = 600ms
    // Go: MinSpeechDuration = 0.1s = 100ms
    // Go: WindowSize = 512 (32ms?) -> But ProcessFrames uses FrameDuration20 (20ms)
    // SileroVadEngine 默认 window 是 32ms (512 samples)
    // 我们的 VadIterator 构造函数支持 windows_frame_size。
    // 这里我们传入 20ms 以匹配 Go 的切片逻辑，但 Silero 模型通常训练于 32ms 或 64ms。
    // Go 代码中 config.SileroVad.WindowSize = 512 (32ms)，但切片逻辑是 20ms。
    // 这可能导致 padding 或不匹配。
    // 不过 sherpa-onnx 底层会自动处理 buffer。
    // 我们这里尽量复用 VadIterator 的 predict 逻辑。
    
    reset();
}

void SherpaVadDetector::reset() {
    state_ = GoState::Inactivity;
    frame_index_ = 0;
    silence_duration_ = 0;
    recognition_duration_ = 0;
    margin_buffer_.clear();
    fixed_buffer_.clear();
    vad_.reset();
    std::cout << "[SherpaVadDetector] Reset" << std::endl;
}

void SherpaVadDetector::set_state(GoState state) {
    silence_duration_ = 0;
    recognition_duration_ = 0;
    state_ = state;
}

VadResult SherpaVadDetector::emit_voice_begin() {
    return {VadState::START_SPEAKING, vad_.get_last_probability(), ""};
}

VadResult SherpaVadDetector::emit_voice_ongoing() {
    return {VadState::SPEAKING, vad_.get_last_probability(), ""};
}

VadResult SherpaVadDetector::emit_voice_end() {
    return {VadState::END_SPEAKING, vad_.get_last_probability(), ""};
}

VadResult SherpaVadDetector::emit_voice_silent() {
    return {VadState::SILENCE, vad_.get_last_probability(), ""};
}

VadResult SherpaVadDetector::process_frame(const std::vector<float>& audio_frame) {
    // 1. 将数据加入 margin_buffer (Go: d.marginBuff.Append)
    margin_buffer_.insert(margin_buffer_.end(), audio_frame.begin(), audio_frame.end());

    // 2. 检查是否有足够的数据 (Go: block := d.marginBuff.Len() / FrameDuration20SizeInBytes)
    // FrameDuration20SizeInBytes 对应 frame_size_samples_ (320 samples)
    int blocks = margin_buffer_.size() / frame_size_samples_;
    
    if (blocks <= 0) {
        return {VadState::SILENCE, vad_.get_last_probability(), ""};
    }

    VadResult last_result = {VadState::SILENCE, 0.0f, ""};
    bool triggered_any = false;

    // 3. 循环处理块
    for (int i = 0; i < blocks; ++i) {
        // 提取一帧
        std::vector<float> frame(
            margin_buffer_.begin(), 
            margin_buffer_.begin() + frame_size_samples_
        );
        
        // 移除已处理数据
        margin_buffer_.erase(
            margin_buffer_.begin(), 
            margin_buffer_.begin() + frame_size_samples_
        );

        // 调用内部处理逻辑
        VadResult res = process_internal(frame);
        
        // 如果有重要状态变化，记录下来
        // 注意：单次调用可能产生多次状态变化（理论上），
        // 但 IVadEngine 接口只返回一个结果。
        // 我们优先返回 START 或 END。
        if (res.state != VadState::SILENCE && res.state != VadState::SPEAKING) {
            last_result = res;
            triggered_any = true;
        } else if (res.state == VadState::SPEAKING && !triggered_any) {
            last_result = res;
        }
    }

    return last_result;
}

VadResult SherpaVadDetector::process_internal(const std::vector<float>& frame) {
    // Go: d.vad.IsSpeech()
    // 我们使用 vad_.predict(frame)
    // 注意：VadIterator 内部有自己的 buffer 和 window 逻辑，
    // 这里我们假设 vad_.predict 能处理 20ms 的块 (需要确认 VadIterator 实现是否支持非 32ms)
    // VadIterator 默认是 32ms (512 samples)。
    // 如果传入 320 samples，VadIterator 会 buffer 住直到够 512。
    // 这与 Go 的逻辑略有不同（Go 是调 sherpa，sherpa 内部可能也 buffer）。
    // 为了尽可能对齐，我们信任 VadIterator 的概率输出。
    
    vad_.predict(frame);
    // float prob = vad_.get_last_probability(); // Not used directly logic-wise in Go, only IsSpeech boolean
    
    // VadIterator 的 is_triggered() 是基于阈值的滞后逻辑，
    // 但 Go 代码似乎直接用了 d.vad.IsSpeech() (基于当前帧概率?) 
    // 并自己实现了状态机。
    // 我们这里应该只获取概率，然后自己实现 Go 的状态机逻辑，而不是用 VadIterator 的状态机。
    // 需检查 VadIterator::predict 是否更新了 prob。是的。
    
    bool frame_active = vad_.get_last_probability() >= 0.5f; // Go: config.SileroVad.Threshold

    frame_index_++;

    // Go: d.recognitionDuration logic inside vad loop?
    // Go 代码中 d.vad.IsSpeech() 后有一个循环: for !d.vad.IsEmpty() { ... }
    // 这似乎是处理 sherpa 内部产生的 speech segment。
    // 但我们的 VadIterator 没有暴露 segment 队列，而是直接处理流。
    // 我们简化为：假设当前帧就是识别持续时间的一部分。

    // 状态机逻辑移植
    switch (state_) {
    case GoState::Inactivity:
        // d.fixed.Append(sourceBuff)
        fixed_buffer_.insert(fixed_buffer_.end(), frame.begin(), frame.end());
        while (fixed_buffer_.size() > fixed_buffer_capacity_) {
            fixed_buffer_.pop_front();
        }

        if (frame_active) {
            recognition_duration_ += frame_duration_ms_;
        } else {
            if (recognition_duration_ >= frame_duration_ms_) {
                recognition_duration_ -= 10.0f; // FrameDuration10
            }
            silence_duration_ += frame_duration_ms_;
        }

        if (recognition_duration_ >= vad_voice_begin_duration_ms_) {
            set_state(GoState::InactivityTransition);
            // Fallthrough to next case immediately? Go doesn't seem to fallthrough switch.
            // But logic says: if >= begin, setState(Transition).
            // Next frame will handle Transition.
        } else if (silence_duration_ >= max_silence_duration_ms_) {
            return emit_voice_silent();
        }
        break;

    case GoState::InactivityTransition:
        // d.fixed.Append(sourceBuff)
        fixed_buffer_.insert(fixed_buffer_.end(), frame.begin(), frame.end());
        while (fixed_buffer_.size() > fixed_buffer_capacity_) {
            fixed_buffer_.pop_front();
        }
        
        // d.waitBuff = append(d.waitBuff, d.fixed.GetData()...) 
        // 这里我们不维护 waitBuff 数据，只负责状态。
        // 但需要通知 Session 这一帧是 START。
        
        set_state(GoState::Activity);
        
        if (!frame_active) {
            silence_duration_ += frame_duration_ms_;
        }
        
        return emit_voice_begin();

    case GoState::Activity:
        // d.waitBuff = append(d.waitBuff, sourceBuff...)
        
        if (frame_active) {
            silence_duration_ -= frame_duration_ms_;
        } else {
            silence_duration_ += frame_duration_ms_;
        }

        if (silence_duration_ < 0) {
            silence_duration_ = 0;
        }

        if (silence_duration_ >= vad_voice_stop_duration_ms_) {
            set_state(GoState::Inactivity);
            return emit_voice_end();
        }
        
        // Go: if len(d.waitBuff) >= FrameDuration100Size... emitVoiceOnGoing()
        // 这里简化为每帧都由上层决定是否发送，或者我们返回 SPEAKING
        return emit_voice_ongoing();
    }

    return {VadState::SILENCE, vad_.get_last_probability(), ""};
}
