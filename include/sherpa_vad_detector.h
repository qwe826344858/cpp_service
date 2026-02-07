#pragma once
#include <vector>
#include <string>
#include <memory>
#include <deque>
#include "vad_engine.h"
#include "vad_iterator.h"

// Go 代码中的状态枚举映射
enum class GoState {
    Inactivity,
    InactivityTransition,
    Activity
};

// 移植自 Go 的 Detector 类
// 注意：为了适配现有 C++ 架构，我们继承自 IVadEngine
// 但内部逻辑尽量保持与 Go 代码一致
class SherpaVadDetector : public IVadEngine {
public:
    explicit SherpaVadDetector(const std::string& model_path, float threshold = 0.5f, int sample_rate = 16000);
    ~SherpaVadDetector() override = default;

    // IVadEngine 接口实现
    VadResult process_frame(const std::vector<float>& audio_frame) override;
    void reset() override;

private:
    // 内部处理函数，对应 Go 的 processFrame
    // source_frame: 原始音频数据 (在此实现中我们假设 audio_frame 已经是 float 格式的 PCM)
    // 返回是否触发了关键事件 (Begin/End/Silent)
    // 为了适配 IVadEngine 的单次返回接口，我们需要将内部产生的事件转换为 VadResult
    VadResult process_internal(const std::vector<float>& frame);

    // 状态设置
    void set_state(GoState state);
    
    // 事件发射模拟 (转换为 VadResult)
    VadResult emit_voice_begin();
    VadResult emit_voice_ongoing();
    VadResult emit_voice_end();
    VadResult emit_voice_silent();

private:
    // 核心组件
    // 这里复用 VadIterator 作为底层 VAD 引擎 (对应 Go 中的 sherpa.VoiceActivityDetector)
    // 注意：Go 代码使用的是 sherpa-onnx-go，底层也是 Silero VAD
    VadIterator vad_;
    
    // 配置参数 (对应 Go 中的常量/配置)
    const int sample_rate_ = 16000;
    const int frame_duration_ms_ = 20; // FrameDuration20
    const int frame_size_samples_;     // 20ms at 16k = 320 samples
    
    const float vad_voice_begin_duration_ms_ = 250.0f;
    const float vad_voice_stop_duration_ms_ = 600.0f;
    const float max_silence_duration_ms_ = 15000.0f; // 假设默认 15s，Go 代码中是外部传入

    // 运行时状态 (对应 Go Detector 结构体字段)
    GoState state_;
    float recognition_duration_ = 0.0f; // ms
    float silence_duration_ = 0.0f;     // ms
    int64_t frame_index_ = 0;

    // 缓冲区
    // Go 中的 marginBuff (用于拼凑足够一帧的数据)
    std::vector<float> margin_buffer_;
    
    // Go 中的 fixed (FixedSizeBuffer) - 用于预卷/上下文
    // 这里简化为一个定长队列
    std::deque<float> fixed_buffer_;
    const size_t fixed_buffer_capacity_; // VADVoiceTotalBufferCapacity 对应 samples

    // Go 中的 waitBuff - 用于存储当前段的音频数据
    // 注意：在当前 C++ 架构中，Session 层负责缓存音频，
    // 但为了逻辑对齐，我们可能需要在 detector 内部也维护状态，
    // 或者仅维护状态机逻辑，音频数据交由上层处理。
    // 鉴于 Go 代码中 waitBuff 用于 emitVoiceEnd 时返回数据，
    // 我们这里暂时只维护状态逻辑，数据由 Session 的 audio_buffer_ 处理。
    // *但是在 Go 逻辑中，waitBuff 包含了 fixed_buffer 的内容（预卷）*
    // 为了精确对齐，我们可能需要通知上层“回溯”数据，或者由 Detector 内部管理这部分逻辑。
    // *决策*：IVadEngine 接口只返回状态。上层 Session 已经实现了 audio_buffer_。
    // 唯一的差异是“预卷”数据 (START_SPEAKING 时包含之前的一小段)。
    // 如果需要严格对齐，我们可以在 START_SPEAKING 时返回一个特殊标记或通过 VadResult 返回预卷时长。
    // 暂时简化：只对齐状态机逻辑。
};
