#include "vad_iterator.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing VAD compilation..." << std::endl;
    // Dummy model path, just to check linking
    VadIterator vad("../model/silero_vad.onnx"); // Path relative to build/ executable
    std::vector<float> dummy_input(16000, 0.0f);
    vad.process(dummy_input);
    auto stamps = vad.get_speech_timestamps();
    std::cout << "VAD processed " << stamps.size() << " segments." << std::endl;
    return 0;
}
