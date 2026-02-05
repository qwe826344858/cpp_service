import websocket
import threading
import time
import math
import struct
import json

# 配置
URI = "ws://localhost:9002"
SAMPLE_RATE = 16000
FRAME_DURATION_MS = 20
FRAME_SIZE = int(SAMPLE_RATE * FRAME_DURATION_MS / 1000) # 320 samples

def generate_sine_wave(frequency, duration_ms, volume=1.0):
    """生成正弦波 PCM 数据 (16bit Little Endian)"""
    num_samples = int(SAMPLE_RATE * duration_ms / 1000)
    audio = bytearray()
    for i in range(num_samples):
        sample = int(32767.0 * volume * math.sin(2 * math.pi * frequency * i / SAMPLE_RATE))
        audio.extend(struct.pack('<h', sample))
    return audio

def generate_silence(duration_ms):
    """生成静音数据"""
    num_samples = int(SAMPLE_RATE * duration_ms / 1000)
    return bytearray(num_samples * 2)

def on_message(ws, message):
    print(f"< Received: {message}")

def on_error(ws, error):
    print(f"! Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("### Closed ###")

def on_open(ws):
    print("### Opened ###")
    
    def run(*args):
        # 1. 发送 1秒静音
        print("> Sending 1s Silence...")
        for _ in range(50): # 50 * 20ms = 1000ms
            ws.send(generate_silence(FRAME_DURATION_MS), opcode=websocket.ABNF.OPCODE_BINARY)
            time.sleep(0.02)
        
        # 2. 发送 2秒语音 (440Hz)
        print("> Sending 2s Speech (440Hz)...")
        sine_wave = generate_sine_wave(440, FRAME_DURATION_MS, 0.5)
        for _ in range(100): 
            ws.send(sine_wave, opcode=websocket.ABNF.OPCODE_BINARY)
            time.sleep(0.02)
            
        # 3. 发送 1秒静音
        print("> Sending 1s Silence...")
        for _ in range(50):
            ws.send(generate_silence(FRAME_DURATION_MS), opcode=websocket.ABNF.OPCODE_BINARY)
            time.sleep(0.02)

        print("Done sending.")
        time.sleep(1)
        ws.close()

    threading.Thread(target=run).start()

if __name__ == "__main__":
    websocket.enableTrace(False)
    ws = websocket.WebSocketApp(URI,
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)
    ws.run_forever()
