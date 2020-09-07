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
#ifndef __ANY_RTMP_CORE_H__
#define __ANY_RTMP_CORE_H__
#include <map>
#include <string>
#include <iostream>
#include <time.h>
#include "webrtc/base/criticalsection.h"
#include "webrtc/system_wrappers/include/critical_section_wrapper.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/thread.h"
#include "webrtc/common_audio/ring_buffer.h"
#include "webrtc/media/engine/webrtcvideodecoderfactory.h"
#include "webrtc/media/engine/webrtcvideoencoderfactory.h"
#include "webrtc/modules/audio_coding/acm2/acm_resampler.h"
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "webrtc\modules\audio_conference_mixer\include\audio_conference_mixer_defines.h"
#include "webrtc/modules/audio_conference_mixer/include/audio_conference_mixer.h"


namespace webrtc {
class AVAudioRecordCallback {
public:
	AVAudioRecordCallback(void){};
	virtual ~AVAudioRecordCallback(void){};

	virtual void OnRecordAudio(const void* audioSamples, const size_t nSamples,
		const size_t nBytesPerSample, const size_t nChannels, const uint32_t samplesPerSec, const uint32_t totalDelayMS) = 0;
};

class AVAudioTrackCallback {
public:
	AVAudioTrackCallback(void){};
	virtual ~AVAudioTrackCallback(void){};

	virtual int OnNeedPlayAudio(void* audioSamples, uint32_t& samplesPerSec, size_t& nChannels) = 0;
};

class AVAudioMixerParticipant : public webrtc::AudioTransport, public webrtc::MixerParticipant
{
	//通过AudioTransport接受录音数据，进行缓存，然后通过MixerParticipant进行混音
public:
	AVAudioMixerParticipant() {}

	virtual int32_t RecordedDataIsAvailable(const void* audioSamples,
		const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		const uint32_t totalDelayMS,
		const int32_t clockDrift,
		const uint32_t currentMicLevel,
		const bool keyPressed,
		uint32_t& newMicLevel) {

		webrtc::CriticalSectionScoped critScoped(&_critSect);

		webrtc::AudioFrame *frame = new webrtc::AudioFrame();
		frame->UpdateFrame(0, 0, (int16_t*)audioSamples, nSamples, samplesPerSec, webrtc::AudioFrame::kNormalSpeech, webrtc::AudioFrame::kVadActive, nChannels);
		m_frames.push_back(frame);

		while (m_frames.size() > 30) {
			delete m_frames.front();
			m_frames.pop_front();
		}

		//std::cout << "bgm audio record available " << m_frames.size() << std::endl;

		time_t timep;
		time(&timep);
		m_timestamp = timep;

		return 0;
	}

	void clearAllCache() {
		webrtc::CriticalSectionScoped critScoped(&_critSect);
		m_frames.clear();
	}

	bool empty() {
		webrtc::CriticalSectionScoped critScoped(&_critSect);
		return m_frames.empty();
	}

	virtual AudioFrameInfo GetAudioFrameWithMuted(int32_t id, AudioFrame* audio_frame) {
		return GetAudioFrame(id, audio_frame) == -1 ?
			AudioFrameInfo::kError :
			AudioFrameInfo::kNormal;
	}

	virtual int32_t GetAudioFrame(int32_t id, AudioFrame* audioFrame) {
		webrtc::CriticalSectionScoped critScoped(&_critSect);
		if (m_frames.size() > 0) {
			audioFrame->CopyFrom(*m_frames.front());
			delete m_frames.front();
			m_frames.pop_front();

			//std::cout << "bgm audio record pop " << m_frames.size() << std::endl;
			return 0;
		}
		return -1;
	}

	virtual int32_t NeededFrequency(int32_t id) const {
		return 48000; 
	}

	//只接收录音进行混音处理
	virtual int32_t NeedMorePlayData(const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		void* audioSamples,
		size_t& nSamplesOut,
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms) {
		ASSERT(false);
		return 0;
	}

	LONGLONG m_timestamp = 0;

private:
	std::list<webrtc::AudioFrame *> m_frames;  //
	CriticalSectionWrapper                 _critSect;
};

class AnyRtmpCore : public rtc::Thread, webrtc::AudioTransport, webrtc::AudioMixerOutputReceiver
{
public:
	static AnyRtmpCore& Inst() {
		static AnyRtmpCore avcore;
		return avcore;
	}

	void setAudioEnable(bool microphoneEnable, bool bgmEnable);

	void StartAudioRecord(AVAudioRecordCallback* callback, int sampleHz, int channel);
	void StopAudioRecord();

	rtc::scoped_refptr<webrtc::AudioDeviceModule> getAudioDeviceManager();

	void StartAudioTrack(AVAudioTrackCallback* callback);
	void StopAudioTrack();

	bool CheckAudioRecordStatus(); //检查声音录制的状态，主要是对比最后的录制时间

public:
	void SetExternalVideoEncoderFactory(cricket::WebRtcVideoEncoderFactory* factory);
	cricket::WebRtcVideoEncoderFactory* ExternalVideoEncoderFactory();

protected:
	//* For rtc::Thread
	virtual void Run();

	//* For webrtc::AudioTransport
	virtual int32_t RecordedDataIsAvailable(const void* audioSamples, const size_t nSamples, 
		const size_t nBytesPerSample, const size_t nChannels, const uint32_t samplesPerSec, const uint32_t totalDelayMS, 
		const int32_t clockDrift, const uint32_t currentMicLevel, const bool keyPressed,uint32_t& newMicLevel);

	virtual int32_t NeedMorePlayData(const size_t nSamples, const size_t nBytesPerSample, const size_t nChannels,
		const uint32_t samplesPerSec, void* audioSamples, size_t& nSamplesOut, int64_t* elapsed_time_ms, int64_t* ntp_time_ms);

	virtual void NewMixedAudio(const int32_t id, const AudioFrame& generalAudioFrame, const AudioFrame** uniqueAudioFrames,
		const uint32_t size) override;

protected:
	AnyRtmpCore();
	virtual ~AnyRtmpCore();
	bool					running_;
	rtc::scoped_refptr<webrtc::AudioDeviceModule>		audio_device_ptr_;
	rtc::scoped_refptr<webrtc::AudioDeviceModule>		audio_capture_ptr_;
	rtc::scoped_ptr<webrtc::AVAudioMixerParticipant>    audio_device_mixer_ptr_;
	rtc::scoped_ptr<webrtc::AVAudioMixerParticipant>    audio_capture_mixer_ptr_;
	webrtc::AudioConferenceMixer *audio_mixer_ = nullptr;
	rtc::scoped_ptr<cricket::WebRtcVideoEncoderFactory> video_encoder_factory_;

	//* For audio record
	rtc::CriticalSection	cs_audio_record_;
	webrtc::acm2::ACMResampler resampler_record_;
	AVAudioRecordCallback	*audio_record_callback_;
	int						audio_record_sample_hz_;
	int						audio_record_channels_;

	//* For audio player
	rtc::CriticalSection	cs_audio_track_;
	webrtc::acm2::ACMResampler resampler_track_;
	AVAudioTrackCallback	*audio_track_callback_;
	int						audio_track_sample_hz_;
	int						audio_track_channels_;

	int						audio_capture_dis = 0; //背景声的最后更新时间，如果超过3s,并且没数据，则不进行混音

	bool					microphone_enable_ = true;
	bool					bgm_enable_ = true;

	LONGLONG m_timestamp = 0;
};

}	// namespace webrtc

#endif	// __ANY_RTMP_CORE_H__