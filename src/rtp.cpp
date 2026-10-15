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

/* RTCRtpSender, RTCRtpReceiver, RTCRtpTransceiver, their parameters, DTMF and data channels */

#include "internal.h"

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

LWRTC_DEFINE_REFCOUNTED(lwrtc_rtp_sender)
LWRTC_DEFINE_REFCOUNTED(lwrtc_rtp_receiver)
LWRTC_DEFINE_REFCOUNTED(lwrtc_rtp_transceiver)
LWRTC_DEFINE_REFCOUNTED(lwrtc_data_channel)

void lwrtc_rtp_senders_free(lwrtc_rtp_sender** senders, size_t count)
{
	if (!senders)
		return;
	for (size_t i = 0; i < count; i++)
		lwrtc::release(senders[i]);
	free(senders);
}

void lwrtc_rtp_receivers_free(lwrtc_rtp_receiver** receivers, size_t count)
{
	if (!receivers)
		return;
	for (size_t i = 0; i < count; i++)
		lwrtc::release(receivers[i]);
	free(receivers);
}

void lwrtc_rtp_transceivers_free(lwrtc_rtp_transceiver** transceivers, size_t count)
{
	if (!transceivers)
		return;
	for (size_t i = 0; i < count; i++)
		lwrtc::release(transceivers[i]);
	free(transceivers);
}

lwrtc_rtp_sender* lwrtc::wrap_sender(scoped_refptr<RTCRtpSender> sender)
{
	if (!sender)
		return nullptr;
	auto* s = new lwrtc_rtp_sender();
	s->sender = sender;
	return s;
}

lwrtc_rtp_receiver* lwrtc::wrap_receiver(scoped_refptr<RTCRtpReceiver> receiver)
{
	if (!receiver)
		return nullptr;
	auto* r = new lwrtc_rtp_receiver();
	r->receiver = receiver;
	return r;
}

lwrtc_rtp_transceiver* lwrtc::wrap_transceiver(scoped_refptr<RTCRtpTransceiver> transceiver)
{
	if (!transceiver)
		return nullptr;
	auto* t = new lwrtc_rtp_transceiver();
	t->transceiver = transceiver;
	return t;
}

lwrtc_data_channel* lwrtc::wrap_data_channel(scoped_refptr<RTCDataChannel> channel)
{
	if (!channel)
		return nullptr;
	auto* d = new lwrtc_data_channel();
	d->channel = channel;
	return d;
}

/* ---- parameters ---- */

void lwrtc_rtp_encoding_init(lwrtc_rtp_encoding* encoding)
{
	if (!encoding)
		return;
	memset(encoding, 0, sizeof(*encoding));
	encoding->active = true;
	encoding->scale_resolution_down_by = 1.0;
	encoding->bitrate_priority = 1.0;
	encoding->network_priority = LWRTC_PRIORITY_LOW;
}

/** applies what is set in `e` (the wrapper can't unset a value) */
static void apply_encoding(RTCRtpEncodingParameters* p, const lwrtc_rtp_encoding& e)
{
	p->set_active(e.active);
	if (e.max_bitrate_bps > 0)
		p->set_max_bitrate_bps(e.max_bitrate_bps);
	if (e.min_bitrate_bps > 0)
		p->set_min_bitrate_bps(e.min_bitrate_bps);
	if (e.max_framerate > 0)
		p->set_max_framerate(e.max_framerate);
	if (e.scale_resolution_down_by > 0)
		p->set_scale_resolution_down_by(e.scale_resolution_down_by);
	if (e.num_temporal_layers > 0)
		p->set_num_temporal_layers(e.num_temporal_layers);
	if (e.scalability_mode && *e.scalability_mode)
		p->set_scalability_mode(lwrtc::pstr(e.scalability_mode));
	if (e.bitrate_priority > 0)
		p->set_bitrate_priority(e.bitrate_priority);
	p->set_network_priority(static_cast<RTCPriority>(e.network_priority));
	p->set_adaptive_ptime(e.adaptive_ptime);
}

scoped_refptr<RTCRtpEncodingParameters> lwrtc::make_encoding(const lwrtc_rtp_encoding& e)
{
	auto p = RTCRtpEncodingParameters::Create();
	if (e.rid && *e.rid)
		p->set_rid(lwrtc::pstr(e.rid));
	apply_encoding(p.get(), e);
	return p;
}

static void read_encoding(RTCRtpEncodingParameters* p, lwrtc_rtp_encoding& e)
{
	e.rid = lwrtc::dup(p->rid());
	e.active = p->active();
	e.max_bitrate_bps = p->max_bitrate_bps();
	e.min_bitrate_bps = p->min_bitrate_bps();
	e.max_framerate = p->max_framerate();
	e.scale_resolution_down_by = p->scale_resolution_down_by();
	e.num_temporal_layers = p->num_temporal_layers();
	e.scalability_mode = lwrtc::dup(p->scalability_mode());
	e.bitrate_priority = p->bitrate_priority();
	e.network_priority = static_cast<lwrtc_priority>(p->network_priority());
	e.adaptive_ptime = p->adaptive_ptime();
	e.ssrc = p->ssrc();
}

static lwrtc_rtp_parameters* read_parameters(scoped_refptr<RTCRtpParameters> p, bool sender)
{
	if (!p)
		return nullptr;
	auto* out = static_cast<lwrtc_rtp_parameters*>(calloc(1, sizeof(lwrtc_rtp_parameters)));
	if (!out)
		return nullptr;

	out->transaction_id = lwrtc::dup(sender ? lwrtc::str(p->transaction_id()) : std::string());
	out->mid = lwrtc::dup(p->mid());

	const auto codecs = p->codecs();
	out->codecs = static_cast<lwrtc_rtp_codec*>(calloc(codecs.size() + 1, sizeof(lwrtc_rtp_codec)));
	for (size_t i = 0; out->codecs && i < codecs.size(); i++)
	{
		RTCRtpCodecParameters* c = codecs[i].get();
		lwrtc_rtp_codec& oc = out->codecs[i];
		oc.mime_type = lwrtc::dup(c->mime_type());
		oc.payload_type = c->payload_type();
		oc.clock_rate = c->clock_rate();
		oc.channels = c->num_channels();

		/* the fmtp parameters, as the SDP line */
		std::string fmtp;
		const auto params = c->parameters();
		for (size_t j = 0; j < params.size(); j++)
		{
			if (!fmtp.empty())
				fmtp += ";";
			/* a value without a key is a whole line, as RED's "111/111" */
			const std::string key = lwrtc::str(params[j].first);
			fmtp += key.empty() ? lwrtc::str(params[j].second)
			                    : key + "=" + lwrtc::str(params[j].second);
		}
		oc.sdp_fmtp_line = lwrtc::dup(fmtp);
		out->codec_count++;
	}

	const auto extensions = p->header_extensions();
	out->header_extensions = static_cast<lwrtc_rtp_header_extension*>(
	    calloc(extensions.size() + 1, sizeof(lwrtc_rtp_header_extension)));
	for (size_t i = 0; out->header_extensions && i < extensions.size(); i++)
	{
		RTCRtpExtension* x = extensions[i].get();
		out->header_extensions[i].uri = lwrtc::dup(x->uri());
		out->header_extensions[i].id = x->id();
		out->header_extensions[i].encrypted = x->encrypt();
		out->header_extension_count++;
	}

	if (sender)
	{
		const auto encodings = p->encodings();
		out->encodings = static_cast<lwrtc_rtp_encoding*>(
		    calloc(encodings.size() + 1, sizeof(lwrtc_rtp_encoding)));
		for (size_t i = 0; out->encodings && i < encodings.size(); i++)
		{
			read_encoding(encodings[i].get(), out->encodings[i]);
			out->encoding_count++;
		}
	}

	if (auto rtcp = p->rtcp_parameters())
	{
		out->rtcp_cname = lwrtc::dup(rtcp->cname());
		out->rtcp_reduced_size = rtcp->reduced_size();
	}
	else
		out->rtcp_cname = lwrtc::dup(std::string());

	out->degradation_preference =
	    static_cast<lwrtc_degradation_preference>(p->GetDegradationPreference());
	return out;
}

void lwrtc_rtp_parameters_free(lwrtc_rtp_parameters* parameters)
{
	if (!parameters)
		return;
	free(parameters->transaction_id);
	free(parameters->mid);
	for (size_t i = 0; i < parameters->codec_count; i++)
	{
		free(parameters->codecs[i].mime_type);
		free(parameters->codecs[i].sdp_fmtp_line);
	}
	free(parameters->codecs);
	for (size_t i = 0; i < parameters->header_extension_count; i++)
		free(parameters->header_extensions[i].uri);
	free(parameters->header_extensions);
	for (size_t i = 0; i < parameters->encoding_count; i++)
	{
		free(parameters->encodings[i].rid);
		free(parameters->encodings[i].scalability_mode);
	}
	free(parameters->encodings);
	free(parameters->rtcp_cname);
	free(parameters);
}

/* ---- senders ---- */

lwrtc_rtp_sender::~lwrtc_rtp_sender()
{
	if (dtmf && dtmfObserver)
		dtmf->UnregisterObserver();
	delete dtmfObserver;
}

char* lwrtc_rtp_sender_id(lwrtc_rtp_sender* sender)
{
	return sender ? lwrtc::dup(sender->sender->id()) : nullptr;
}

lwrtc_media_kind lwrtc_rtp_sender_kind(lwrtc_rtp_sender* sender)
{
	return sender ? lwrtc::kind(sender->sender->media_type()) : LWRTC_MEDIA_UNSUPPORTED;
}

lwrtc_media_track* lwrtc_rtp_sender_track(lwrtc_rtp_sender* sender)
{
	return sender ? lwrtc::wrap_track(sender->sender->track(), false) : nullptr;
}

lwrtc_result lwrtc_rtp_sender_replace_track(lwrtc_rtp_sender* sender, lwrtc_media_track* track)
{
	if (!sender)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	return sender->sender->set_track(track ? track->track : scoped_refptr<RTCMediaTrack>())
	           ? LWRTC_OK
	           : LWRTC_ERROR_FAILED;
}

char** lwrtc_rtp_sender_stream_ids(lwrtc_rtp_sender* sender, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	return sender ? lwrtc::dup_strings(sender->sender->stream_ids(), count) : nullptr;
}

lwrtc_result lwrtc_rtp_sender_set_stream_ids(lwrtc_rtp_sender* sender,
                                             const char* const* stream_ids, size_t count)
{
	if (!sender)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	sender->sender->set_stream_ids(lwrtc::pstrings(stream_ids, count));
	return LWRTC_OK;
}

uint32_t lwrtc_rtp_sender_ssrc(lwrtc_rtp_sender* sender)
{
	return sender ? sender->sender->ssrc() : 0;
}

lwrtc_rtp_parameters* lwrtc_rtp_sender_get_parameters(lwrtc_rtp_sender* sender)
{
	return sender ? read_parameters(sender->sender->parameters(), true) : nullptr;
}

lwrtc_result lwrtc_rtp_sender_set_parameters(lwrtc_rtp_sender* sender,
                                             const lwrtc_rtp_parameters* parameters)
{
	if (!sender || !parameters)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	auto p = sender->sender->parameters();
	if (!p)
		return LWRTC_ERROR_INVALID_STATE;
	const auto encodings = p->encodings();
	if (encodings.size() != parameters->encoding_count)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	for (size_t i = 0; i < encodings.size(); i++)
		apply_encoding(encodings[i].get(), parameters->encodings[i]);
	p->set_encodings(encodings);
	p->SetDegradationPreference(
	    static_cast<RTCDegradationPreference>(parameters->degradation_preference));
	return sender->sender->set_parameters(p) ? LWRTC_OK : LWRTC_ERROR_FAILED;
}

static RTCDtmfSender* dtmf_sender(lwrtc_rtp_sender* sender)
{
	if (!sender->dtmf)
		sender->dtmf = sender->sender->dtmf_sender();
	return sender->dtmf.get();
}

bool lwrtc_rtp_sender_can_insert_dtmf(lwrtc_rtp_sender* sender)
{
	RTCDtmfSender* dtmf = sender ? dtmf_sender(sender) : nullptr;
	return dtmf && dtmf->CanInsertDtmf();
}

lwrtc_result lwrtc_rtp_sender_insert_dtmf(lwrtc_rtp_sender* sender, const char* tones, int duration,
                                          int inter_tone_gap)
{
	RTCDtmfSender* dtmf = sender ? dtmf_sender(sender) : nullptr;
	if (!dtmf || !tones)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	if (!dtmf->CanInsertDtmf())
		return LWRTC_ERROR_INVALID_STATE;
	return dtmf->InsertDtmf(lwrtc::pstr(tones), duration, inter_tone_gap) ? LWRTC_OK
	                                                                      : LWRTC_ERROR_FAILED;
}

lwrtc_result lwrtc_rtp_sender_set_dtmf_observer(lwrtc_rtp_sender* sender, lwrtc_dtmf_tone_fn fn,
                                                void* user)
{
	RTCDtmfSender* dtmf = sender ? dtmf_sender(sender) : nullptr;
	if (!dtmf)
		return LWRTC_ERROR_INVALID_ARGUMENT;

	if (sender->dtmfObserver)
	{
		dtmf->UnregisterObserver();
		delete sender->dtmfObserver;
		sender->dtmfObserver = nullptr;
	}
	if (fn)
	{
		sender->dtmfObserver = new DtmfObserver();
		sender->dtmfObserver->fn = fn;
		sender->dtmfObserver->user = user;
		dtmf->RegisterObserver(sender->dtmfObserver);
	}
	return LWRTC_OK;
}

/* ---- receivers ---- */

char* lwrtc_rtp_receiver_id(lwrtc_rtp_receiver* receiver)
{
	return receiver ? lwrtc::dup(receiver->receiver->id()) : nullptr;
}

lwrtc_media_kind lwrtc_rtp_receiver_kind(lwrtc_rtp_receiver* receiver)
{
	return receiver ? lwrtc::kind(receiver->receiver->media_type()) : LWRTC_MEDIA_UNSUPPORTED;
}

lwrtc_media_track* lwrtc_rtp_receiver_track(lwrtc_rtp_receiver* receiver)
{
	return receiver ? lwrtc::wrap_track(receiver->receiver->track(), true) : nullptr;
}

char** lwrtc_rtp_receiver_stream_ids(lwrtc_rtp_receiver* receiver, size_t* count)
{
	if (!count)
		return nullptr;
	*count = 0;
	return receiver ? lwrtc::dup_strings(receiver->receiver->stream_ids(), count) : nullptr;
}

lwrtc_rtp_parameters* lwrtc_rtp_receiver_get_parameters(lwrtc_rtp_receiver* receiver)
{
	return receiver ? read_parameters(receiver->receiver->parameters(), false) : nullptr;
}

void lwrtc_rtp_receiver_set_jitter_buffer_minimum_delay(lwrtc_rtp_receiver* receiver,
                                                        double seconds)
{
	if (receiver)
		receiver->receiver->SetJitterBufferMinimumDelay(seconds);
}

/* ---- transceivers ---- */

char* lwrtc_rtp_transceiver_mid(lwrtc_rtp_transceiver* transceiver)
{
	return transceiver ? lwrtc::dup(transceiver->transceiver->mid()) : nullptr;
}

lwrtc_media_kind lwrtc_rtp_transceiver_kind(lwrtc_rtp_transceiver* transceiver)
{
	return transceiver ? lwrtc::kind(transceiver->transceiver->media_type())
	                   : LWRTC_MEDIA_UNSUPPORTED;
}

lwrtc_rtp_sender* lwrtc_rtp_transceiver_sender(lwrtc_rtp_transceiver* transceiver)
{
	return transceiver ? lwrtc::wrap_sender(transceiver->transceiver->sender()) : nullptr;
}

lwrtc_rtp_receiver* lwrtc_rtp_transceiver_receiver(lwrtc_rtp_transceiver* transceiver)
{
	return transceiver ? lwrtc::wrap_receiver(transceiver->transceiver->receiver()) : nullptr;
}

lwrtc_direction lwrtc_rtp_transceiver_direction(lwrtc_rtp_transceiver* transceiver)
{
	return transceiver ? lwrtc::direction(transceiver->transceiver->direction())
	                   : LWRTC_DIRECTION_STOPPED;
}

bool lwrtc_rtp_transceiver_current_direction(lwrtc_rtp_transceiver* transceiver,
                                             lwrtc_direction* direction)
{
	if (!transceiver || !direction)
		return false;
	/* the wrapper has no "not negotiated": currentDirection comes with the mid */
	if (lwrtc::str(transceiver->transceiver->mid()).empty())
		return false;
	*direction = lwrtc::direction(transceiver->transceiver->current_direction());
	return true;
}

static char* error_or_null(const string& error)
{
	const std::string e = lwrtc::str(error);
	return e.empty() ? nullptr : lwrtc::dup(e);
}

char* lwrtc_rtp_transceiver_set_direction(lwrtc_rtp_transceiver* transceiver,
                                          lwrtc_direction direction)
{
	if (!transceiver)
		return lwrtc::dup(std::string("no transceiver"));
	return error_or_null(
	    transceiver->transceiver->SetDirectionWithError(lwrtc::direction(direction)));
}

char* lwrtc_rtp_transceiver_stop(lwrtc_rtp_transceiver* transceiver)
{
	if (!transceiver)
		return lwrtc::dup(std::string("no transceiver"));
	return error_or_null(transceiver->transceiver->StopStandard());
}

bool lwrtc_rtp_transceiver_stopped(lwrtc_rtp_transceiver* transceiver)
{
	return !transceiver || transceiver->transceiver->Stopped();
}

/* ---- data channels ---- */

lwrtc_data_channel::~lwrtc_data_channel()
{
	if (observer)
	{
		channel->UnregisterObserver();
		delete observer;
	}
}

void lwrtc_data_channel_set_observer(lwrtc_data_channel* channel,
                                     const lwrtc_data_channel_observer* observer)
{
	if (!channel)
		return;

	std::lock_guard<std::mutex> guard(channel->lock);
	if (channel->observer)
	{
		channel->channel->UnregisterObserver();
		delete channel->observer;
		channel->observer = nullptr;
	}
	if (observer)
	{
		channel->observer = new DataChannelObserver();
		channel->observer->callbacks = *observer;
		channel->channel->RegisterObserver(channel->observer);
	}
}

char* lwrtc_data_channel_label(lwrtc_data_channel* channel)
{
	return channel ? lwrtc::dup(channel->channel->label()) : nullptr;
}

int lwrtc_data_channel_id(lwrtc_data_channel* channel)
{
	return channel ? channel->channel->id() : -1;
}

lwrtc_data_channel_state lwrtc_data_channel_state_get(lwrtc_data_channel* channel)
{
	return channel ? static_cast<lwrtc_data_channel_state>(channel->channel->state())
	               : LWRTC_DATA_CHANNEL_CLOSED;
}

uint64_t lwrtc_data_channel_buffered_amount(lwrtc_data_channel* channel)
{
	return channel ? channel->channel->buffered_amount() : 0;
}

lwrtc_result lwrtc_data_channel_send(lwrtc_data_channel* channel, const uint8_t* data,
                                     size_t length, bool binary)
{
	if (!channel || (!data && length) || length > UINT32_MAX)
		return LWRTC_ERROR_INVALID_ARGUMENT;
	if (channel->channel->state() != RTCDataChannelOpen)
		return LWRTC_ERROR_INVALID_STATE;
	channel->channel->Send(data, static_cast<uint32_t>(length), binary);
	return LWRTC_OK;
}

void lwrtc_data_channel_close(lwrtc_data_channel* channel)
{
	if (channel)
		channel->channel->Close();
}
