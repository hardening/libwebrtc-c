/**
 * libwebrtc-c: a C API over the webrtc-sdk/libwebrtc C++ wrapper
 *
 * Copyright 2026 David Fort <contact@hardening-consulting.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LWRTC_INTERNAL_H
#define LWRTC_INTERNAL_H

/* libwebrtc.so is built with its own libc++: only the wrapper's portable:: types and
 * RefCountInterface objects may cross the boundary. Never call the methods taking or returning
 * std:: containers (RTCStatsMember::ValueMapString*, RTCRtpCodecParameters::set_parameters). */

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "libwebrtc.h"
#include "rtc_audio_device.h"
#include "rtc_audio_source.h"
#include "rtc_audio_track.h"
#include "rtc_data_channel.h"
#include "rtc_dtmf_sender.h"
#include "rtc_ice_candidate.h"
#include "rtc_media_stream.h"
#include "rtc_media_track.h"
#include "rtc_mediaconstraints.h"
#include "rtc_peerconnection.h"
#include "rtc_peerconnection_factory.h"
#include "rtc_rtp_parameters.h"
#include "rtc_rtp_receiver.h"
#include "rtc_rtp_sender.h"
#include "rtc_rtp_transceiver.h"
#include "rtc_session_description.h"
#include "rtc_video_device.h"
#include "rtc_video_frame.h"
#include "rtc_video_renderer.h"
#include "rtc_video_source.h"
#include "rtc_video_track.h"

#include <lwrtc/lwrtc.h>

namespace lwrtc
{
	/** base of every reference counted handle */
	struct Object
	{
		std::atomic<int> refs{ 1 };
		virtual ~Object() = default;
	};

	template <typename T> T* add_ref(T* object)
	{
		if (object)
			object->refs.fetch_add(1, std::memory_order_relaxed);
		return object;
	}

	template <typename T> void release(T* object)
	{
		if (object && object->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete object;
	}

	/* ---- strings ---- */

	inline std::string str(const libwebrtc::string& s)
	{
		return s.std_string();
	}

	inline libwebrtc::string pstr(const char* s)
	{
		return libwebrtc::string(s ? s : "");
	}

	/** a malloc()ed copy, for lwrtc_free() */
	char* dup(const std::string& s);
	inline char* dup(const libwebrtc::string& s)
	{
		return dup(str(s));
	}

	char** dup_strings(const libwebrtc::vector<libwebrtc::string>& v, size_t* count);
	libwebrtc::vector<libwebrtc::string> pstrings(const char* const* strings, size_t count);

	/** a malloc()ed array of the handles, for lwrtc_*s_free() */
	template <typename Handle>
	Handle** dup_handles(const std::vector<Handle*>& handles, size_t* count)
	{
		*count = handles.size();
		Handle** out = static_cast<Handle**>(calloc(handles.size() + 1, sizeof(Handle*)));
		if (!out)
		{
			for (Handle* h : handles)
				release(h);
			*count = 0;
			return nullptr;
		}
		for (size_t i = 0; i < handles.size(); i++)
			out[i] = handles[i];
		return out;
	}

	/* ---- enum conversions ---- */

	lwrtc_media_kind kind(libwebrtc::RTCMediaType type);
	libwebrtc::RTCMediaType media_type(lwrtc_media_kind kind);
	lwrtc_direction direction(libwebrtc::RTCRtpTransceiverDirection d);
	libwebrtc::RTCRtpTransceiverDirection direction(lwrtc_direction d);

	/* ---- object factories ---- */

	lwrtc_media_track* wrap_track(libwebrtc::scoped_refptr<libwebrtc::RTCMediaTrack> track,
	                              bool remote);
	lwrtc_rtp_sender* wrap_sender(libwebrtc::scoped_refptr<libwebrtc::RTCRtpSender> sender);
	lwrtc_rtp_receiver* wrap_receiver(libwebrtc::scoped_refptr<libwebrtc::RTCRtpReceiver> receiver);
	lwrtc_rtp_transceiver*
	wrap_transceiver(libwebrtc::scoped_refptr<libwebrtc::RTCRtpTransceiver> transceiver);
	lwrtc_data_channel*
	wrap_data_channel(libwebrtc::scoped_refptr<libwebrtc::RTCDataChannel> channel);

	libwebrtc::scoped_refptr<libwebrtc::RTCRtpEncodingParameters>
	make_encoding(const lwrtc_rtp_encoding& e);
} // namespace lwrtc

/* ---- the handles ---- */

struct lwrtc_factory : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCPeerConnectionFactory> factory;
	libwebrtc::scoped_refptr<libwebrtc::RTCAudioDevice> audio;
	libwebrtc::scoped_refptr<libwebrtc::RTCVideoDevice> video;

	std::mutex lock;
	lwrtc_device_change_fn deviceChange = nullptr;
	void* deviceChangeUser = nullptr;

	~lwrtc_factory() override;
};

struct lwrtc_media_track : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCMediaTrack> track;
	bool remote = false;
	std::string label;
	std::string deviceId;
	std::atomic<bool> stopped{ false };

	/* local tracks: what produces the media */
	libwebrtc::scoped_refptr<libwebrtc::RTCAudioSource> audioSource;
	libwebrtc::scoped_refptr<libwebrtc::RTCVideoSource> videoSource;
	libwebrtc::scoped_refptr<libwebrtc::RTCVideoCapturer> capturer;

	~lwrtc_media_track() override;

	libwebrtc::RTCAudioTrack* audio() const
	{
		return track->kind().std_string() == "audio"
		           ? static_cast<libwebrtc::RTCAudioTrack*>(track.get())
		           : nullptr;
	}

	libwebrtc::RTCVideoTrack* video() const
	{
		return track->kind().std_string() == "video"
		           ? static_cast<libwebrtc::RTCVideoTrack*>(track.get())
		           : nullptr;
	}
};

struct lwrtc_video_frame : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCVideoFrame> frame;
};

struct lwrtc_rtp_sender;

/** observer of a sender's DTMF sender, owned by the sender handle */
struct DtmfObserver : libwebrtc::RTCDtmfSenderObserver
{
	lwrtc_dtmf_tone_fn fn = nullptr;
	void* user = nullptr;

	void OnToneChange(const libwebrtc::string tone, const libwebrtc::string tone_buffer) override
	{
		fn(user, lwrtc::str(tone).c_str(), lwrtc::str(tone_buffer).c_str());
	}

	/* the wrapper calls both overloads for each tone: this one would repeat the other */
	void OnToneChange(const libwebrtc::string) override
	{
	}
};

struct lwrtc_rtp_sender : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCRtpSender> sender;
	libwebrtc::scoped_refptr<libwebrtc::RTCDtmfSender> dtmf;
	DtmfObserver* dtmfObserver = nullptr;

	~lwrtc_rtp_sender() override;
};

struct lwrtc_rtp_receiver : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCRtpReceiver> receiver;
};

struct lwrtc_rtp_transceiver : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCRtpTransceiver> transceiver;
};

/** observer of a data channel, owned by the data channel handle */
struct DataChannelObserver : libwebrtc::RTCDataChannelObserver
{
	lwrtc_data_channel_observer callbacks{};

	void OnStateChange(libwebrtc::RTCDataChannelState state) override
	{
		if (callbacks.on_state)
			callbacks.on_state(callbacks.user, static_cast<lwrtc_data_channel_state>(state));
	}

	void OnMessage(const char* buffer, int length, bool binary) override
	{
		if (callbacks.on_message)
			callbacks.on_message(callbacks.user, reinterpret_cast<const uint8_t*>(buffer),
			                     length > 0 ? static_cast<size_t>(length) : 0, binary);
	}
};

struct lwrtc_data_channel : lwrtc::Object
{
	libwebrtc::scoped_refptr<libwebrtc::RTCDataChannel> channel;
	std::mutex lock;
	DataChannelObserver* observer = nullptr;

	~lwrtc_data_channel() override;
};

#endif /* LWRTC_INTERNAL_H */
