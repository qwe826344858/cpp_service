#pragma once
#include <vector>
#include <string>

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

    // 输入 PCM 音频数据 (假设是 float 格式，归一化到 -1.0 ~ 1.0)
    // 返回本次处理的 VAD 状态结果
    virtual VadResult process_frame(const std::vector<float>& audio_frame) = 0;
    
    // 重置状态
    virtual void reset() = 0;
};
