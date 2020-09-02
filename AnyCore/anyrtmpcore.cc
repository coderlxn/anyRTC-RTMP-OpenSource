/*
*  Copyright (c) 2016 The AnyRTC project authors. All Rights Reserved.
*
*  Please visit https://www.anyrtc.io for detail.
*
* The GNU General Public License is a free, copyleft license for
* software and other kinds of works.
*
* The licenses for most software and other practical works are designed
* to take away your freedom to share and change the works.  By contrast,
* the GNU General Public License is intended to guarantee your freedom to
* share and change all versions of a program--to make sure it remains free
* software for all its users.  We, the Free Software Foundation, use the
* GNU General Public License for most of our software; it applies also to
* any other work released this way by its authors.  You can apply it to
* your programs, too.
* See the GNU LICENSE file for more info.
*/
#include <iostream>
#include "anyrtmpcore.h"
#include "webrtc/modules/audio_device/audio_device_impl.h"
#include "webrtc/base/logging.h"
#ifdef WIN32
#include "webrtc/base/win32socketserver.h"
#else
#include <unistd.h>
#endif

#include "webrtc\modules\audio_conference_mixer\include\audio_conference_mixer.h"

#include "AudioCaptureModule.h"

static const size_t kMaxDataSizeSamples = 3840;
static const uint32_t kMaxAacSizeSamples = 1920;

namespace webrtc {
AnyRtmpCore::AnyRtmpCore()
	: running_(false)
	, audio_device_ptr_(NULL)
	, audio_capture_ptr_(NULL)
	, audio_record_callback_(NULL)
	, audio_record_sample_hz_(48000)
	, audio_record_channels_(2)
	, audio_track_callback_(NULL)
	, audio_track_sample_hz_(48000)
	, audio_track_channels_(2)
{
	running_ = true;
	rtc::Thread::SetName("AnyRTC-RTMP-Core", this);
	rtc::Thread::Start();

    //audio_device_ptr_ =  AudioDeviceModuleImpl::Create(0, AudioDeviceModule::kWindowsCoreAudio);
	//Microphone
	audio_device_ptr_ = AudioDeviceModuleImpl::Create(0, AudioDeviceModule::kPlatformDefaultAudio);
	audio_device_ptr_->Init();
	audio_device_ptr_->AddRef();
	audio_device_mixer_ptr_.reset(new webrtc::AVAudioMixerParticipant());
	audio_device_ptr_->RegisterAudioCallback(this);

	/*bgm_enable_ = false;*/
	//BGM
	audio_capture_ptr_ = webrtc::AudioCaptureModule::NewCreate(0, AudioDeviceModule::kWindowsCoreAudio);
	audio_capture_ptr_->Init();
	audio_capture_ptr_->AddRef();
	audio_capture_mixer_ptr_.reset(new webrtc::AVAudioMixerParticipant());
	audio_capture_ptr_->RegisterAudioCallback(audio_capture_mixer_ptr_.get());

	audio_mixer_ = webrtc::AudioConferenceMixer::Create(0);
	audio_mixer_->RegisterMixedStreamCallback(this);
	audio_mixer_->SetMixabilityStatus(audio_device_mixer_ptr_.get(), true);
	audio_mixer_->SetMixabilityStatus(audio_capture_mixer_ptr_.get(), true);

	// Initialize the default microphone
#ifdef WIN32
	if (audio_device_ptr_ && audio_device_ptr_->SetRecordingDevice(AudioDeviceModule::kDefaultCommunicationDevice) != 0) {
	//if (audio_device_ptr_ && audio_device_ptr_->SetRecordingDevice(AudioDeviceModule::kDefaultDevice) != 0) {
		audio_device_ptr_->InitMicrophone();
	}

	//背景声功能使用系统默认的设备
	/*if (audio_capture_ptr_ && audio_capture_ptr_->SetRecordingDevice(
		AudioDeviceModule::kDefaultCommunicationDevice) != 0) {
		audio_capture_ptr_->InitMicrophone();
	}*/
#endif
	
#ifdef WIN32
	if (audio_device_ptr_->SetPlayoutDevice(AudioDeviceModule::kDefaultCommunicationDevice) == 0) {
		audio_device_ptr_->InitSpeaker();
		audio_device_ptr_->SetStereoPlayout(true);
	}
#endif
}


AnyRtmpCore::~AnyRtmpCore()
{
	if (audio_device_ptr_) {
		if (audio_device_ptr_->Recording())
			audio_device_ptr_->StopRecording();
		if (audio_device_ptr_->Playing())
			audio_device_ptr_->StopPlayout();
		audio_device_ptr_->RegisterAudioCallback(NULL);
		audio_device_ptr_->Release();
		audio_device_ptr_ = NULL;
	}

	if (audio_capture_ptr_) {
		if (audio_capture_ptr_->Recording())
			audio_capture_ptr_->StopRecording();
		audio_capture_ptr_->RegisterAudioCallback(NULL);
		audio_capture_ptr_->Release();
		audio_capture_ptr_ = NULL;
	}

	if (running_) {
		running_ = false;
		rtc::Thread::Stop();
	}
}

rtc::scoped_refptr<webrtc::AudioDeviceModule> AnyRtmpCore::getAudioDeviceManager()
{
	return audio_device_ptr_;
}

//int16_t AnyRtmpCore::GetAudioDevicesNum()
//{
//	return audio_device_ptr_->RecordingDevices();
//}
//
//void AnyRtmpCore::GetDevicesName(std::map<std::string, std::string> &deviceNames)
//{
//	deviceNames.clear();
//	int num = audio_device_ptr_->RecordingDevices();
//	for (int i = 0; i < num; ++i) {
//		char name[webrtc::kAdmMaxDeviceNameSize];
//		char guid[webrtc::kAdmMaxGuidSize];
//		audio_device_ptr_->RecordingDeviceName(i, name, guid);
//		deviceNames.insert(std::string(guid), std::string(name));
//	}
//}
//
//void AnyRtmpCore::SetRecordingDevice(const std::string &deviceGuid)
//{
//	int num = audio_device_ptr_->RecordingDevices();
//	for (int i = 0; i < num; ++i) {
//		char name[webrtc::kAdmMaxDeviceNameSize];
//		char guid[webrtc::kAdmMaxGuidSize];
//		audio_device_ptr_->RecordingDeviceName(i, name, guid);
//		if (deviceGuid.compare(guid) == 0) {
//			audio_device_ptr_->SetRecordingDevice(i);
//		}
//	}
//}

void AnyRtmpCore::SetExternalVideoEncoderFactory(cricket::WebRtcVideoEncoderFactory* factory)
{
	video_encoder_factory_.reset(factory);
}
cricket::WebRtcVideoEncoderFactory* AnyRtmpCore::ExternalVideoEncoderFactory()
{
	return video_encoder_factory_.get();
}

void AnyRtmpCore::Run()
{
#if WIN32
	// Need to pump messages on our main thread on Windows.
	rtc::Win32Thread w32_thread;
#endif
	while (running_)
	{
		{// ProcessMessages
			this->ProcessMessages(10);			
		}
#if WIN32
		w32_thread.ProcessMessages(1);
#else
		usleep(1000);
#endif
	}
}

void AnyRtmpCore::setAudioEnable(bool microphoneEnable, bool bgmEnable)
{	
	//声音的合成是通过RecordedDataIsAvailable来控制的，当只有一种声音时，不进行混音	
	if (microphoneEnable) {
		if (audio_capture_ptr_) {
			std::cout << "audio capture callback set to mixer " << std::endl;
			audio_capture_ptr_->RegisterAudioCallback(audio_capture_mixer_ptr_.get());			
		}
		if (audio_device_ptr_ && !audio_device_ptr_->Recording()) {
			audio_device_ptr_->InitRecording();
			audio_device_ptr_->SetStereoRecording(true);
			audio_device_ptr_->StartRecording();
		}
	}
	else {
		audio_device_ptr_->StopRecording();
		if (audio_capture_ptr_) {
			std::cout << "audio capture callback set to core " << std::endl;
			audio_capture_ptr_->RegisterAudioCallback(this);
		}
	}

	if (bgmEnable) {
		audio_capture_ptr_->StartRecording();
	}
	else {
		audio_capture_ptr_->StopRecording();
	}

	audio_capture_mixer_ptr_->clearAllCache();
	audio_device_mixer_ptr_->clearAllCache();
	microphone_enable_ = microphoneEnable;
	bgm_enable_ = bgmEnable;

	/*if (microphoneEnable && bgmEnable) {
		if (audio_mixer_ != nullptr) {
			audio_mixer_->SetMixabilityStatus(audio_device_mixer_ptr_.get(), true);
			audio_mixer_->SetMixabilityStatus(audio_capture_mixer_ptr_.get(), true);
		}		
	}*/
}

bool AnyRtmpCore::CheckAudioRecordStatus()
{
	time_t timep;
	time(&timep);

	//检查最后收到声音的时间，如果是合成音，则检查两个mixer，如果是单音轨则检查 AnyRtmpCore 的时间戳
	//目前只处理麦克风的声音，背景声在没有音乐播放的情况下会是空白的
	if (microphone_enable_ && bgm_enable_) {
		int dt1 = timep - audio_device_mixer_ptr_->m_timestamp;
		//int dt2 = timep - audio_capture_mixer_ptr_->m_timestamp;
		//std::cout << "CheckAudioRecordStatus1 " << timep << "  " << dt1 << "  " << dt2 << std::endl;
		return dt1 < 5;
	}
	else if (microphone_enable_)  {
		//std::cout << "CheckAudioRecordStatus2 " << timep << "  " << m_timestamp << std::endl;
		return timep - m_timestamp < 5;
	}
	return true;
}

void AnyRtmpCore::StartAudioRecord(AVAudioRecordCallback* callback, int sampleHz, int channel)
{
	if (callback == NULL)
		return;

	{
		rtc::CritScope cs(&cs_audio_record_);
		audio_record_callback_ = callback;
		audio_record_sample_hz_ = sampleHz;
		audio_record_channels_ = channel;
	}
	
	if (audio_device_ptr_ && !audio_device_ptr_->Recording()) {
		audio_device_ptr_->InitRecording();
		audio_device_ptr_->SetStereoRecording(true);
		audio_device_ptr_->StartRecording();
	}

	if (audio_capture_ptr_ && !audio_capture_ptr_->Recording()) {
		audio_capture_ptr_->InitRecording();
		audio_capture_ptr_->SetStereoRecording(true);
		audio_capture_ptr_->StartRecording();
	}
	
}

void AnyRtmpCore::StopAudioRecord()
{
	{
		rtc::CritScope cs(&cs_audio_record_);
		audio_record_callback_ = NULL;
	}
	
	if (audio_device_ptr_ && audio_device_ptr_->Recording()) {
		audio_device_ptr_->StopRecording();
	}

	if (audio_capture_ptr_ && audio_capture_ptr_->Recording()) {
		audio_capture_ptr_->StopRecording();
	}
}

void AnyRtmpCore::StartAudioTrack(AVAudioTrackCallback* callback)
{
	{
		rtc::CritScope cs(&cs_audio_track_);
		audio_track_callback_ = callback;
	}
	
	if (audio_device_ptr_ && !audio_device_ptr_->Playing()) {
		audio_device_ptr_->InitPlayout();
		audio_device_ptr_->StartPlayout();
	}

	if (audio_capture_ptr_ && !audio_capture_ptr_->Playing()) {
		audio_capture_ptr_->InitPlayout();
		audio_capture_ptr_->StartPlayout();
	}
}

void AnyRtmpCore::StopAudioTrack()
{
	{
		rtc::CritScope cs(&cs_audio_track_);
		audio_track_callback_ = NULL;
	}

    if (audio_device_ptr_ && audio_device_ptr_->Playing()) {
        audio_device_ptr_->StopPlayout();
    }

	if (audio_capture_ptr_ && audio_capture_ptr_->Playing()) {
		audio_capture_ptr_->StopPlayout();
	}
}

int32_t AnyRtmpCore::RecordedDataIsAvailable(const void* audioSamples, const size_t nSamples,
	const size_t nBytesPerSample, const size_t nChannels, const uint32_t samplesPerSec, const uint32_t totalDelayMS,
	const int32_t clockDrift, const uint32_t currentMicLevel, const bool keyPressed, uint32_t& newMicLevel)
{
	//std::cout << "[-----------] record data avaliable " << nSamples << nBytesPerSample << nChannels << samplesPerSec << std::endl;
	rtc::CritScope cs(&cs_audio_record_);

	if (microphone_enable_ && bgm_enable_) {
		audio_device_mixer_ptr_->RecordedDataIsAvailable(audioSamples, nSamples,
			nBytesPerSample, nChannels, samplesPerSec, totalDelayMS,
			clockDrift, currentMicLevel, keyPressed, newMicLevel);
		if (audio_mixer_) {
			audio_mixer_->Process();
		}		
	}
	else
	{
		time_t timep;
		time(&timep);
		m_timestamp = timep;

		// 当只有一种声音时，不进行混音
		if (audio_record_callback_) {
			if (audio_record_sample_hz_ != samplesPerSec || nChannels != audio_record_channels_) {
				int16_t temp_output[kMaxDataSizeSamples];
				int samples_per_channel_int = resampler_record_.Resample10Msec((int16_t*)audioSamples, samplesPerSec * nChannels,
					audio_record_sample_hz_ * audio_record_channels_, 1, kMaxDataSizeSamples, temp_output);
				audio_record_callback_->OnRecordAudio(temp_output, audio_record_sample_hz_ / 100, nBytesPerSample, audio_record_channels_, audio_record_sample_hz_, totalDelayMS);
			}
			else {
				audio_record_callback_->OnRecordAudio(audioSamples, nSamples, nBytesPerSample, audio_record_channels_, samplesPerSec, totalDelayMS);
			}
		}
	}
		
	return 0;
}

int32_t AnyRtmpCore::NeedMorePlayData(const size_t nSamples, const size_t nBytesPerSample, const size_t nChannels,
	const uint32_t samplesPerSec, void* audioSamples, size_t& nSamplesOut, int64_t* elapsed_time_ms, int64_t* ntp_time_ms)
{
	rtc::CritScope cs(&cs_audio_track_);
	if (audio_track_callback_ != NULL) {
		char aac_buf[kMaxAacSizeSamples];
		uint32_t sampleHz = 0;
		size_t channel = 0;
		int readed = audio_track_callback_->OnNeedPlayAudio(aac_buf, sampleHz, channel);
		audio_track_sample_hz_ = sampleHz;
		audio_track_channels_ = channel;
		*elapsed_time_ms = 0;
		*ntp_time_ms = 0;

		if (readed > 0) {
			int16_t temp_output[kMaxDataSizeSamples];
			int samples_per_channel_int = resampler_track_.Resample10Msec((int16_t*)aac_buf, audio_track_sample_hz_ * audio_track_channels_,
				samplesPerSec * nChannels, 1, kMaxDataSizeSamples, temp_output);
			
			if (samples_per_channel_int > 0) {
				memcpy(audioSamples, (char*)temp_output, samples_per_channel_int * sizeof(int16_t));
				nSamplesOut = samples_per_channel_int / nChannels;
			}
		}
		else {
			int samples_per_channel_int = samplesPerSec / 100;
			if (samples_per_channel_int > 0) {
				memset(audioSamples, 0, samples_per_channel_int * sizeof(int16_t) * nChannels);
				nSamplesOut = samples_per_channel_int;
			}
		}
		
	}

	return 0;
}

void AnyRtmpCore::NewMixedAudio(const int32_t id, const AudioFrame& generalAudioFrame, const AudioFrame** uniqueAudioFrames,
	const uint32_t size) {
	//从audioframe中读取数据进行编码上传，是混合之后的声音
	if (audio_record_callback_) {
		audio_record_callback_->OnRecordAudio(generalAudioFrame.data_, generalAudioFrame.samples_per_channel_, 0, generalAudioFrame.num_channels_, generalAudioFrame.sample_rate_hz_, 0);
	}
}

}	// namespace webrtc
