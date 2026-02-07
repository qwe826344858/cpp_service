package silerovad

import (
	"context"
	"fmt"
	"time"

	"gurobot.cn/iot-alarm/app/domain"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/sherpa_onnx"
	"go.uber.org/zap"
	"gurobot.cn/iot-alarm/pkg/logger"
	"gurobot.cn/iot-alarm/pkg/speex"
	"gurobot.cn/iot-alarm/pkg/webrtcvad"
)

const (
	FrameDuration10                           = 20
	FrameDuration20                           = 20
	FrameDuration20SizeInBytes                = SampleRate16 / 1000 * FrameDuration20 * BitDepth16 / 8
	FrameDuration100SizeInBytesInMilliseconds = FrameDuration20SizeInBytes * 5

	VadVoiceBeginDurationMilliseconds = 250
	VADVoiceStopDurationMilliseconds  = 600
	VADVoiceMaximumDurationSeconds    = 30 * time.Second
	VADVoiceTotalBufferCapacity       = FrameDuration100SizeInBytesInMilliseconds * 10

	VoiceActivityDetectorDEBUG = false

	VoiceActivityDetectorLocalStorageWavDIR = "/log/server/"
	VoiceActivityDetectorVolumeMultiple     = 1
)

type Detector struct {
	vad        *sherpa.VoiceActivityDetector
	modeConfig sherpa.VadModelConfig
	modelPath  string
	marginBuff intBuff
	waitBuff   []byte
	fixed      *webrtcvad.FixedSizeBuffer

	frameIndex          int64
	recognitionDuration float32
	silenceDuration     float32
	maxSilenceDuration  float32

	state             State
	clip              Clip
	Events            chan *Event
	timeoutCtx        context.Context
	timeoutCancel     context.CancelFunc
	uuid              string
	L                 *logger.Logger
	isSendSourceAudio bool
}

func New(uuid string, modelPath string, l *logger.Logger, deviceParam domain.DeviceParam) *Detector {
	isSendSourceAudio := deviceParam.SupportCustom
	config := sherpa.VadModelConfig{}
	config.SileroVad.Model = modelPath
	config.SileroVad.Threshold = deviceParam.GetVadThreshold()
	// config.SileroVad.Threshold = 0.8925
	config.SileroVad.MinSilenceDuration = 0.6 //( Not container Internal 100ms splicing)
	// Final speech chunks shorter min_speech_duration_ms are thrown out
	config.SileroVad.MinSpeechDuration = 0.1
	config.SileroVad.WindowSize = 512
	config.SampleRate = 16000
	config.NumThreads = 1
	config.Provider = "cpu"
	config.Debug = 0

	return &Detector{
		uuid:              uuid,
		modelPath:         modelPath,
		modeConfig:        config,
		Events:            make(chan *Event),
		L:                 l,
		isSendSourceAudio: isSendSourceAudio,
	}
}

func (d *Detector) Initialize(maxSilenceDuration int) error {
	if d.vad == nil {
		bufferSizeInSeconds := float32(15)
		d.vad = sherpa.NewVoiceActivityDetector(&d.modeConfig, bufferSizeInSeconds)
	}
	d.maxSilenceDuration = float32(maxSilenceDuration)

	d.Reset()
	return nil

}

// Reset the parameters to initial state
func (d *Detector) Reset() {

	d.frameIndex = 0
	d.silenceDuration = 0
	d.recognitionDuration = 0

	d.waitBuff = make([]byte, 0)
	d.marginBuff.Reset()

	d.fixed = webrtcvad.NewFixedSizeBuffer(VADVoiceTotalBufferCapacity)

	d.setState(StateInactivity)

	d.vad.Clear()

	d.L.Info(logger.ModuleWebsocket, zap.String(logger.DeviceID, d.uuid), zap.String(logger.Content, "VAD initialized"))

}

func (d *Detector) ProcessFrames(buff []byte, encoding string, speexSize int) bool {
	var pcmBuff []byte
	if encoding == "speex-wb" {
		pcmBuff, _ = speex.Decode(buff, len(buff), speexSize)
	} else if encoding == "raw" {
		pcmBuff = buff
	}

	d.marginBuff.Append(pcmBuff, buff)
	block := d.marginBuff.Len() / FrameDuration20SizeInBytes
	if block <= 0 {
		return false
	}

	pcmData, sourceData := d.marginBuff.Get(block * FrameDuration20SizeInBytes)
	size := len(pcmData)
	ratio := float32(len(sourceData)) / float32(len(pcmData))

	for i := 0; i < size; i += FrameDuration20SizeInBytes {
		end := i + FrameDuration20SizeInBytes
		if end > size {
			end = size
		}
		pcmFrame := pcmData[i:end]
		sourceFrame := sourceData[int(float32(i)*ratio):int(float32(end)*ratio)]
		if end == size {
			// 最后一次，防止数据丢失
			sourceFrame = sourceData[int(float32(i)*ratio):]
		}

		// Process the chunk and get the result
		if d.processFrame(pcmFrame, sourceFrame) {
			return true
		}
	}

	return false
}

// 强制停止云端 vad
func (d *Detector) ForceEnd() {
	if d.state == StateInactivity {
		return
	}

	d.setState(StateInactivity)
	d.emitVoiceEnd()
}

func (d *Detector) processFrame(buff, sourceBuff []byte) bool {
	d.vad.AcceptWaveform(d.byteToFloat32(buff))
	frameActive := d.vad.IsSpeech()

	if VoiceActivityDetectorDEBUG {
		d.L.Warn(logger.ModuleWebsocket,
			zap.String(logger.DeviceID, d.uuid),
			zap.String(logger.Content, fmt.Sprintf("frameIndex:%d,frameActive:%v", d.frameIndex, frameActive)))

	}
	if !d.isSendSourceAudio {
		sourceBuff = buff
	}

	d.frameIndex++

	for !d.vad.IsEmpty() {
		segment := d.vad.Front()
		d.vad.Pop()

		d.recognitionDuration = float32(len(segment.Samples)) / float32(d.modeConfig.SampleRate) * 1000

		d.L.Warn(logger.ModuleWebsocket,
			zap.String(logger.DeviceID, d.uuid),
			zap.Float32("duration", d.recognitionDuration))

		if d.state == StateInactivity {
			if d.recognitionDuration >= VadVoiceBeginDurationMilliseconds {
				d.setState(StateInactivityTransition)
				return false
			}
		} else if d.state == StateActivity {
			if d.recognitionDuration >= VadVoiceBeginDurationMilliseconds {
				d.setState(StateInactivity)
				d.emitVoiceEnd()
				return true
			}
		}
	}

	switch d.state {
	case StateInactivity:
		// 目标是转到半活动状态(StateInactivityTransition)
		d.fixed.Append(sourceBuff)

		if frameActive {
			d.recognitionDuration += FrameDuration20
		} else {

			if d.recognitionDuration >= FrameDuration20 {
				d.recognitionDuration -= FrameDuration10
			}

			d.silenceDuration += FrameDuration20
		}

		if d.recognitionDuration >= VadVoiceBeginDurationMilliseconds {
			d.setState(StateInactivityTransition)

		} else if d.silenceDuration >= d.maxSilenceDuration {
			d.emitVoiceSilent()
			return true
		}

	case StateInactivityTransition:
		// 目标是转到全活动状态(StateActivity)
		d.fixed.Append(sourceBuff)

		d.waitBuff = append(d.waitBuff, d.fixed.GetData()...)

		if VoiceActivityDetectorDEBUG {
			d.clip.AppendToActiveData(d.fixed.GetData())
		}

		d.setState(StateActivity)
		d.emitVoiceBegin()

		if !frameActive {
			d.silenceDuration += FrameDuration20
		}

	case StateActivity:
		// 目标是转到非活动状态(StateInactivity) 表示用户停止说话或超时
		d.waitBuff = append(d.waitBuff, sourceBuff...)
		if VoiceActivityDetectorDEBUG {
			d.clip.AppendToActiveData(sourceBuff)
		}

		if frameActive {
			d.silenceDuration -= FrameDuration20
		} else {
			d.silenceDuration += FrameDuration20
		}

		if d.silenceDuration < 0 {
			d.silenceDuration = 0
		}

		if d.silenceDuration >= VADVoiceStopDurationMilliseconds {
			d.setState(StateInactivity)
			d.emitVoiceEnd()

			return true
		}

		if len(d.waitBuff) >= FrameDuration100SizeInBytesInMilliseconds {
			d.emitVoiceOnGoing()
		}
	}

	return false
}

func (d *Detector) setState(state State) {
	d.silenceDuration = 0
	d.recognitionDuration = 0
	d.state = state
}

func (d *Detector) deepClone(buff []byte) []byte {
	return append([]byte{}, buff...)
}

// emitVoiceBegin sends an Event of type EventVoiceBegin to the Events channel
func (d *Detector) emitVoiceBegin() {

	if VoiceActivityDetectorVolumeMultiple > 1 {
		amplifyPCMData16Bit(d.waitBuff, VoiceActivityDetectorVolumeMultiple)
	}

	d.Events <- &Event{Type: EventVoiceBegin, Clip: Clip{Data: d.deepClone(d.waitBuff)}}
	d.waitBuff = d.waitBuff[:0]

	d.startContextTimeout()
}

// emitVoiceOnGoing sends an Event of type EventVoiceOngoing to the Events channel
func (d *Detector) emitVoiceOnGoing() {

	if VoiceActivityDetectorVolumeMultiple > 1 {
		amplifyPCMData16Bit(d.waitBuff, VoiceActivityDetectorVolumeMultiple)
	}

	d.Events <- &Event{Type: EventVoiceOngoing, Clip: Clip{Data: d.deepClone(d.waitBuff)}}
	d.waitBuff = d.waitBuff[:0]
}

// emitVoiceEnd sends an Event of type EventVoiceEnd to the Events channel
func (d *Detector) emitVoiceEnd() {

	if VoiceActivityDetectorVolumeMultiple > 1 {
		amplifyPCMData16Bit(d.waitBuff, VoiceActivityDetectorVolumeMultiple)
	}

	d.Events <- &Event{Type: EventVoiceEnd, Clip: Clip{Data: d.deepClone(d.waitBuff)}}
	d.waitBuff = d.waitBuff[:0]

	d.releaseContextTimeout()

	if VoiceActivityDetectorDEBUG {
		id := time.Now().Unix()
		d.clip.SaveActiveDataToFile(fmt.Sprintf("%s%s_%d_active.pcm", VoiceActivityDetectorLocalStorageWavDIR, d.uuid, id))
		d.clip.ResetActiveData()
	}

}

func (d *Detector) emitVoiceSilent() {
	d.Events <- &Event{Type: EventVoiceSilent, Clip: Clip{}}
	d.releaseContextTimeout()
	d.clip.ResetData()
}

func (d *Detector) startContextTimeout() {
	// IMPORTANT: 先销毁上一个实例的timeoutCtx，防止上一个没结束就提前触发
	d.releaseContextTimeout()

	d.timeoutCtx, d.timeoutCancel = context.WithTimeout(context.Background(), VADVoiceMaximumDurationSeconds)
	go func(ctx context.Context) {
		<-ctx.Done()
		if ctx.Err() == context.DeadlineExceeded {
			d.L.Warn(logger.ModuleWebsocket, zap.String(logger.DeviceID, d.uuid), zap.String("Timeout", "Voice activity timed out"))
			d.emitVoiceEnd()
		}
	}(d.timeoutCtx)
}

func (d *Detector) releaseContextTimeout() {
	if d.timeoutCancel != nil {
		d.L.Warn(logger.ModuleWebsocket, zap.String(logger.DeviceID, d.uuid), zap.String("Timeout", "Cancel voice activity timeout"))
		d.timeoutCancel()
		d.timeoutCancel = nil
	}
}

func (d *Detector) Troubleshoot(buff []byte, end bool) {
	if VoiceActivityDetectorDEBUG {
		d.clip.AppendToBigCache(buff)
		if end {
			id := time.Now().Unix()
			d.clip.SaveBigCacheToFile(fmt.Sprintf("%s%s_%d_big.pcm", VoiceActivityDetectorLocalStorageWavDIR, d.uuid, id))
		}
	}
}

// releaseVad releases the VAD instance
func (d *Detector) releaseVad() {
	if d.vad != nil {
		d.fixed.Reset()
		d.vad.Reset()
		sherpa.DeleteVoiceActivityDetector(d.vad)
		close(d.Events)
		d.vad = nil
	}
}

// Release releases the VAD instance
func (d *Detector) Release() bool {
	d.L.Warn(logger.ModuleWebsocket, zap.String(logger.DeviceID, d.uuid), zap.String(logger.Reason, "VAD released"))

	d.releaseVad()
	d.releaseContextTimeout()
	return true
}

func (d *Detector) byteToFloat32(buff []byte) []float32 {
	if len(buff)%2 != 0 {
		buff = buff[:len(buff)-(len(buff)%2)]
	}

	count := len(buff) / 2
	pcmData := make([]float32, 0, count)

	for i := 0; i < count; i++ {
		sample := int16(buff[i*2]) | int16(buff[i*2+1])<<8
		pcmData = append(pcmData, float32(sample)/32767.0)
	}
	return pcmData
}

func (d *Detector) writeWAVFile(k int, duration float32, SampleRate int, segment *sherpa.SpeechSegment) {
	audio := sherpa.GeneratedAudio{}
	audio.Samples = segment.Samples
	audio.SampleRate = SampleRate

	filename := fmt.Sprintf("./debug/seg-%d-%.2f-seconds.wav", k, duration)
	if !audio.Save(filename) {
		fmt.Println("Failed to save audio")
	}
}

func (d *Detector) GetModel() float32 {
	return d.modeConfig.SileroVad.Threshold
}
