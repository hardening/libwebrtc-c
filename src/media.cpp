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

/* factory, devices, tracks, sinks and video frames */

#include "internal.h"

#include "helper.h"

using namespace libwebrtc;

#define LWRTC_DEFINE_REFCOUNTED(type)  \
	type* type##_add_ref(type* object) \
	{                                  \
		return lwrtc::add_ref(object); \
	}                                  \
	void type##_release(type* object)  \
	{                                  \
		lwrtc::release(object);        \
	}

LWRTC_DEFINE_REFCOUNTED(lwrtc_factory)
LWRTC_DEFINE_REFCOUNTED(lwrtc_media_track)
LWRTC_DEFINE_REFCOUNTED(lwrtc_video_frame)

void lwrtc_media_tracks_free(lwrtc_media_track** tracks, size_t count)
{
	if (!tracks)
		return;
	for (size_t i = 0; i < count; i++)
		lwrtc::release(tracks[i]);
	free(tracks);
}

/* ---- factory ---- */

lwrtc_factory::~lwrtc_factory()
{
	if (audio)
		audio->OnDeviceChange(nullptr);
	audio = nullptr;
	video = nullptr;
	if (factory)
		factory->Terminate();
}

lwrtc_factory* lwrtc_factory_new(void)
{
	auto* f = new lwrtc_factory();
	f->factory = LibWebRTC::CreateRTCPeerConnectionFactory();
	if (!f->factory || !f->factory->Initialize())
	{
		f->factory = nullptr;
		lwrtc::release(f);
		return nullptr;
	}
	f->audio = f->factory->GetAudioDevice();
	f->video = f->factory->GetVideoDevice();
	return f;
}

/* ---- devices ---- */

namespace
{
	/** PulseAudio names the default device "default: <name>" */
	const char kDefaultPrefix[] = "default: ";

	struct Device
	{
		lwrtc_device_kind kind;
		std::string id;
		std::string label;
		std::string name; /* WebRTC's name, to open a camera */
		uint32_t index;
	};

	std::vector<Device> audio_devices(lwrtc_factory* f, bool input)
	{
		std::vector<Device> out;
		if (!f->audio)
			return out;

		const int count = input ? f->audio->RecordingDevices() : f->audio->PlayoutDevices();
		for (int i = 0; i < count; i++)
		{
			char name[RTCAudioDevice::kAdmMaxDeviceNameSize] = { 0 };
			char guid[RTCAudioDevice::kAdmMaxGuidSize] = { 0 };
			const int32_t rc = input ? f->audio->RecordingDeviceName(i, name, guid)
			                         : f->audio->PlayoutDeviceName(i, name, guid);
			if (rc != 0)
				continue;

			Device d;
			d.kind = input ? LWRTC_DEVICE_AUDIO_INPUT : LWRTC_DEVICE_AUDIO_OUTPUT;
			d.index = static_cast<uint32_t>(i);
			d.name = name;
			if (strncmp(name, kDefaultPrefix, sizeof(kDefaultPrefix) - 1) == 0)
			{
				/* as browsers present it */
				d.id = "default";
				d.label = std::string("Default - ") + (name + sizeof(kDefaultPrefix) - 1);
			}
			else
			{
				d.id = guid[0] ? guid : name;
				d.label = name;
			}
			out.push_back(d);
		}
		return out;
	}

	std::vector<Device> video_devices(lwrtc_factory* f)
	{
		std::vector<Device> out;
		if (!f->video)
			return out;

		const uint32_t count = f->video->NumberOfDevices();
		for (uint32_t i = 0; i < count; i++)
		{
			char name[LWRTC_DEVICE_STRING_SIZE] = { 0 };
			char id[LWRTC_DEVICE_STRING_SIZE] = { 0 };
			if (f->video->GetDeviceName(i, name, sizeof(name), id, sizeof(id)) != 0)
				continue;

			Device d;
			d.kind = LWRTC_DEVICE_VIDEO_INPUT;
			d.index = i;
			d.name = name;
			d.id = id[0] ? id : name;
			d.label = name;
			out.push_back(d);
		}
		return out;
	}

	bool is_default(const char* id)
	{
		return !id || !*id || strcmp(id, "default") == 0;
	}

	/** the device with this id, or the default one (the first) */
	const Device* find_device(const std::vector<Device>& devices, const char* id)
	{
		for (const Device& d : devices)
		{
			if (is_default(id) || d.id == id)
				return &d;
		}
		return nullptr;
	}

	void copy_string(char* dst, const std::string& src)
	{
		strncpy(dst, src.c_str(), LWRTC_DEVICE_STRING_SIZE - 1);
		dst[LWRTC_DEVICE_STRING_SIZE - 1] = 0;
	}
} // namespace

lwrtc_device_info* lwrtc_factory_enumerate_devices(lwrtc_factory* factory, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	if (!factory)
		return nullptr;

	std::vector<Device> all = audio_devices(factory, true);
	for (const Device& d : audio_devices(factory, false))
		all.push_back(d);
	for (const Device& d : video_devices(factory))
		all.push_back(d);

	auto* out = static_cast<lwrtc_device_info*>(calloc(all.size() + 1, sizeof(lwrtc_device_info)));
	if (!out)
		return nullptr;
	for (size_t i = 0; i < all.size(); i++)
	{
		out[i].kind = all[i].kind;
		copy_string(out[i].device_id, all[i].id);
		copy_string(out[i].label, all[i].label);
	}
	*count = all.size();
	return out;
}

lwrtc_result lwrtc_factory_set_device_change_callback(lwrtc_factory* factory,
                                                      lwrtc_device_change_fn fn, void* user)
{
	if (!factory || !factory->audio)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	{
		std::lock_guard<std::mutex> guard(factory->lock);
		factory->deviceChange = fn;
		factory->deviceChangeUser = user;
	}

	if (!fn)
		return factory->audio->OnDeviceChange(nullptr) == 0 ? LWRTC_OK : LWRTC_ERROR_FAILED;

	lwrtc_factory* f = factory;
	return factory->audio->OnDeviceChange(
	           [f]()
	           {
		           std::lock_guard<std::mutex> guard(f->lock);
		           if (f->deviceChange)
			           f->deviceChange(f->deviceChangeUser);
	           }) == 0
	           ? LWRTC_OK
	           : LWRTC_ERROR_FAILED;
}

lwrtc_result lwrtc_factory_set_playout_device(lwrtc_factory* factory, const char* device_id)
{
	if (!factory || !factory->audio)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	const std::vector<Device> devices = audio_devices(factory, false);
	const Device* d = find_device(devices, device_id);
	if (!d)
		return LWRTC_ERROR_NOT_FOUND;
	return factory->audio->SetPlayoutDevice(static_cast<uint16_t>(d->index)) == 0
	           ? LWRTC_OK
	           : LWRTC_ERROR_FAILED;
}

static std::string track_id(const char* id)
{
	return (id && *id) ? std::string(id) : lwrtc::str(Helper::CreateRandomUuid());
}

lwrtc_media_track* lwrtc_factory_create_microphone_track(lwrtc_factory* factory,
                                                         const char* device_id,
                                                         const lwrtc_audio_options* options,
                                                         const char* track_id_)
{
	if (!factory)
		return nullptr;

	const std::vector<Device> devices = audio_devices(factory, true);
	const Device* d = find_device(devices, device_id);
	if (!d)
		return nullptr;
	if (factory->audio->SetRecordingDevice(static_cast<uint16_t>(d->index)) != 0)
		return nullptr;

	RTCAudioOptions opts;
	if (options)
	{
		opts.echo_cancellation = options->echo_cancellation;
		opts.auto_gain_control = options->auto_gain_control;
		opts.noise_suppression = options->noise_suppression;
		opts.highpass_filter = options->highpass_filter;
	}

	auto source = factory->factory->CreateAudioSource(
	    lwrtc::pstr(d->label.c_str()), RTCAudioSource::SourceType::kMicrophone, opts);
	if (!source)
		return nullptr;
	auto track =
	    factory->factory->CreateAudioTrack(source, lwrtc::pstr(track_id(track_id_).c_str()));
	if (!track)
		return nullptr;

	lwrtc_media_track* t = lwrtc::wrap_track(track, false);
	t->audioSource = source;
	t->label = d->label;
	t->deviceId = d->id;
	return t;
}

lwrtc_media_track* lwrtc_factory_create_camera_track(lwrtc_factory* factory, const char* device_id,
                                                     uint32_t width, uint32_t height, uint32_t fps,
                                                     const char* track_id_)
{
	if (!factory || !factory->video)
		return nullptr;

	const std::vector<Device> devices = video_devices(factory);
	const Device* d = nullptr;
	for (const Device& dev : devices)
	{
		if (!device_id || !*device_id || dev.id == device_id)
		{
			d = &dev;
			break;
		}
	}
	if (!d)
		return nullptr;

	if (!width || !height)
	{
		width = 640;
		height = 480;
	}
	if (!fps)
		fps = 30;

	auto capturer = factory->video->Create(d->name.c_str(), d->index, width, height, fps);
	if (!capturer)
		return nullptr;
	auto source = factory->factory->CreateVideoSource(capturer, lwrtc::pstr(d->label.c_str()),
	                                                  RTCMediaConstraints::Create());
	if (!source)
		return nullptr;
	if (!capturer->CaptureStarted() && !capturer->StartCapture())
		return nullptr;
	auto track =
	    factory->factory->CreateVideoTrack(source, lwrtc::pstr(track_id(track_id_).c_str()));
	if (!track)
	{
		capturer->StopCapture();
		return nullptr;
	}

	lwrtc_media_track* t = lwrtc::wrap_track(track, false);
	t->videoSource = source;
	t->capturer = capturer;
	t->label = d->label;
	t->deviceId = d->id;
	return t;
}

/* ---- tracks ---- */

lwrtc_media_track* lwrtc::wrap_track(scoped_refptr<RTCMediaTrack> track, bool remote)
{
	if (!track)
		return nullptr;
	auto* t = new lwrtc_media_track();
	t->track = track;
	t->remote = remote;
	return t;
}

lwrtc_media_track::~lwrtc_media_track()
{
	/* the last handle on a camera track stops the camera */
	if (capturer && capturer->CaptureStarted())
		capturer->StopCapture();
}

lwrtc_media_kind lwrtc_media_track_kind(lwrtc_media_track* track)
{
	if (!track)
		return LWRTC_MEDIA_UNSUPPORTED;
	lwrtc_media_kind kind = LWRTC_MEDIA_UNSUPPORTED;
	lwrtc_media_kind_from_name(lwrtc::str(track->track->kind()).c_str(), &kind);
	return kind;
}

char* lwrtc_media_track_id(lwrtc_media_track* track)
{
	return track ? lwrtc::dup(track->track->id()) : nullptr;
}

char* lwrtc_media_track_label(lwrtc_media_track* track)
{
	return track ? lwrtc::dup(track->label) : nullptr;
}

char* lwrtc_media_track_device_id(lwrtc_media_track* track)
{
	return track ? lwrtc::dup(track->deviceId) : nullptr;
}

bool lwrtc_media_track_is_remote(lwrtc_media_track* track)
{
	return track && track->remote;
}

bool lwrtc_media_track_enabled(lwrtc_media_track* track)
{
	return track && track->track->enabled();
}

void lwrtc_media_track_set_enabled(lwrtc_media_track* track, bool enabled)
{
	if (track && !track->stopped)
		track->track->set_enabled(enabled);
}

lwrtc_track_state lwrtc_media_track_state(lwrtc_media_track* track)
{
	if (!track || track->stopped || track->track->state() == RTCMediaTrack::kEnded)
		return LWRTC_TRACK_ENDED;
	return LWRTC_TRACK_LIVE;
}

void lwrtc_media_track_stop(lwrtc_media_track* track)
{
	if (!track || track->stopped.exchange(true))
		return;
	track->track->set_enabled(false);
	if (track->capturer && track->capturer->CaptureStarted())
		track->capturer->StopCapture();
}

lwrtc_result lwrtc_audio_track_set_volume(lwrtc_media_track* track, double volume)
{
	RTCAudioTrack* audio = track ? track->audio() : nullptr;
	if (!audio)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	/* WebRTC's gain: 0 to 10, 1 being the volume received */
	audio->SetVolume(volume < 0 ? 0 : (volume > 1 ? 1 : volume));
	return LWRTC_OK;
}

/* ---- sinks ---- */

struct lwrtc_audio_sink : AudioTrackSink
{
	scoped_refptr<RTCAudioTrack> track;
	lwrtc_audio_data_fn fn = nullptr;
	void* user = nullptr;

	void OnData(const void* audio_data, int bits_per_sample, int sample_rate,
	            size_t number_of_channels, size_t number_of_frames) override
	{
		fn(user, audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
	}
};

lwrtc_audio_sink* lwrtc_audio_track_add_sink(lwrtc_media_track* track, lwrtc_audio_data_fn fn,
                                             void* user)
{
	RTCAudioTrack* audio = track ? track->audio() : nullptr;
	if (!audio || !fn)
		return nullptr;

	auto* sink = new lwrtc_audio_sink();
	sink->track = audio;
	sink->fn = fn;
	sink->user = user;
	audio->AddSink(sink);
	return sink;
}

void lwrtc_audio_sink_remove(lwrtc_audio_sink* sink)
{
	if (!sink)
		return;
	sink->track->RemoveSink(sink);
	delete sink;
}

struct lwrtc_video_sink : RTCVideoRenderer<scoped_refptr<RTCVideoFrame>>
{
	scoped_refptr<RTCVideoTrack> track;
	lwrtc_video_frame_fn fn = nullptr;
	void* user = nullptr;

	void OnFrame(scoped_refptr<RTCVideoFrame> frame) override
	{
		auto* f = new lwrtc_video_frame();
		f->frame = frame;
		fn(user, f);
		lwrtc::release(f);
	}
};

lwrtc_video_sink* lwrtc_video_track_add_sink(lwrtc_media_track* track, lwrtc_video_frame_fn fn,
                                             void* user)
{
	RTCVideoTrack* video = track ? track->video() : nullptr;
	if (!video || !fn)
		return nullptr;

	auto* sink = new lwrtc_video_sink();
	sink->track = video;
	sink->fn = fn;
	sink->user = user;
	video->AddRenderer(sink);
	return sink;
}

void lwrtc_video_sink_remove(lwrtc_video_sink* sink)
{
	if (!sink)
		return;
	sink->track->RemoveRenderer(sink);
	delete sink;
}

/* ---- video frames ---- */

lwrtc_video_frame* lwrtc_video_frame_new_i420(int width, int height, const uint8_t* y, int stride_y,
                                              const uint8_t* u, int stride_u, const uint8_t* v,
                                              int stride_v)
{
	if (width <= 0 || height <= 0 || !y || !u || !v)
		return nullptr;
	auto frame = RTCVideoFrame::Create(width, height, y, stride_y, u, stride_u, v, stride_v);
	if (!frame)
		return nullptr;
	auto* f = new lwrtc_video_frame();
	f->frame = frame;
	return f;
}

int lwrtc_video_frame_width(lwrtc_video_frame* frame)
{
	return frame ? frame->frame->width() : 0;
}

int lwrtc_video_frame_height(lwrtc_video_frame* frame)
{
	return frame ? frame->frame->height() : 0;
}

int lwrtc_video_frame_rotation(lwrtc_video_frame* frame)
{
	return frame ? static_cast<int>(frame->frame->rotation()) : 0;
}

void lwrtc_video_frame_i420(lwrtc_video_frame* frame, const uint8_t** y, int* stride_y,
                            const uint8_t** u, int* stride_u, const uint8_t** v, int* stride_v)
{
	if (!frame)
		return;
	const RTCVideoFrame* f = frame->frame.get();
	if (y)
		*y = f->DataY();
	if (stride_y)
		*stride_y = f->StrideY();
	if (u)
		*u = f->DataU();
	if (stride_u)
		*stride_u = f->StrideU();
	if (v)
		*v = f->DataV();
	if (stride_v)
		*stride_v = f->StrideV();
}

lwrtc_result lwrtc_video_frame_convert(lwrtc_video_frame* frame, lwrtc_pixel_format format,
                                       uint8_t* dst, int dst_stride, int width, int height)
{
	if (!frame || !dst || width != frame->frame->width() || height != frame->frame->height() ||
	    dst_stride < width * 4)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	/* the wrapper's types are libyuv's names, which give the bytes in reverse: kARGB writes
	 * B, G, R, A */
	RTCVideoFrame::Type type;
	switch (format)
	{
		case LWRTC_PIXEL_BGRA32:
			type = RTCVideoFrame::Type::kARGB;
			break;
		case LWRTC_PIXEL_RGBA32:
			type = RTCVideoFrame::Type::kABGR;
			break;
		default:
			return LWRTC_ERROR_INVALID_ARGUMENT;
	}
	return frame->frame->ConvertToARGB(type, dst, dst_stride, width, height) >= 0
	           ? LWRTC_OK
	           : LWRTC_ERROR_FAILED;
}
