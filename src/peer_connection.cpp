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

/* RTCPeerConnection and getStats */

#include "internal.h"

#include <charconv>

using namespace libwebrtc;

/* ---- state conversions ----
 * The wrapper translates WebRTC's states into its own enums, whose order isn't always WebRTC's
 * (nor ours): convert by meaning. */

static lwrtc_signaling_state signaling_state(RTCSignalingState s)
{
	switch (s)
	{
		case RTCSignalingStateStable:
			return LWRTC_SIGNALING_STABLE;
		case RTCSignalingStateHaveLocalOffer:
			return LWRTC_SIGNALING_HAVE_LOCAL_OFFER;
		case RTCSignalingStateHaveRemoteOffer:
			return LWRTC_SIGNALING_HAVE_REMOTE_OFFER;
		case RTCSignalingStateHaveLocalPrAnswer:
			return LWRTC_SIGNALING_HAVE_LOCAL_PRANSWER;
		case RTCSignalingStateHaveRemotePrAnswer:
			return LWRTC_SIGNALING_HAVE_REMOTE_PRANSWER;
		default:
			return LWRTC_SIGNALING_CLOSED;
	}
}

static lwrtc_ice_connection_state ice_connection_state(RTCIceConnectionState s)
{
	switch (s)
	{
		case RTCIceConnectionStateNew:
			return LWRTC_ICE_CONNECTION_NEW;
		case RTCIceConnectionStateChecking:
			return LWRTC_ICE_CONNECTION_CHECKING;
		case RTCIceConnectionStateConnected:
			return LWRTC_ICE_CONNECTION_CONNECTED;
		case RTCIceConnectionStateCompleted:
			return LWRTC_ICE_CONNECTION_COMPLETED;
		case RTCIceConnectionStateFailed:
			return LWRTC_ICE_CONNECTION_FAILED;
		case RTCIceConnectionStateDisconnected:
			return LWRTC_ICE_CONNECTION_DISCONNECTED;
		default:
			return LWRTC_ICE_CONNECTION_CLOSED;
	}
}

static lwrtc_peer_connection_state connection_state(RTCPeerConnectionState s)
{
	return static_cast<lwrtc_peer_connection_state>(s);
}

static lwrtc_ice_gathering_state gathering_state(RTCIceGatheringState s)
{
	return static_cast<lwrtc_ice_gathering_state>(s);
}

/* ---- the peer connection ---- */

struct lwrtc_peer_connection : lwrtc::Object, RTCPeerConnectionObserver
{
	lwrtc_factory* factory = nullptr; /* a reference */
	scoped_refptr<RTCPeerConnection> pc;
	lwrtc_peer_connection_observer observer{};

	/* held while an event is delivered: close() waits for it, and none starts after */
	std::recursive_mutex lock;
	std::atomic<bool> closed{ false };

	~lwrtc_peer_connection() override
	{
		close();
		lwrtc::release(factory);
	}

	void close()
	{
		{
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (closed)
				return;
			closed = true;
		}
		/* not under the lock: Close() waits for WebRTC's signalling thread, which may be
		 * about to deliver an event */
		pc->DeRegisterRTCPeerConnectionObserver();
		pc->Close();
		factory->factory->Delete(pc);
	}

	template <typename F> void deliver(F f)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!closed)
			f(observer);
	}

	void OnSignalingState(RTCSignalingState state) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_signaling_state)
				    o.on_signaling_state(o.user, signaling_state(state));
		    });
	}

	void OnPeerConnectionState(RTCPeerConnectionState state) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_connection_state)
				    o.on_connection_state(o.user, connection_state(state));
		    });
	}

	void OnIceGatheringState(RTCIceGatheringState state) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_ice_gathering_state)
				    o.on_ice_gathering_state(o.user, gathering_state(state));
		    });
	}

	void OnIceConnectionState(RTCIceConnectionState state) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_ice_connection_state)
				    o.on_ice_connection_state(o.user, ice_connection_state(state));
		    });
	}

	void OnIceCandidate(scoped_refptr<RTCIceCandidate> candidate) override
	{
		if (!candidate)
			return;
		const std::string sdp = lwrtc::str(candidate->candidate());
		const std::string mid = lwrtc::str(candidate->sdp_mid());
		const int index = candidate->sdp_mline_index();
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_ice_candidate)
				    o.on_ice_candidate(o.user, sdp.c_str(), mid.c_str(), index);
		    });
	}

	void OnAddStream(scoped_refptr<RTCMediaStream>) override
	{
	}

	void OnRemoveStream(scoped_refptr<RTCMediaStream>) override
	{
	}

	void OnDataChannel(scoped_refptr<RTCDataChannel> data_channel) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (!o.on_data_channel)
				    return;
			    lwrtc_data_channel* dc = lwrtc::wrap_data_channel(data_channel);
			    o.on_data_channel(o.user, dc);
			    lwrtc::release(dc);
		    });
	}

	void OnRenegotiationNeeded() override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (o.on_negotiation_needed)
				    o.on_negotiation_needed(o.user);
		    });
	}

	void OnTrack(scoped_refptr<RTCRtpTransceiver> transceiver) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (!o.on_track || !transceiver)
				    return;

			    std::vector<std::string> ids;
			    if (auto receiver = transceiver->receiver())
			    {
				    const vector<string> streamIds = receiver->stream_ids();
				    for (size_t i = 0; i < streamIds.size(); i++)
					    ids.push_back(lwrtc::str(streamIds[i]));
			    }
			    std::vector<const char*> cids;
			    for (const std::string& id : ids)
				    cids.push_back(id.c_str());

			    lwrtc_rtp_transceiver* t = lwrtc::wrap_transceiver(transceiver);
			    o.on_track(o.user, t, cids.data(), cids.size());
			    lwrtc::release(t);
		    });
	}

	/* with unified plan, OnTrack() tells the same */
	void OnAddTrack(vector<scoped_refptr<RTCMediaStream>>, scoped_refptr<RTCRtpReceiver>) override
	{
	}

	void OnRemoveTrack(scoped_refptr<RTCRtpReceiver> receiver) override
	{
		deliver(
		    [&](lwrtc_peer_connection_observer& o)
		    {
			    if (!o.on_remove_track || !receiver)
				    return;
			    lwrtc_rtp_receiver* r = lwrtc::wrap_receiver(receiver);
			    o.on_remove_track(o.user, r);
			    lwrtc::release(r);
		    });
	}
};

/** the wrapper's connection can't be used once closed: its native one is gone */
static bool usable(const lwrtc_peer_connection* pc)
{
	return pc && !pc->closed;
}

static const char kClosed[] = "InvalidStateError: the peer connection is closed";

lwrtc_peer_connection* lwrtc_peer_connection_add_ref(lwrtc_peer_connection* pc)
{
	return lwrtc::add_ref(pc);
}

void lwrtc_peer_connection_release(lwrtc_peer_connection* pc)
{
	lwrtc::release(pc);
}

lwrtc_peer_connection* lwrtc_peer_connection_new(lwrtc_factory* factory,
                                                 const lwrtc_configuration* configuration,
                                                 const lwrtc_peer_connection_observer* observer)
{
	if (!factory)
		return nullptr;

	RTCConfiguration config;
	/* browsers only offer the transceivers asked for */
	config.offer_to_receive_audio = false;
	config.offer_to_receive_video = false;
	config.bundle_policy = kBundlePolicyBalanced;
	if (configuration)
	{
		size_t n = 0;
		for (size_t i = 0; i < configuration->ice_server_count; i++)
		{
			const lwrtc_ice_server& s = configuration->ice_servers[i];
			for (size_t j = 0; j < s.url_count; j++)
			{
				if (n >= kMaxIceServerSize)
					return nullptr;
				config.ice_servers[n].uri = lwrtc::pstr(s.urls[j]);
				config.ice_servers[n].username = lwrtc::pstr(s.username);
				config.ice_servers[n].password = lwrtc::pstr(s.credential);
				n++;
			}
		}
		config.type = configuration->ice_transport_policy == LWRTC_ICE_TRANSPORT_RELAY
		                  ? IceTransportsType::kRelay
		                  : IceTransportsType::kAll;
		switch (configuration->bundle_policy)
		{
			case LWRTC_BUNDLE_MAX_BUNDLE:
				config.bundle_policy = kBundlePolicyMaxBundle;
				break;
			case LWRTC_BUNDLE_MAX_COMPAT:
				config.bundle_policy = kBundlePolicyMaxCompat;
				break;
			default:
				break;
		}
		config.ice_candidate_pool_size = configuration->ice_candidate_pool_size;
	}

	auto* pc = new lwrtc_peer_connection();
	pc->factory = lwrtc::add_ref(factory);
	if (observer)
		pc->observer = *observer;
	pc->pc = factory->factory->Create(config, RTCMediaConstraints::Create());
	if (!pc->pc)
	{
		pc->closed = true;
		lwrtc::release(pc);
		return nullptr;
	}
	pc->pc->RegisterRTCPeerConnectionObserver(pc);
	return pc;
}

void lwrtc_peer_connection_close(lwrtc_peer_connection* pc)
{
	if (pc)
		pc->close();
}

/* ---- offer / answer ---- */

static scoped_refptr<RTCMediaConstraints> offer_constraints(const lwrtc_offer_options* options)
{
	auto constraints = RTCMediaConstraints::Create();
	if (options && options->ice_restart)
		constraints->AddMandatoryConstraint(RTCMediaConstraints::kIceRestart,
		                                    RTCMediaConstraints::kValueTrue);
	if (options && !options->voice_activity_detection)
		constraints->AddMandatoryConstraint(RTCMediaConstraints::kVoiceActivityDetection,
		                                    RTCMediaConstraints::kValueFalse);
	return constraints;
}

void lwrtc_peer_connection_create_offer(lwrtc_peer_connection* pc,
                                        const lwrtc_offer_options* options, lwrtc_description_fn fn,
                                        void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
	{
		fn(user, nullptr, nullptr, kClosed);
		return;
	}
	pc->pc->CreateOffer([fn, user](const string sdp, const string type)
	                    { fn(user, lwrtc::str(type).c_str(), lwrtc::str(sdp).c_str(), nullptr); },
	                    [fn, user](const char* error)
	                    { fn(user, nullptr, nullptr, error ? error : "createOffer failed"); },
	                    offer_constraints(options));
}

void lwrtc_peer_connection_create_answer(lwrtc_peer_connection* pc, lwrtc_description_fn fn,
                                         void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
	{
		fn(user, nullptr, nullptr, kClosed);
		return;
	}
	pc->pc->CreateAnswer([fn, user](const string sdp, const string type)
	                     { fn(user, lwrtc::str(type).c_str(), lwrtc::str(sdp).c_str(), nullptr); },
	                     [fn, user](const char* error)
	                     { fn(user, nullptr, nullptr, error ? error : "createAnswer failed"); },
	                     RTCMediaConstraints::Create());
}

static void set_local(lwrtc_peer_connection* pc, const string& type, const string& sdp,
                      lwrtc_done_fn fn, void* user)
{
	pc->pc->SetLocalDescription(
	    sdp, type,
	    [fn, user]()
	    {
		    if (fn)
			    fn(user, nullptr);
	    },
	    [fn, user](const char* error)
	    {
		    if (fn)
			    fn(user, error ? error : "setLocalDescription failed");
	    });
}

void lwrtc_peer_connection_set_local_description(lwrtc_peer_connection* pc, const char* type,
                                                 const char* sdp, lwrtc_done_fn fn, void* user)
{
	if (!pc)
		return;
	if (!usable(pc))
	{
		if (fn)
			fn(user, kClosed);
		return;
	}
	if (sdp && *sdp)
	{
		set_local(pc, lwrtc::pstr(type), lwrtc::pstr(sdp), fn, user);
		return;
	}

	/* implicit: the offer or answer the state calls for. The pc is kept until it is set. */
	lwrtc::add_ref(pc);
	auto onCreated = [pc, fn, user](const string sdp_, const string type_)
	{
		if (usable(pc))
			set_local(pc, type_, sdp_, fn, user);
		else if (fn)
			fn(user, kClosed);
		lwrtc::release(pc);
	};
	auto onFailed = [pc, fn, user](const char* error)
	{
		if (fn)
			fn(user, error ? error : "setLocalDescription failed");
		lwrtc::release(pc);
	};

	const lwrtc_signaling_state state = signaling_state(pc->pc->signaling_state());
	if (state == LWRTC_SIGNALING_HAVE_REMOTE_OFFER || state == LWRTC_SIGNALING_HAVE_LOCAL_PRANSWER)
		pc->pc->CreateAnswer(onCreated, onFailed, RTCMediaConstraints::Create());
	else
		pc->pc->CreateOffer(onCreated, onFailed, RTCMediaConstraints::Create());
}

void lwrtc_peer_connection_set_remote_description(lwrtc_peer_connection* pc, const char* type,
                                                  const char* sdp, lwrtc_done_fn fn, void* user)
{
	if (!pc)
		return;
	if (!usable(pc))
	{
		if (fn)
			fn(user, kClosed);
		return;
	}
	pc->pc->SetRemoteDescription(
	    lwrtc::pstr(sdp), lwrtc::pstr(type),
	    [fn, user]()
	    {
		    if (fn)
			    fn(user, nullptr);
	    },
	    [fn, user](const char* error)
	    {
		    if (fn)
			    fn(user, error ? error : "setRemoteDescription failed");
	    });
}

static void get_description(lwrtc_peer_connection* pc, bool local, lwrtc_description_fn fn,
                            void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
	{
		fn(user, nullptr, nullptr, nullptr);
		return;
	}
	auto ok = [fn, user](const char* sdp, const char* type)
	{
		if (sdp && *sdp)
			fn(user, type, sdp, nullptr);
		else
			fn(user, nullptr, nullptr, nullptr);
	};
	/* no description is not an error */
	auto failed = [fn, user](const char*) { fn(user, nullptr, nullptr, nullptr); };
	if (local)
		pc->pc->GetLocalDescription(ok, failed);
	else
		pc->pc->GetRemoteDescription(ok, failed);
}

void lwrtc_peer_connection_get_local_description(lwrtc_peer_connection* pc, lwrtc_description_fn fn,
                                                 void* user)
{
	get_description(pc, true, fn, user);
}

void lwrtc_peer_connection_get_remote_description(lwrtc_peer_connection* pc,
                                                  lwrtc_description_fn fn, void* user)
{
	get_description(pc, false, fn, user);
}

lwrtc_result lwrtc_peer_connection_add_ice_candidate(lwrtc_peer_connection* pc,
                                                     const char* candidate, const char* sdp_mid,
                                                     int sdp_mline_index)
{
	if (!pc)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	if (!usable(pc))
		return LWRTC_ERROR_INVALID_STATE;
	if (!candidate || !*candidate)
		return LWRTC_OK;

	/* AddCandidate() reports nothing: check the candidate first */
	SdpParseError error;
	if (!RTCIceCandidate::Create(lwrtc::pstr(candidate), lwrtc::pstr(sdp_mid), sdp_mline_index,
	                             &error))
		return LWRTC_ERROR_INVALID_ARGUMENT;
	pc->pc->AddCandidate(lwrtc::pstr(sdp_mid), sdp_mline_index, lwrtc::pstr(candidate));
	return LWRTC_OK;
}

void lwrtc_peer_connection_restart_ice(lwrtc_peer_connection* pc)
{
	if (usable(pc))
		pc->pc->RestartIce();
}

lwrtc_signaling_state lwrtc_peer_connection_signaling_state(lwrtc_peer_connection* pc)
{
	return usable(pc) ? signaling_state(pc->pc->signaling_state()) : LWRTC_SIGNALING_CLOSED;
}

lwrtc_peer_connection_state lwrtc_peer_connection_connection_state(lwrtc_peer_connection* pc)
{
	return usable(pc) ? connection_state(pc->pc->peer_connection_state())
	                  : LWRTC_PEER_CONNECTION_CLOSED;
}

lwrtc_ice_connection_state lwrtc_peer_connection_ice_connection_state(lwrtc_peer_connection* pc)
{
	return usable(pc) ? ice_connection_state(pc->pc->standardized_ice_connection_state())
	                  : LWRTC_ICE_CONNECTION_CLOSED;
}

lwrtc_ice_gathering_state lwrtc_peer_connection_ice_gathering_state(lwrtc_peer_connection* pc)
{
	return usable(pc) ? gathering_state(pc->pc->ice_gathering_state())
	                  : LWRTC_ICE_GATHERING_COMPLETE;
}

/* ---- tracks and transceivers ---- */

lwrtc_rtp_sender* lwrtc_peer_connection_add_track(lwrtc_peer_connection* pc,
                                                  lwrtc_media_track* track,
                                                  const char* const* stream_ids,
                                                  size_t stream_count)
{
	if (!usable(pc) || !track)
		return nullptr;
	return lwrtc::wrap_sender(
	    pc->pc->AddTrack(track->track, lwrtc::pstrings(stream_ids, stream_count)));
}

lwrtc_result lwrtc_peer_connection_remove_track(lwrtc_peer_connection* pc, lwrtc_rtp_sender* sender)
{
	if (!pc || !sender)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	if (!usable(pc))
		return LWRTC_ERROR_INVALID_STATE;
	return pc->pc->RemoveTrack(sender->sender) ? LWRTC_OK : LWRTC_ERROR_FAILED;
}

lwrtc_rtp_transceiver* lwrtc_peer_connection_add_transceiver(lwrtc_peer_connection* pc,
                                                             lwrtc_media_track* track,
                                                             lwrtc_media_kind kind,
                                                             const lwrtc_transceiver_init* init)
{
	if (!usable(pc))
		return nullptr;

	scoped_refptr<RTCRtpTransceiverInit> tinit;
	if (init)
	{
		std::vector<scoped_refptr<RTCRtpEncodingParameters>> encodings;
		for (size_t i = 0; i < init->send_encoding_count; i++)
			encodings.push_back(lwrtc::make_encoding(init->send_encodings[i]));
		tinit = RTCRtpTransceiverInit::Create(
		    lwrtc::direction(init->direction),
		    lwrtc::pstrings(init->stream_ids, init->stream_count),
		    vector<scoped_refptr<RTCRtpEncodingParameters>>(encodings));
	}

	scoped_refptr<RTCRtpTransceiver> t;
	if (track)
		t = tinit ? pc->pc->AddTransceiver(track->track, tinit)
		          : pc->pc->AddTransceiver(track->track);
	else if (kind == LWRTC_MEDIA_AUDIO || kind == LWRTC_MEDIA_VIDEO)
		t = tinit ? pc->pc->AddTransceiver(lwrtc::media_type(kind), tinit)
		          : pc->pc->AddTransceiver(lwrtc::media_type(kind));
	return lwrtc::wrap_transceiver(t);
}

lwrtc_rtp_sender** lwrtc_peer_connection_senders(lwrtc_peer_connection* pc, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	if (!usable(pc))
		return nullptr;
	const auto v = pc->pc->senders();
	std::vector<lwrtc_rtp_sender*> out;
	for (size_t i = 0; i < v.size(); i++)
		out.push_back(lwrtc::wrap_sender(v[i]));
	return lwrtc::dup_handles(out, count);
}

lwrtc_rtp_receiver** lwrtc_peer_connection_receivers(lwrtc_peer_connection* pc, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	if (!usable(pc))
		return nullptr;
	const auto v = pc->pc->receivers();
	std::vector<lwrtc_rtp_receiver*> out;
	for (size_t i = 0; i < v.size(); i++)
		out.push_back(lwrtc::wrap_receiver(v[i]));
	return lwrtc::dup_handles(out, count);
}

lwrtc_rtp_transceiver** lwrtc_peer_connection_transceivers(lwrtc_peer_connection* pc, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	if (!usable(pc))
		return nullptr;
	const auto v = pc->pc->transceivers();
	std::vector<lwrtc_rtp_transceiver*> out;
	for (size_t i = 0; i < v.size(); i++)
		out.push_back(lwrtc::wrap_transceiver(v[i]));
	return lwrtc::dup_handles(out, count);
}

lwrtc_data_channel* lwrtc_peer_connection_create_data_channel(lwrtc_peer_connection* pc,
                                                              const char* label,
                                                              const lwrtc_data_channel_init* init)
{
	if (!usable(pc))
		return nullptr;

	RTCDataChannelInit dinit;
	dinit.protocol = lwrtc::pstr(init ? init->protocol : nullptr);
	dinit.id = -1;
	if (init)
	{
		dinit.ordered = init->ordered;
		dinit.maxRetransmitTime = init->max_packet_life_time;
		dinit.maxRetransmits = init->max_retransmits;
		dinit.reliable = init->max_packet_life_time < 0 && init->max_retransmits < 0;
		dinit.negotiated = init->negotiated;
		if (init->negotiated)
			dinit.id = init->id;
	}
	return lwrtc::wrap_data_channel(pc->pc->CreateDataChannel(lwrtc::pstr(label), &dinit));
}

/* ---- statistics ---- */

struct lwrtc_stats_report
{
	struct Stats
	{
		std::string id;
		std::string type;
		int64_t timestamp;
		std::string json;
		std::vector<std::pair<std::string, std::string>> members;
	};

	std::vector<Stats> stats;
};

namespace
{
	template <typename T> std::string number(T v)
	{
		char buf[64];
		auto r = std::to_chars(buf, buf + sizeof(buf), v);
		return std::string(buf, r.ptr);
	}

	std::string json_string(const std::string& s)
	{
		std::string out = "\"";
		for (char c : s)
		{
			switch (c)
			{
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buf[8];
						snprintf(buf, sizeof(buf), "\\u%04x", c);
						out += buf;
					}
					else
						out += c;
			}
		}
		return out + "\"";
	}

	template <typename T, typename F> std::string sequence(const vector<T>& v, F format)
	{
		std::string out = "[";
		for (size_t i = 0; i < v.size(); i++)
		{
			if (i)
				out += ",";
			out += format(v[i]);
		}
		return out + "]";
	}

	/** the value as JavaScript's String() would give it; false if not available */
	bool member_value(RTCStatsMember* m, std::string& out)
	{
		switch (m->GetType())
		{
			case RTCStatsMember::kBool:
				out = m->ValueBool() ? "true" : "false";
				return true;
			case RTCStatsMember::kInt32:
				out = number(m->ValueInt32());
				return true;
			case RTCStatsMember::kUint32:
				out = number(m->ValueUint32());
				return true;
			case RTCStatsMember::kInt64:
				out = number(m->ValueInt64());
				return true;
			case RTCStatsMember::kUint64:
				out = number(m->ValueUint64());
				return true;
			case RTCStatsMember::kDouble:
				out = number(m->ValueDouble());
				return true;
			case RTCStatsMember::kString:
				out = lwrtc::str(m->ValueString());
				return true;
			case RTCStatsMember::kSequenceBool:
				out = sequence(m->ValueSequenceBool(),
				               [](bool b) { return std::string(b ? "true" : "false"); });
				return true;
			case RTCStatsMember::kSequenceInt32:
				out = sequence(m->ValueSequenceInt32(), [](int32_t v) { return number(v); });
				return true;
			case RTCStatsMember::kSequenceUint32:
				out = sequence(m->ValueSequenceUint32(), [](uint32_t v) { return number(v); });
				return true;
			case RTCStatsMember::kSequenceInt64:
				out = sequence(m->ValueSequenceInt64(), [](int64_t v) { return number(v); });
				return true;
			case RTCStatsMember::kSequenceUint64:
				out = sequence(m->ValueSequenceUint64(), [](uint64_t v) { return number(v); });
				return true;
			case RTCStatsMember::kSequenceDouble:
				out = sequence(m->ValueSequenceDouble(), [](double v) { return number(v); });
				return true;
			case RTCStatsMember::kSequenceString:
				out = sequence(m->ValueSequenceString(),
				               [](const string& s) { return json_string(lwrtc::str(s)); });
				return true;
			default:
				/* maps come as std::map from the library's own C++ runtime: unusable here */
				return false;
		}
	}

	lwrtc_stats_report build_report(const vector<scoped_refptr<MediaRTCStats>>& reports)
	{
		lwrtc_stats_report report;
		for (size_t i = 0; i < reports.size(); i++)
		{
			MediaRTCStats* s = reports[i].get();
			if (!s)
				continue;
			lwrtc_stats_report::Stats st;
			st.id = lwrtc::str(s->id());
			st.type = lwrtc::str(s->type());
			st.timestamp = s->timestamp_us();
			st.json = lwrtc::str(s->ToJson());
			const auto members = s->Members();
			for (size_t j = 0; j < members.size(); j++)
			{
				RTCStatsMember* m = members[j].get();
				std::string value;
				if (m && m->IsDefined() && member_value(m, value))
					st.members.emplace_back(lwrtc::str(m->GetName()), value);
			}
			report.stats.push_back(std::move(st));
		}
		return report;
	}

	OnStatsCollectorSuccess stats_ok(lwrtc_stats_fn fn, void* user)
	{
		return [fn, user](const vector<scoped_refptr<MediaRTCStats>> reports)
		{
			const lwrtc_stats_report report = build_report(reports);
			fn(user, &report, nullptr);
		};
	}

	OnStatsCollectorFailure stats_failed(lwrtc_stats_fn fn, void* user)
	{
		return [fn, user](const char* error)
		{ fn(user, nullptr, error ? error : "getStats failed"); };
	}
} // namespace

void lwrtc_peer_connection_get_stats(lwrtc_peer_connection* pc, lwrtc_stats_fn fn, void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
		fn(user, nullptr, kClosed);
	else
		pc->pc->GetStats(stats_ok(fn, user), stats_failed(fn, user));
}

void lwrtc_peer_connection_get_sender_stats(lwrtc_peer_connection* pc, lwrtc_rtp_sender* sender,
                                            lwrtc_stats_fn fn, void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
		fn(user, nullptr, kClosed);
	else if (!sender ||
	         !pc->pc->GetStats(sender->sender, stats_ok(fn, user), stats_failed(fn, user)))
		fn(user, nullptr, "getStats: unknown sender");
}

void lwrtc_peer_connection_get_receiver_stats(lwrtc_peer_connection* pc,
                                              lwrtc_rtp_receiver* receiver, lwrtc_stats_fn fn,
                                              void* user)
{
	if (!pc || !fn)
		return;
	if (!usable(pc))
		fn(user, nullptr, kClosed);
	else if (!receiver ||
	         !pc->pc->GetStats(receiver->receiver, stats_ok(fn, user), stats_failed(fn, user)))
		fn(user, nullptr, "getStats: unknown receiver");
}

size_t lwrtc_stats_report_count(const lwrtc_stats_report* report)
{
	return report ? report->stats.size() : 0;
}

static const lwrtc_stats_report::Stats* stats_at(const lwrtc_stats_report* report, size_t index)
{
	return (report && index < report->stats.size()) ? &report->stats[index] : nullptr;
}

const char* lwrtc_stats_id(const lwrtc_stats_report* report, size_t index)
{
	const auto* s = stats_at(report, index);
	return s ? s->id.c_str() : nullptr;
}

const char* lwrtc_stats_type(const lwrtc_stats_report* report, size_t index)
{
	const auto* s = stats_at(report, index);
	return s ? s->type.c_str() : nullptr;
}

int64_t lwrtc_stats_timestamp_us(const lwrtc_stats_report* report, size_t index)
{
	const auto* s = stats_at(report, index);
	return s ? s->timestamp : 0;
}

size_t lwrtc_stats_member_count(const lwrtc_stats_report* report, size_t index)
{
	const auto* s = stats_at(report, index);
	return s ? s->members.size() : 0;
}

const char* lwrtc_stats_member_name(const lwrtc_stats_report* report, size_t index, size_t member)
{
	const auto* s = stats_at(report, index);
	return (s && member < s->members.size()) ? s->members[member].first.c_str() : nullptr;
}

const char* lwrtc_stats_member_value(const lwrtc_stats_report* report, size_t index, size_t member)
{
	const auto* s = stats_at(report, index);
	return (s && member < s->members.size()) ? s->members[member].second.c_str() : nullptr;
}

const char* lwrtc_stats_json(const lwrtc_stats_report* report, size_t index)
{
	const auto* s = stats_at(report, index);
	return s ? s->json.c_str() : nullptr;
}
