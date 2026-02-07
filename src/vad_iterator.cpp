#include "vad_iterator.h"
#include <cstring>
#include <iostream>
#include <cmath>

// timestamp_t implementation
timestamp_t::timestamp_t(int start, int end)
    : start(start), end(end) { }

timestamp_t& timestamp_t::operator=(const timestamp_t& a) {
    start = a.start;
    end = a.end;
    return *this;
}

bool timestamp_t::operator==(const timestamp_t& a) const {
    return (start == a.start && end == a.end);
}

std::string timestamp_t::c_str() const {
    return format("{start:%08d, end:%08d}", start, end);
}

std::string timestamp_t::format(const char* fmt, ...) const {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    const auto r = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (r < 0)
        return {};
    const size_t len = r;
    if (len < sizeof(buf))
        return std::string(buf, len);
#if __cplusplus >= 201703L
    std::string s(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(s.data(), len + 1, fmt, args);
    va_end(args);
    return s;
#else
    auto vbuf = std::unique_ptr<char[]>(new char[len + 1]);
    va_start(args, fmt);
    std::vsnprintf(vbuf.get(), len + 1, fmt, args);
    va_end(args);
    return std::string(vbuf.get(), len);
#endif
}

// VadIterator implementation
VadIterator::VadIterator(const std::string ModelPath,
    int Sample_rate, int windows_frame_size,
    float Threshold, int min_silence_duration_ms,
    int speech_pad_ms, int min_speech_duration_ms,
    float max_speech_duration_s)
    : sample_rate(Sample_rate), threshold(Threshold), speech_pad_samples(speech_pad_ms), prev_end(0)
{
    sr_per_ms = sample_rate / 1000;
    window_size_samples = windows_frame_size * sr_per_ms;
    effective_window_size = window_size_samples + context_samples;
    input_node_dims[0] = 1;
    input_node_dims[1] = effective_window_size;
    _state.resize(size_state);
    sr.resize(1);
    sr[0] = sample_rate;
    _context.assign(context_samples, 0.0f);
    min_speech_samples = sr_per_ms * min_speech_duration_ms;
    max_speech_samples = (sample_rate * max_speech_duration_s - window_size_samples - 2 * speech_pad_samples);
    min_silence_samples = sr_per_ms * min_silence_duration_ms;
    min_silence_samples_at_max_speech = sr_per_ms * 98;
    init_onnx_model(ModelPath);
}

void VadIterator::init_onnx_model(const std::string& model_path) {
    init_engine_threads(1, 1);
    session = std::make_shared<Ort::Session>(env, model_path.c_str(), session_options);
}

void VadIterator::init_engine_threads(int inter_threads, int intra_threads) {
    session_options.SetIntraOpNumThreads(intra_threads);
    session_options.SetInterOpNumThreads(inter_threads);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

void VadIterator::reset_states() {
    std::memset(_state.data(), 0, _state.size() * sizeof(float));
    triggered = false;
    temp_end = 0;
    current_sample = 0;
    prev_end = next_start = 0;
    speeches.clear();
    current_speech = timestamp_t();
    std::fill(_context.begin(), _context.end(), 0.0f);
}

void VadIterator::predict(const std::vector<float>& data_chunk) {
    std::vector<float> new_data(effective_window_size, 0.0f);
    std::copy(_context.begin(), _context.end(), new_data.begin());
    std::copy(data_chunk.begin(), data_chunk.end(), new_data.begin() + context_samples);
    input = new_data;

    Ort::Value input_ort = Ort::Value::CreateTensor<float>(
        memory_info, input.data(), input.size(), input_node_dims, 2);
    Ort::Value state_ort = Ort::Value::CreateTensor<float>(
        memory_info, _state.data(), _state.size(), state_node_dims, 3);
    Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(
        memory_info, sr.data(), sr.size(), sr_node_dims, 1);
    ort_inputs.clear();
    ort_inputs.emplace_back(std::move(input_ort));
    ort_inputs.emplace_back(std::move(state_ort));
    ort_inputs.emplace_back(std::move(sr_ort));

    ort_outputs = session->Run(
        Ort::RunOptions{ nullptr },
        input_node_names.data(), ort_inputs.data(), ort_inputs.size(),
        output_node_names.data(), output_node_names.size());

    float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
    last_prob = speech_prob;
    float* stateN = ort_outputs[1].GetTensorMutableData<float>();
    std::memcpy(_state.data(), stateN, size_state * sizeof(float));
    current_sample += static_cast<unsigned int>(window_size_samples);

    if (speech_prob >= threshold) {
        if (temp_end != 0) {
            temp_end = 0;
            if (next_start < prev_end)
                next_start = current_sample - window_size_samples;
        }
        if (!triggered) {
            triggered = true;
            current_speech.start = current_sample - window_size_samples;
        }
        std::copy(new_data.end() - context_samples, new_data.end(), _context.begin());
        return;
    }

    if (triggered && ((current_sample - current_speech.start) > max_speech_samples)) {
        if (prev_end > 0) {
            current_speech.end = prev_end;
            speeches.push_back(current_speech);
            current_speech = timestamp_t();
            if (next_start < prev_end)
                triggered = false;
            else
                current_speech.start = next_start;
            prev_end = 0;
            next_start = 0;
            temp_end = 0;
        }
        else {
            current_speech.end = current_sample;
            speeches.push_back(current_speech);
            current_speech = timestamp_t();
            prev_end = 0;
            next_start = 0;
            temp_end = 0;
            triggered = false;
        }
        std::copy(new_data.end() - context_samples, new_data.end(), _context.begin());
        return;
    }

    if ((speech_prob >= (threshold - 0.15)) && (speech_prob < threshold)) {
        std::copy(new_data.end() - context_samples, new_data.end(), _context.begin());
        return;
    }

    if (speech_prob < (threshold - 0.15)) {
        if (triggered) {
            if (temp_end == 0)
                temp_end = current_sample;
            if (current_sample - temp_end > min_silence_samples_at_max_speech)
                prev_end = temp_end;
            if ((current_sample - temp_end) >= min_silence_samples) {
                current_speech.end = temp_end;
                if (current_speech.end - current_speech.start > min_speech_samples) {
                    speeches.push_back(current_speech);
                    current_speech = timestamp_t();
                    prev_end = 0;
                    next_start = 0;
                    temp_end = 0;
                    triggered = false;
                }
            }
        }
        std::copy(new_data.end() - context_samples, new_data.end(), _context.begin());
        return;
    }
}

void VadIterator::process(const std::vector<float>& input_wav) {
    reset_states();
    audio_length_samples = static_cast<int>(input_wav.size());
    for (size_t j = 0; j < static_cast<size_t>(audio_length_samples); j += static_cast<size_t>(window_size_samples)) {
        if (j + static_cast<size_t>(window_size_samples) > static_cast<size_t>(audio_length_samples))
            break;
        std::vector<float> chunk(&input_wav[j], &input_wav[j] + window_size_samples);
        predict(chunk);
    }
    if (current_speech.start >= 0) {
        current_speech.end = audio_length_samples;
        speeches.push_back(current_speech);
        current_speech = timestamp_t();
        prev_end = 0;
        next_start = 0;
        temp_end = 0;
        triggered = false;
    }
}

const std::vector<timestamp_t> VadIterator::get_speech_timestamps() const {
    return speeches;
}

void VadIterator::reset() {
    reset_states();
}
