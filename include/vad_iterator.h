#ifndef VAD_ITERATOR_H
#define VAD_ITERATOR_H

#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <cstdarg>
#include <cstdio>
#include "onnxruntime_cxx_api.h"

class timestamp_t {
public:
    int start;
    int end;

    timestamp_t(int start = -1, int end = -1);
    timestamp_t& operator=(const timestamp_t& a);
    bool operator==(const timestamp_t& a) const;
    std::string c_str() const;
private:
    std::string format(const char* fmt, ...) const;
};

class VadIterator {
private:
    // ONNX Runtime resources
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::shared_ptr<Ort::Session> session = nullptr;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

    const int context_samples = 64;
    std::vector<float> _context;
    int window_size_samples;
    int effective_window_size;
    int sr_per_ms;

    std::vector<Ort::Value> ort_inputs;
    std::vector<const char*> input_node_names = { "input", "state", "sr" };
    std::vector<float> input;
    unsigned int size_state = 2 * 1 * 128;
    std::vector<float> _state;
    std::vector<int64_t> sr;
    int64_t input_node_dims[2] = {};
    const int64_t state_node_dims[3] = { 2, 1, 128 };
    const int64_t sr_node_dims[1] = { 1 };
    std::vector<Ort::Value> ort_outputs;
    std::vector<const char*> output_node_names = { "output", "stateN" };

    int sample_rate;
    float threshold;
    int min_silence_samples;
    int min_silence_samples_at_max_speech;
    int min_speech_samples;
    float max_speech_samples;
    int speech_pad_samples;
    int audio_length_samples;

    bool triggered = false;
    unsigned int temp_end = 0;
    unsigned int current_sample = 0;
    int prev_end;
    int next_start = 0;
    std::vector<timestamp_t> speeches;
    timestamp_t current_speech;
    
    float last_prob = 0.0f;

    void init_onnx_model(const std::string& model_path);
    void init_engine_threads(int inter_threads, int intra_threads);
    void reset_states();
    
public:
    void predict(const std::vector<float>& data_chunk);
    bool is_triggered() const { return triggered; }

    VadIterator(const std::string ModelPath,
        int Sample_rate = 16000, int windows_frame_size = 32,
        float Threshold = 0.5, int min_silence_duration_ms = 100,
        int speech_pad_ms = 30, int min_speech_duration_ms = 250,
        float max_speech_duration_s = std::numeric_limits<float>::infinity());

    void process(const std::vector<float>& input_wav);
    const std::vector<timestamp_t> get_speech_timestamps() const;
    float get_last_probability() const { return last_prob; }
    void reset();
};

#endif // VAD_ITERATOR_H
