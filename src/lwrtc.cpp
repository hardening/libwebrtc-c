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

/* library setup, logging, strings and enums */

#include "internal.h"

#include "helper.h"
#include "rtc_logging.h"

using namespace libwebrtc;

namespace lwrtc
{
	char* dup(const std::string& s)
	{
		char* out = static_cast<char*>(malloc(s.size() + 1));
		if (out)
			memcpy(out, s.c_str(), s.size() + 1);
		return out;
	}

	char** dup_strings(const vector<string>& v, size_t* count)
	{
		*count = v.size();
		char** out = static_cast<char**>(calloc(v.size() + 1, sizeof(char*)));
		if (!out)
		{
			*count = 0;
			return nullptr;
		}
		for (size_t i = 0; i < v.size(); i++)
			out[i] = dup(v[i]);
		return out;
	}

	vector<string> pstrings(const char* const* strings, size_t count)
	{
		std::vector<string> v;
		for (size_t i = 0; i < count; i++)
			v.push_back(pstr(strings[i]));
		return vector<string>(v);
	}

	lwrtc_media_kind kind(RTCMediaType type)
	{
		switch (type)
		{
			case RTCMediaType::AUDIO:
				return LWRTC_MEDIA_AUDIO;
			case RTCMediaType::VIDEO:
				return LWRTC_MEDIA_VIDEO;
			case RTCMediaType::DATA:
				return LWRTC_MEDIA_DATA;
			default:
				return LWRTC_MEDIA_UNSUPPORTED;
		}
	}

	RTCMediaType media_type(lwrtc_media_kind kind)
	{
		switch (kind)
		{
			case LWRTC_MEDIA_AUDIO:
				return RTCMediaType::AUDIO;
			case LWRTC_MEDIA_VIDEO:
				return RTCMediaType::VIDEO;
			case LWRTC_MEDIA_DATA:
				return RTCMediaType::DATA;
			default:
				return RTCMediaType::UNSUPPORTED;
		}
	}

	/* same order on both sides */
	lwrtc_direction direction(RTCRtpTransceiverDirection d)
	{
		return static_cast<lwrtc_direction>(d);
	}

	RTCRtpTransceiverDirection direction(lwrtc_direction d)
	{
		return static_cast<RTCRtpTransceiverDirection>(d);
	}
} // namespace lwrtc

void lwrtc_free(void* ptr)
{
	free(ptr);
}

/* ---- library ---- */

static std::mutex g_logLock;
static lwrtc_log_fn g_logFn = nullptr;
static void* g_logUser = nullptr;

static void logSink(const string& message)
{
	std::lock_guard<std::mutex> guard(g_logLock);
	if (g_logFn)
		g_logFn(g_logUser, lwrtc::str(message).c_str());
}

bool lwrtc_initialize(const char* const* field_trials, size_t count)
{
	if (field_trials && count)
	{
		LibWebRTC::InitializeWithFieldTrials(lwrtc::pstrings(field_trials, count));
		return true;
	}
	return LibWebRTC::Initialize();
}

void lwrtc_terminate(void)
{
	LibWebRTC::Terminate();
}

void lwrtc_set_log_sink(lwrtc_log_level min_level, lwrtc_log_fn fn, void* user)
{
	{
		std::lock_guard<std::mutex> guard(g_logLock);
		g_logFn = fn;
		g_logUser = user;
	}
	if (fn)
		LibWebRTCLogging::setLogSink(static_cast<RTCLoggingSeverity>(min_level), logSink);
	else
		LibWebRTCLogging::removeLogSink();
}

char* lwrtc_create_uuid(void)
{
	return lwrtc::dup(Helper::CreateRandomUuid());
}

/* ---- enum names ---- */

template <size_t N> static const char* name_of(const char* const (&names)[N], int value)
{
	return (value >= 0 && static_cast<size_t>(value) < N) ? names[value] : "";
}

template <size_t N, typename E>
static bool from_name(const char* const (&names)[N], const char* name, E* out)
{
	if (!name || !out)
		return false;
	for (size_t i = 0; i < N; i++)
	{
		if (strcmp(names[i], name) == 0)
		{
			*out = static_cast<E>(i);
			return true;
		}
	}
	return false;
}

static const char* const kKinds[] = { "audio", "video", "data", "" };
static const char* const kSignaling[] = { "stable",
	                                      "have-local-offer",
	                                      "have-remote-offer",
	                                      "have-local-pranswer",
	                                      "have-remote-pranswer",
	                                      "closed" };
static const char* const kConnection[] = { "new",          "connecting", "connected",
	                                       "disconnected", "failed",     "closed" };
static const char* const kIceConnection[] = { "new",    "checking",     "connected", "completed",
	                                          "failed", "disconnected", "closed" };
static const char* const kIceGathering[] = { "new", "gathering", "complete" };
static const char* const kDirections[] = { "sendrecv", "sendonly", "recvonly", "inactive",
	                                       "stopped" };
static const char* const kDataChannel[] = { "connecting", "open", "closing", "closed" };
static const char* const kTrack[] = { "live", "ended" };
static const char* const kPriority[] = { "very-low", "low", "medium", "high" };
static const char* const kDegradation[] = { "disabled", "maintain-framerate", "maintain-resolution",
	                                        "balanced", "maintain-framerate-and-resolution" };

const char* lwrtc_media_kind_name(lwrtc_media_kind kind)
{
	return name_of(kKinds, kind);
}

const char* lwrtc_signaling_state_name(lwrtc_signaling_state state)
{
	return name_of(kSignaling, state);
}

const char* lwrtc_peer_connection_state_name(lwrtc_peer_connection_state state)
{
	return name_of(kConnection, state);
}

const char* lwrtc_ice_connection_state_name(lwrtc_ice_connection_state state)
{
	return name_of(kIceConnection, state);
}

const char* lwrtc_ice_gathering_state_name(lwrtc_ice_gathering_state state)
{
	return name_of(kIceGathering, state);
}

const char* lwrtc_direction_name(lwrtc_direction direction)
{
	return name_of(kDirections, direction);
}

const char* lwrtc_data_channel_state_name(lwrtc_data_channel_state state)
{
	return name_of(kDataChannel, state);
}

const char* lwrtc_track_state_name(lwrtc_track_state state)
{
	return name_of(kTrack, state);
}

const char* lwrtc_priority_name(lwrtc_priority priority)
{
	return name_of(kPriority, priority);
}

const char* lwrtc_degradation_preference_name(lwrtc_degradation_preference pref)
{
	return name_of(kDegradation, pref);
}

bool lwrtc_media_kind_from_name(const char* name, lwrtc_media_kind* kind)
{
	return (name && *name) ? from_name(kKinds, name, kind) : false;
}

bool lwrtc_direction_from_name(const char* name, lwrtc_direction* direction)
{
	return from_name(kDirections, name, direction);
}

bool lwrtc_priority_from_name(const char* name, lwrtc_priority* priority)
{
	return from_name(kPriority, name, priority);
}

bool lwrtc_degradation_preference_from_name(const char* name, lwrtc_degradation_preference* pref)
{
	return from_name(kDegradation, name, pref);
}
