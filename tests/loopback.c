/**
 * libwebrtc-c: loopback test
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

/* Two peer connections of the same process call each other: offer/answer (the answer through
 * the implicit setLocalDescription), ICE, a data channel both ways, transceivers, parameters,
 * stats and pixel conversion. With --media, the caller also sends the microphone and the camera
 * (the callee plays nothing: remote audio is set to volume 0) and counts what arrives, and
 * sends DTMF. */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms)
{
	Sleep(ms);
}
#else
#include <time.h>
static void sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}
#endif

#include <lwrtc/lwrtc.h>

static int failures = 0;

#define CHECK(cond)                                                           \
	do                                                                        \
	{                                                                         \
		if (!(cond))                                                          \
		{                                                                     \
			fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/** waits up to `ms` for `flag` to become non zero */
static bool wait_for(atomic_int* flag, int ms)
{
	for (int t = 0; t < ms; t += 10)
	{
		if (atomic_load(flag))
			return true;
		sleep_ms(10);
	}
	return atomic_load(flag) != 0;
}

typedef struct
{
	const char* name;
	lwrtc_peer_connection* pc;
	lwrtc_peer_connection* other;
	atomic_int connected;
	atomic_int tracks;
	atomic_int signaling_seen[LWRTC_SIGNALING_CLOSED + 1];
	atomic_int ice_seen[LWRTC_ICE_CONNECTION_CLOSED + 1];
	int first_ice_after_checking;
	lwrtc_data_channel* remote_channel;
	atomic_int remote_channel_ready;
	atomic_int messages;
	char last_message[256];
	atomic_int description_sync;
	atomic_int audio_frames;
	atomic_int video_frames;
	lwrtc_audio_sink* audio_sink;
	lwrtc_video_sink* video_sink;
	lwrtc_media_track* remote_tracks[4];
	int remote_track_count;
} Peer;

/* ---- peer connection events ---- */

static void on_signaling(void* user, lwrtc_signaling_state state)
{
	Peer* p = user;
	printf("[%s] signaling %s\n", p->name, lwrtc_signaling_state_name(state));
	atomic_store(&p->signaling_seen[state], 1);
}

static void on_connection(void* user, lwrtc_peer_connection_state state)
{
	Peer* p = user;
	printf("[%s] connection %s\n", p->name, lwrtc_peer_connection_state_name(state));
	if (state == LWRTC_PEER_CONNECTION_CONNECTED)
		atomic_store(&p->connected, 1);
}

static void on_ice_connection(void* user, lwrtc_ice_connection_state state)
{
	Peer* p = user;
	printf("[%s] ice connection %s\n", p->name, lwrtc_ice_connection_state_name(state));
	if (!p->first_ice_after_checking && state != LWRTC_ICE_CONNECTION_NEW &&
	    state != LWRTC_ICE_CONNECTION_CHECKING)
		p->first_ice_after_checking = (int)state + 1;
	atomic_store(&p->ice_seen[state], 1);
}

static void on_gathering(void* user, lwrtc_ice_gathering_state state)
{
	Peer* p = user;
	printf("[%s] gathering %s\n", p->name, lwrtc_ice_gathering_state_name(state));
}

static void on_description_in_candidate(void* user, const char* type, const char* sdp,
                                        const char* error)
{
	(void)type;
	(void)error;
	Peer* p = user;
	if (sdp)
		atomic_store(&p->description_sync, 1);
}

static void on_candidate(void* user, const char* candidate, const char* mid, int index)
{
	Peer* p = user;
	CHECK(lwrtc_peer_connection_add_ice_candidate(p->other, candidate, mid, index) == LWRTC_OK);

	/* is the local description given at once from a candidate event? The MMR channel sends
	 * both together. */
	lwrtc_peer_connection_get_local_description(p->pc, on_description_in_candidate, p);
}

static void on_audio(void* user, const void* data, int bits, int rate, size_t channels,
                     size_t frames)
{
	(void)data;
	(void)bits;
	(void)rate;
	(void)channels;
	Peer* p = user;
	atomic_fetch_add(&p->audio_frames, (int)frames);
}

static void on_video(void* user, lwrtc_video_frame* frame)
{
	Peer* p = user;
	if (atomic_fetch_add(&p->video_frames, 1) == 0)
	{
		const int w = lwrtc_video_frame_width(frame);
		const int h = lwrtc_video_frame_height(frame);
		uint8_t* bgra = malloc((size_t)w * h * 4);
		CHECK(bgra &&
		      lwrtc_video_frame_convert(frame, LWRTC_PIXEL_BGRA32, bgra, w * 4, w, h) == LWRTC_OK);
		printf("[%s] first remote video frame %dx%d\n", p->name, w, h);
		free(bgra);
	}
}

static void on_track(void* user, lwrtc_rtp_transceiver* transceiver, const char* const* streams,
                     size_t stream_count)
{
	Peer* p = user;
	char* mid = lwrtc_rtp_transceiver_mid(transceiver);
	lwrtc_rtp_receiver* receiver = lwrtc_rtp_transceiver_receiver(transceiver);
	lwrtc_media_track* track = lwrtc_rtp_receiver_track(receiver);
	char* id = lwrtc_media_track_id(track);
	printf("[%s] ontrack mid=%s kind=%s track=%s streams=%zu%s%s\n", p->name, mid,
	       lwrtc_media_kind_name(lwrtc_media_track_kind(track)), id, stream_count,
	       stream_count ? " first=" : "", stream_count ? streams[0] : "");
	CHECK(lwrtc_media_track_is_remote(track));

	if (lwrtc_media_track_kind(track) == LWRTC_MEDIA_AUDIO)
	{
		/* not played: no <audio> element */
		CHECK(lwrtc_audio_track_set_volume(track, 0) == LWRTC_OK);
		if (!p->audio_sink)
			p->audio_sink = lwrtc_audio_track_add_sink(track, on_audio, p);
	}
	else if (lwrtc_media_track_kind(track) == LWRTC_MEDIA_VIDEO && !p->video_sink)
		p->video_sink = lwrtc_video_track_add_sink(track, on_video, p);

	if (p->remote_track_count < 4)
		p->remote_tracks[p->remote_track_count++] = track;
	else
		lwrtc_media_track_release(track);
	atomic_fetch_add(&p->tracks, 1);
	lwrtc_free(id);
	lwrtc_rtp_receiver_release(receiver);
	lwrtc_free(mid);
}

/* ---- data channels ---- */

static void on_channel_state(void* user, lwrtc_data_channel_state state)
{
	Peer* p = user;
	printf("[%s] data channel %s\n", p->name, lwrtc_data_channel_state_name(state));
	if (state == LWRTC_DATA_CHANNEL_OPEN)
		atomic_store(&p->remote_channel_ready, 1);
}

static void on_channel_message(void* user, const uint8_t* data, size_t length, bool binary)
{
	Peer* p = user;
	if (length >= sizeof(p->last_message))
		length = sizeof(p->last_message) - 1;
	memcpy(p->last_message, data, length);
	p->last_message[length] = 0;
	printf("[%s] message (%s): %s\n", p->name, binary ? "binary" : "text", p->last_message);
	atomic_fetch_add(&p->messages, 1);
}

static void on_data_channel(void* user, lwrtc_data_channel* channel)
{
	Peer* p = user;
	char* label = lwrtc_data_channel_label(channel);
	printf("[%s] ondatachannel %s\n", p->name, label);
	lwrtc_free(label);

	p->remote_channel = lwrtc_data_channel_add_ref(channel);
	const lwrtc_data_channel_observer o = { p, on_channel_state, on_channel_message };
	lwrtc_data_channel_set_observer(channel, &o);
	if (lwrtc_data_channel_state_get(channel) == LWRTC_DATA_CHANNEL_OPEN)
		atomic_store(&p->remote_channel_ready, 1);
}

/* ---- async results ---- */

typedef struct
{
	atomic_int done;
	char type[32];
	char* sdp;
	char error[256];
} Result;

static char* copy_string(const char* s)
{
	const size_t n = strlen(s) + 1;
	char* out = malloc(n);
	if (out)
		memcpy(out, s, n);
	return out;
}

static void on_description(void* user, const char* type, const char* sdp, const char* error)
{
	Result* r = user;
	if (error)
		snprintf(r->error, sizeof(r->error), "%s", error);
	if (type)
		snprintf(r->type, sizeof(r->type), "%s", type);
	if (sdp)
		r->sdp = copy_string(sdp);
	atomic_store(&r->done, 1);
}

static void on_done(void* user, const char* error)
{
	Result* r = user;
	if (error)
		snprintf(r->error, sizeof(r->error), "%s", error);
	atomic_store(&r->done, 1);
}

static bool finished(Result* r, const char* what)
{
	if (!wait_for(&r->done, 5000))
	{
		fprintf(stderr, "FAILED: %s timed out\n", what);
		failures++;
		return false;
	}
	if (r->error[0])
	{
		fprintf(stderr, "FAILED: %s: %s\n", what, r->error);
		failures++;
		return false;
	}
	return true;
}

static void on_stats(void* user, const lwrtc_stats_report* report, const char* error)
{
	Result* r = user;
	if (error)
		snprintf(r->error, sizeof(r->error), "%s", error);
	else
	{
		const size_t n = lwrtc_stats_report_count(report);
		printf("stats: %zu objects\n", n);
		for (size_t i = 0; i < n; i++)
		{
			const char* type = lwrtc_stats_type(report, i);
			if (strcmp(type, "candidate-pair") == 0 || strcmp(type, "outbound-rtp") == 0)
			{
				printf("  %s %s (%zu members)\n", type, lwrtc_stats_id(report, i),
				       lwrtc_stats_member_count(report, i));
				for (size_t m = 0; m < lwrtc_stats_member_count(report, i) && m < 6; m++)
					printf("    %s = %s\n", lwrtc_stats_member_name(report, i, m),
					       lwrtc_stats_member_value(report, i, m));
			}
		}
		snprintf(r->type, sizeof(r->type), "%zu", n);
	}
	atomic_store(&r->done, 1);
}

static void on_tone(void* user, const char* tone, const char* buffer)
{
	atomic_int* tones = user;
	printf("dtmf tone '%s' (left '%s')\n", tone, buffer);
	atomic_fetch_add(tones, 1);
}

static void on_log(void* user, const char* message)
{
	(void)user;
	fprintf(stderr, "webrtc: %s", message);
}

/* ---- the test ---- */

static void test_pixels(void)
{
	/* red in BT.601 limited range */
	uint8_t y[16], u[4], v[4];
	memset(y, 81, sizeof(y));
	memset(u, 90, sizeof(u));
	memset(v, 240, sizeof(v));
	lwrtc_video_frame* frame = lwrtc_video_frame_new_i420(4, 4, y, 4, u, 2, v, 2);
	CHECK(frame);
	uint8_t out[64];
	CHECK(lwrtc_video_frame_convert(frame, LWRTC_PIXEL_BGRA32, out, 16, 4, 4) == LWRTC_OK);
	CHECK(out[0] < 8 && out[1] < 8 && out[2] > 240 && out[3] == 255);
	CHECK(lwrtc_video_frame_convert(frame, LWRTC_PIXEL_RGBA32, out, 16, 4, 4) == LWRTC_OK);
	CHECK(out[0] > 240 && out[1] < 8 && out[2] < 8 && out[3] == 255);
	CHECK(lwrtc_video_frame_convert(frame, LWRTC_PIXEL_BGRA32, out, 16, 8, 8) ==
	      LWRTC_ERROR_INVALID_ARGUMENT);
	lwrtc_video_frame_release(frame);
}

static void test_names(void)
{
	lwrtc_direction d;
	CHECK(lwrtc_direction_from_name("recvonly", &d) && d == LWRTC_DIRECTION_RECVONLY);
	CHECK(!lwrtc_direction_from_name("sideways", &d));
	CHECK(strcmp(lwrtc_signaling_state_name(LWRTC_SIGNALING_HAVE_REMOTE_OFFER),
	             "have-remote-offer") == 0);
}

static void list_devices(lwrtc_factory* factory)
{
	size_t n = 0;
	lwrtc_device_info* devices = lwrtc_factory_enumerate_devices(factory, &n);
	static const char* kinds[] = { "audioinput", "audiooutput", "videoinput" };
	for (size_t i = 0; i < n; i++)
		printf("device %-11s id=%s label=%s\n", kinds[devices[i].kind], devices[i].device_id,
		       devices[i].label);
	lwrtc_free(devices);
}

static void test_parameters(lwrtc_rtp_sender* sender)
{
	lwrtc_rtp_parameters* params = lwrtc_rtp_sender_get_parameters(sender);
	CHECK(params);
	if (!params)
		return;
	printf("sender parameters: mid=%s codecs=%zu extensions=%zu encodings=%zu cname=%s\n",
	       params->mid, params->codec_count, params->header_extension_count, params->encoding_count,
	       params->rtcp_cname);
	for (size_t i = 0; i < params->codec_count && i < 3; i++)
		printf("  codec %s/%d pt=%d fmtp=%s\n", params->codecs[i].mime_type,
		       params->codecs[i].clock_rate, params->codecs[i].payload_type,
		       params->codecs[i].sdp_fmtp_line);
	CHECK(params->encoding_count >= 1);
	if (params->encoding_count >= 1)
	{
		params->encodings[0].max_bitrate_bps = 32000;
		CHECK(lwrtc_rtp_sender_set_parameters(sender, params) == LWRTC_OK);
		lwrtc_rtp_parameters* again = lwrtc_rtp_sender_get_parameters(sender);
		CHECK(again && again->encoding_count >= 1 && again->encodings[0].max_bitrate_bps == 32000);
		lwrtc_rtp_parameters_free(again);
	}
	lwrtc_rtp_parameters_free(params);
}

int main(int argc, char** argv)
{
	const bool media = argc > 1 && strcmp(argv[1], "--media") == 0;
	if (getenv("LWRTC_LOG"))
		lwrtc_set_log_sink(LWRTC_LOG_INFO, on_log, NULL);

	test_names();
	CHECK(lwrtc_initialize(NULL, 0));
	test_pixels();

	lwrtc_factory* factory = lwrtc_factory_new();
	CHECK(factory);
	if (!factory)
		return 1;
	list_devices(factory);

	static Peer a = { .name = "caller" };
	static Peer b = { .name = "callee" };
	lwrtc_peer_connection_observer oa = {
		&a,           on_signaling, on_connection, on_ice_connection, on_gathering,
		on_candidate, on_track,     NULL,          on_data_channel,   NULL
	};
	lwrtc_peer_connection_observer ob = oa;
	ob.user = &b;

	a.pc = lwrtc_peer_connection_new(factory, NULL, &oa);
	b.pc = lwrtc_peer_connection_new(factory, NULL, &ob);
	CHECK(a.pc && b.pc);
	a.other = b.pc;
	b.other = a.pc;

	/* the caller's media: an audio transceiver (with the microphone in --media), a video one
	 * receive only (with the camera, sending, in --media) */
	lwrtc_media_track* mic = NULL;
	lwrtc_media_track* cam = NULL;
	if (media)
	{
		mic = lwrtc_factory_create_microphone_track(factory, NULL, NULL, NULL);
		cam = lwrtc_factory_create_camera_track(factory, NULL, 640, 480, 30, NULL);
		CHECK(mic && cam);
		char* label = lwrtc_media_track_label(mic);
		printf("microphone track: %s\n", label);
		lwrtc_free(label);
	}

	const char* streams[] = { "stream-a" };
	lwrtc_transceiver_init audio_init = { LWRTC_DIRECTION_SENDRECV, streams, 1, NULL, 0 };
	lwrtc_rtp_transceiver* ta =
	    lwrtc_peer_connection_add_transceiver(a.pc, mic, LWRTC_MEDIA_AUDIO, &audio_init);
	CHECK(ta);
	lwrtc_rtp_sender* sender = NULL;
	if (cam)
		sender = lwrtc_peer_connection_add_track(a.pc, cam, streams, 1);
	else
	{
		lwrtc_transceiver_init video_init = { LWRTC_DIRECTION_RECVONLY, NULL, 0, NULL, 0 };
		lwrtc_rtp_transceiver* tv =
		    lwrtc_peer_connection_add_transceiver(a.pc, NULL, LWRTC_MEDIA_VIDEO, &video_init);
		CHECK(tv && lwrtc_rtp_transceiver_direction(tv) == LWRTC_DIRECTION_RECVONLY);
		lwrtc_rtp_transceiver_release(tv);
	}
	(void)sender;

	lwrtc_data_channel* channel = lwrtc_peer_connection_create_data_channel(a.pc, "chat", NULL);
	CHECK(channel);
	static Peer channel_events = { .name = "caller-channel" };
	const lwrtc_data_channel_observer co = { &channel_events, on_channel_state,
		                                     on_channel_message };
	lwrtc_data_channel_set_observer(channel, &co);

	/* offer, explicitly set */
	static Result offer, set_offer_local, set_offer_remote, set_answer_local, answer,
	    set_answer_remote;
	lwrtc_peer_connection_create_offer(a.pc, NULL, on_description, &offer);
	if (finished(&offer, "createOffer"))
	{
		CHECK(strcmp(offer.type, "offer") == 0);
		CHECK(strstr(offer.sdp, "m=audio") && strstr(offer.sdp, "m=video") &&
		      strstr(offer.sdp, "m=application"));
		lwrtc_peer_connection_set_local_description(a.pc, offer.type, offer.sdp, on_done,
		                                            &set_offer_local);
		finished(&set_offer_local, "caller setLocalDescription");
		CHECK(lwrtc_peer_connection_signaling_state(a.pc) == LWRTC_SIGNALING_HAVE_LOCAL_OFFER);

		lwrtc_peer_connection_set_remote_description(b.pc, offer.type, offer.sdp, on_done,
		                                             &set_offer_remote);
		finished(&set_offer_remote, "callee setRemoteDescription");
		CHECK(lwrtc_peer_connection_signaling_state(b.pc) == LWRTC_SIGNALING_HAVE_REMOTE_OFFER);

		/* answer through the implicit setLocalDescription() */
		lwrtc_peer_connection_set_local_description(b.pc, NULL, NULL, on_done, &set_answer_local);
		finished(&set_answer_local, "callee implicit setLocalDescription");
		lwrtc_peer_connection_get_local_description(b.pc, on_description, &answer);
		if (finished(&answer, "callee localDescription"))
		{
			CHECK(strcmp(answer.type, "answer") == 0);
			lwrtc_peer_connection_set_remote_description(a.pc, answer.type, answer.sdp, on_done,
			                                             &set_answer_remote);
			finished(&set_answer_remote, "caller setRemoteDescription");
		}
	}

	CHECK(wait_for(&a.connected, 10000));
	CHECK(wait_for(&b.connected, 10000));
	CHECK(atomic_load(&b.signaling_seen[LWRTC_SIGNALING_HAVE_REMOTE_OFFER]));
	CHECK(atomic_load(&a.signaling_seen[LWRTC_SIGNALING_HAVE_LOCAL_OFFER]));
	CHECK(atomic_load(&a.signaling_seen[LWRTC_SIGNALING_STABLE]));
	printf(
	    "first ICE state after checking: caller %s, callee %s\n",
	    a.first_ice_after_checking ? lwrtc_ice_connection_state_name(a.first_ice_after_checking - 1)
	                               : "-",
	    b.first_ice_after_checking ? lwrtc_ice_connection_state_name(b.first_ice_after_checking - 1)
	                               : "-");
	CHECK(a.first_ice_after_checking - 1 == LWRTC_ICE_CONNECTION_CONNECTED);
	printf("local description from a candidate event: %s\n",
	       atomic_load(&a.description_sync) ? "given" : "not given");
	CHECK(wait_for(&b.tracks, 5000));

	/* data channel both ways */
	CHECK(wait_for(&channel_events.remote_channel_ready, 5000));
	CHECK(wait_for(&b.remote_channel_ready, 5000));
	const char hello[] = "hello from the caller";
	CHECK(lwrtc_data_channel_send(channel, (const uint8_t*)hello, strlen(hello), false) ==
	      LWRTC_OK);
	CHECK(wait_for(&b.messages, 5000) && strcmp(b.last_message, hello) == 0);
	const uint8_t bin[] = { 1, 2, 3 };
	CHECK(b.remote_channel &&
	      lwrtc_data_channel_send(b.remote_channel, bin, sizeof(bin), true) == LWRTC_OK);
	CHECK(wait_for(&channel_events.messages, 5000));

	/* transceivers after negotiation */
	size_t n = 0;
	lwrtc_rtp_transceiver** ts = lwrtc_peer_connection_transceivers(a.pc, &n);
	CHECK(n == 2);
	for (size_t i = 0; i < n; i++)
	{
		char* mid = lwrtc_rtp_transceiver_mid(ts[i]);
		lwrtc_direction cur = LWRTC_DIRECTION_STOPPED;
		const bool negotiated = lwrtc_rtp_transceiver_current_direction(ts[i], &cur);
		printf("caller transceiver mid=%s kind=%s direction=%s current=%s\n", mid,
		       lwrtc_media_kind_name(lwrtc_rtp_transceiver_kind(ts[i])),
		       lwrtc_direction_name(lwrtc_rtp_transceiver_direction(ts[i])),
		       negotiated ? lwrtc_direction_name(cur) : "(none)");
		CHECK(negotiated);
		lwrtc_free(mid);
	}
	lwrtc_rtp_transceivers_free(ts, n);

	lwrtc_rtp_sender* audio_sender = lwrtc_rtp_transceiver_sender(ta);
	CHECK(audio_sender);
	test_parameters(audio_sender);

	static Result stats, sender_stats;
	lwrtc_peer_connection_get_stats(a.pc, on_stats, &stats);
	if (finished(&stats, "getStats"))
		CHECK(atoi(stats.type) > 0);
	lwrtc_peer_connection_get_sender_stats(a.pc, audio_sender, on_stats, &sender_stats);
	finished(&sender_stats, "getStats(sender)");

	if (media)
	{
		static atomic_int tones;
		CHECK(lwrtc_rtp_sender_set_dtmf_observer(audio_sender, on_tone, &tones) == LWRTC_OK);
		CHECK(lwrtc_rtp_sender_insert_dtmf(audio_sender, "12", 100, 70) == LWRTC_OK);
		sleep_ms(3000);
		printf("received: %d audio frames, %d video frames, %d dtmf events\n",
		       atomic_load(&b.audio_frames), atomic_load(&b.video_frames), atomic_load(&tones));
		CHECK(atomic_load(&b.audio_frames) > 0);
		CHECK(atomic_load(&b.video_frames) > 0);
		CHECK(atomic_load(&tones) > 0);
		lwrtc_media_track_stop(cam);
		CHECK(lwrtc_media_track_state(cam) == LWRTC_TRACK_ENDED);
	}
	lwrtc_rtp_sender_release(audio_sender);
	lwrtc_rtp_sender_release(sender);

	/* tear down */
	lwrtc_audio_sink_remove(b.audio_sink);
	lwrtc_video_sink_remove(b.video_sink);
	for (int i = 0; i < b.remote_track_count; i++)
		lwrtc_media_track_release(b.remote_tracks[i]);
	lwrtc_data_channel_release(b.remote_channel);
	lwrtc_data_channel_release(channel);
	lwrtc_rtp_transceiver_release(ta);
	lwrtc_peer_connection_close(a.pc);
	lwrtc_peer_connection_close(b.pc);
	CHECK(lwrtc_peer_connection_signaling_state(a.pc) == LWRTC_SIGNALING_CLOSED);
	lwrtc_peer_connection_release(a.pc);
	lwrtc_peer_connection_release(b.pc);
	lwrtc_media_track_release(mic);
	lwrtc_media_track_release(cam);
	lwrtc_factory_release(factory);
	lwrtc_terminate();

	free(offer.sdp);
	free(answer.sdp);
	printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
	return failures ? 1 : 0;
}
