---
name: "cpp-vad-service"
description: "Manages the C++ VAD (Voice Activity Detection) service. Invoke when the user wants to start, stop, test, or check the status of the VAD service."
---

# C++ VAD Service Skill

This skill allows you to manage the high-performance C++ Voice Activity Detection (VAD) service. The service uses Silero VAD (ONNX) to detect speech in real-time audio streams via WebSocket.

## Capabilities

1.  **Start Service**: Launch the VAD service using Docker Compose.
2.  **Stop Service**: Stop the VAD service.
3.  **Run Tests**: Execute the Python test client to verify VAD functionality.
4.  **View Logs**: Check the service logs.

## Usage

### 1. Start Service
Use this command to build and start the service in the background:
```bash
docker compose up -d --build
```
*Wait for a few seconds for the service to initialize.*

### 2. Check Service Status
Verify if the container is running:
```bash
docker ps | grep cpp_service
```

### 3. Run Tests
**Prerequisite**: The service must be running (see "Start Service").

Run the integrated Python test client (sends `test_long.pcm`):
```bash
python3 test/test_client.py
```
*Expected Output*: You should see `VAD_BEGIN`, `SPEAKING`, and `END_SPEAKING` JSON messages in the output.

### 4. View Logs
To debug or see server-side processing:
```bash
docker compose logs -f vad_server
```

### 5. Stop Service
To clean up resources:
```bash
docker compose down
```

## Protocol Overview
- **URL**: `ws://localhost:9002`
- **Input**: 16kHz, 16-bit, Mono PCM binary stream.
- **Output**: JSON messages.
  - `VAD_BEGIN`: Speech started (includes `new_session` timestamp + audio).
  - `SPEAKING`: Speech continuing (includes audio chunk).
  - `END_SPEAKING`: Speech ended (includes full audio segment).
  - `SILENCE`: Silence detected (no audio).
