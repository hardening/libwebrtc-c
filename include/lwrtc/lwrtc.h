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

#ifndef LWRTC_H
#define LWRTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @file
 *  The part of WebRTC that FreeRDP's multimedia redirection channel (MMR, the WebRTC
 *  redirection of a browser running in the RDP session) needs, in C. It mirrors the W3C API the
 *  host proxies: RTCPeerConnection, RTCRtpSender/Receiver/Transceiver, RTCDataChannel,
 *  MediaStreamTrack, getUserMedia/enumerateDevices, getStats, DTMF, plus raw access to the media
 *  (remote audio PCM, decoded video frames) to implement <audio>/<video> elements and WebAudio
 *  nodes.
 *
 *  Conventions:
 *  - objects are opaque and reference counted: a function returning an object gives the caller a
 *    reference (to drop with lwrtc_<type>_release()), objects passed to callbacks are borrowed
 *    (take a reference with lwrtc_<type>_add_ref() to keep them);
 *  - two handles may wrap the same WebRTC object (each call to lwrtc_rtp_transceiver_sender()
 *    returns a new handle): compare ids, not pointers;
 *  - strings returned as `char*` are allocated by the library and freed with lwrtc_free(), so are
 *    the arrays returned with a count;
 *  - callbacks run on WebRTC's threads (signalling thread for the peer connection, data channel
 *    and DTMF ones, media threads for audio/video sinks). They may call back into the library but
 *    must not block for long;
 *  - errors of asynchronous operations come as a message, NULL on success.
 */

#if defined(_WIN32)
#if defined(LWRTC_BUILDING)
#define LWRTC_API __declspec(dllexport)
#else
#define LWRTC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define LWRTC_API __attribute__((visibility("default")))
#else
#define LWRTC_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

	typedef enum
	{
		LWRTC_OK = 0,
		LWRTC_ERROR_INVALID_ARGUMENT,
		LWRTC_ERROR_INVALID_STATE,
		LWRTC_ERROR_NOT_FOUND,
		LWRTC_ERROR_NO_MEMORY,
		LWRTC_ERROR_FAILED
	} lwrtc_result;

	/** @brief frees a string or array returned by the library */
	LWRTC_API void lwrtc_free(void* ptr);

	/* ---- library ---- */

	typedef enum
	{
		LWRTC_LOG_VERBOSE,
		LWRTC_LOG_INFO,
		LWRTC_LOG_WARNING,
		LWRTC_LOG_ERROR,
		LWRTC_LOG_NONE
	} lwrtc_log_level;

	typedef void (*lwrtc_log_fn)(void* user, const char* message);

	/** @brief initializes WebRTC (SSL, its threads), with optional field trials
	 *  ("WebRTC-Foo/Enabled/"). Call once, before anything else. */
	LWRTC_API bool lwrtc_initialize(const char* const* field_trials, size_t count);
	LWRTC_API void lwrtc_terminate(void);

	/** @brief routes WebRTC's logs to `fn` (NULL to stop) */
	LWRTC_API void lwrtc_set_log_sink(lwrtc_log_level min_level, lwrtc_log_fn fn, void* user);

	/** @brief a random UUID, e.g. for track or stream ids */
	LWRTC_API char* lwrtc_create_uuid(void);

	/* ---- enums and their W3C names ---- */

	typedef enum
	{
		LWRTC_MEDIA_AUDIO,
		LWRTC_MEDIA_VIDEO,
		LWRTC_MEDIA_DATA,
		LWRTC_MEDIA_UNSUPPORTED
	} lwrtc_media_kind;

	typedef enum
	{
		LWRTC_SIGNALING_STABLE,
		LWRTC_SIGNALING_HAVE_LOCAL_OFFER,
		LWRTC_SIGNALING_HAVE_REMOTE_OFFER,
		LWRTC_SIGNALING_HAVE_LOCAL_PRANSWER,
		LWRTC_SIGNALING_HAVE_REMOTE_PRANSWER,
		LWRTC_SIGNALING_CLOSED
	} lwrtc_signaling_state;

	typedef enum
	{
		LWRTC_PEER_CONNECTION_NEW,
		LWRTC_PEER_CONNECTION_CONNECTING,
		LWRTC_PEER_CONNECTION_CONNECTED,
		LWRTC_PEER_CONNECTION_DISCONNECTED,
		LWRTC_PEER_CONNECTION_FAILED,
		LWRTC_PEER_CONNECTION_CLOSED
	} lwrtc_peer_connection_state;

	typedef enum
	{
		LWRTC_ICE_CONNECTION_NEW,
		LWRTC_ICE_CONNECTION_CHECKING,
		LWRTC_ICE_CONNECTION_CONNECTED,
		LWRTC_ICE_CONNECTION_COMPLETED,
		LWRTC_ICE_CONNECTION_FAILED,
		LWRTC_ICE_CONNECTION_DISCONNECTED,
		LWRTC_ICE_CONNECTION_CLOSED
	} lwrtc_ice_connection_state;

	typedef enum
	{
		LWRTC_ICE_GATHERING_NEW,
		LWRTC_ICE_GATHERING_GATHERING,
		LWRTC_ICE_GATHERING_COMPLETE
	} lwrtc_ice_gathering_state;

	typedef enum
	{
		LWRTC_DIRECTION_SENDRECV,
		LWRTC_DIRECTION_SENDONLY,
		LWRTC_DIRECTION_RECVONLY,
		LWRTC_DIRECTION_INACTIVE,
		LWRTC_DIRECTION_STOPPED
	} lwrtc_direction;

	typedef enum
	{
		LWRTC_DATA_CHANNEL_CONNECTING,
		LWRTC_DATA_CHANNEL_OPEN,
		LWRTC_DATA_CHANNEL_CLOSING,
		LWRTC_DATA_CHANNEL_CLOSED
	} lwrtc_data_channel_state;

	typedef enum
	{
		LWRTC_TRACK_LIVE,
		LWRTC_TRACK_ENDED
	} lwrtc_track_state;

	typedef enum
	{
		LWRTC_PRIORITY_VERY_LOW,
		LWRTC_PRIORITY_LOW,
		LWRTC_PRIORITY_MEDIUM,
		LWRTC_PRIORITY_HIGH
	} lwrtc_priority;

	typedef enum
	{
		LWRTC_DEGRADATION_DISABLED,
		LWRTC_DEGRADATION_MAINTAIN_FRAMERATE,
		LWRTC_DEGRADATION_MAINTAIN_RESOLUTION,
		LWRTC_DEGRADATION_BALANCED,
		LWRTC_DEGRADATION_MAINTAIN_FRAMERATE_AND_RESOLUTION
	} lwrtc_degradation_preference;

	/** W3C names: "audio", "stable", "have-local-offer", "connected", "sendrecv", "open", "live",
	 *  "high", "maintain-framerate"... */
	LWRTC_API const char* lwrtc_media_kind_name(lwrtc_media_kind kind);
	LWRTC_API const char* lwrtc_signaling_state_name(lwrtc_signaling_state state);
	LWRTC_API const char* lwrtc_peer_connection_state_name(lwrtc_peer_connection_state state);
	LWRTC_API const char* lwrtc_ice_connection_state_name(lwrtc_ice_connection_state state);
	LWRTC_API const char* lwrtc_ice_gathering_state_name(lwrtc_ice_gathering_state state);
	LWRTC_API const char* lwrtc_direction_name(lwrtc_direction direction);
	LWRTC_API const char* lwrtc_data_channel_state_name(lwrtc_data_channel_state state);
	LWRTC_API const char* lwrtc_track_state_name(lwrtc_track_state state);
	LWRTC_API const char* lwrtc_priority_name(lwrtc_priority priority);
	LWRTC_API const char* lwrtc_degradation_preference_name(lwrtc_degradation_preference pref);

	LWRTC_API bool lwrtc_media_kind_from_name(const char* name, lwrtc_media_kind* kind);
	LWRTC_API bool lwrtc_direction_from_name(const char* name, lwrtc_direction* direction);
	LWRTC_API bool lwrtc_priority_from_name(const char* name, lwrtc_priority* priority);
	LWRTC_API bool lwrtc_degradation_preference_from_name(const char* name,
	                                                      lwrtc_degradation_preference* pref);

	/* ---- objects ---- */

	typedef struct lwrtc_factory lwrtc_factory;
	typedef struct lwrtc_media_track lwrtc_media_track;
	typedef struct lwrtc_audio_sink lwrtc_audio_sink;
	typedef struct lwrtc_video_sink lwrtc_video_sink;
	typedef struct lwrtc_video_frame lwrtc_video_frame;
	typedef struct lwrtc_peer_connection lwrtc_peer_connection;
	typedef struct lwrtc_rtp_sender lwrtc_rtp_sender;
	typedef struct lwrtc_rtp_receiver lwrtc_rtp_receiver;
	typedef struct lwrtc_rtp_transceiver lwrtc_rtp_transceiver;
	typedef struct lwrtc_data_channel lwrtc_data_channel;
	typedef struct lwrtc_stats_report lwrtc_stats_report;

#define LWRTC_DECLARE_REFCOUNTED(type)            \
	LWRTC_API type* type##_add_ref(type* object); \
	LWRTC_API void type##_release(type* object);

	LWRTC_DECLARE_REFCOUNTED(lwrtc_factory)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_media_track)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_video_frame)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_peer_connection)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_rtp_sender)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_rtp_receiver)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_rtp_transceiver)
	LWRTC_DECLARE_REFCOUNTED(lwrtc_data_channel)

	/** @brief releases the `count` objects of an array returned by the library, then the array */
	LWRTC_API void lwrtc_rtp_senders_free(lwrtc_rtp_sender** senders, size_t count);
	LWRTC_API void lwrtc_rtp_receivers_free(lwrtc_rtp_receiver** receivers, size_t count);
	LWRTC_API void lwrtc_rtp_transceivers_free(lwrtc_rtp_transceiver** transceivers, size_t count);
	LWRTC_API void lwrtc_media_tracks_free(lwrtc_media_track** tracks, size_t count);

	/* ---- factory and devices (navigator.mediaDevices) ---- */

	/** @brief the peer connection factory, with its audio device (one microphone and one
	 *  speaker for the whole factory: every remote audio track is played on it, mixed) */
	LWRTC_API lwrtc_factory* lwrtc_factory_new(void);

	typedef enum
	{
		LWRTC_DEVICE_AUDIO_INPUT,
		LWRTC_DEVICE_AUDIO_OUTPUT,
		LWRTC_DEVICE_VIDEO_INPUT
	} lwrtc_device_kind;

#define LWRTC_DEVICE_STRING_SIZE 256

	typedef struct
	{
		lwrtc_device_kind kind;
		/** stable id: "default" for the system's default audio device, else WebRTC's device
		 *  GUID, or the device name where the platform has none (PulseAudio) */
		char device_id[LWRTC_DEVICE_STRING_SIZE];
		char label[LWRTC_DEVICE_STRING_SIZE];
		char group_id[LWRTC_DEVICE_STRING_SIZE]; /* "" (WebRTC doesn't group devices) */
	} lwrtc_device_info;

	/** @brief enumerateDevices(): audio inputs, audio outputs, then cameras. The array is freed
	 *  with lwrtc_free(). */
	LWRTC_API lwrtc_device_info* lwrtc_factory_enumerate_devices(lwrtc_factory* factory,
	                                                             size_t* count);

	typedef void (*lwrtc_device_change_fn)(void* user);
	/** @brief devicechange: called when audio devices come or go (NULL to stop) */
	LWRTC_API lwrtc_result lwrtc_factory_set_device_change_callback(lwrtc_factory* factory,
	                                                                lwrtc_device_change_fn fn,
	                                                                void* user);

	/** @brief selects the speaker every remote audio track is played on (setSinkId); NULL, ""
	 *  and "default" select the default one */
	LWRTC_API lwrtc_result lwrtc_factory_set_playout_device(lwrtc_factory* factory,
	                                                        const char* device_id);

	typedef struct
	{
		bool echo_cancellation;
		bool auto_gain_control;
		bool noise_suppression;
		bool highpass_filter;
	} lwrtc_audio_options;

	/** @brief getUserMedia({audio}): a microphone track. `device_id` NULL, "" or "default"
	 *  takes the default microphone (there is a single capture device per factory: this selects
	 *  it for every microphone track). `options` NULL: echo cancellation, gain control and noise
	 *  suppression on. `track_id` NULL: a random one. */
	LWRTC_API lwrtc_media_track*
	lwrtc_factory_create_microphone_track(lwrtc_factory* factory, const char* device_id,
	                                      const lwrtc_audio_options* options, const char* track_id);

	/** @brief getUserMedia({video}): a camera track, capture started. `device_id` NULL or "":
	 *  the first camera. 0 for a size or rate takes 640x480 at 30 fps. */
	LWRTC_API lwrtc_media_track*
	lwrtc_factory_create_camera_track(lwrtc_factory* factory, const char* device_id, uint32_t width,
	                                  uint32_t height, uint32_t fps, const char* track_id);

	/* ---- media tracks (MediaStreamTrack) ---- */

	LWRTC_API lwrtc_media_kind lwrtc_media_track_kind(lwrtc_media_track* track);
	LWRTC_API char* lwrtc_media_track_id(lwrtc_media_track* track);
	/** @brief the device label for local tracks, "" for remote ones */
	LWRTC_API char* lwrtc_media_track_label(lwrtc_media_track* track);
	/** @brief the device id a local track captures from, "" for remote ones */
	LWRTC_API char* lwrtc_media_track_device_id(lwrtc_media_track* track);
	LWRTC_API bool lwrtc_media_track_is_remote(lwrtc_media_track* track);
	LWRTC_API bool lwrtc_media_track_enabled(lwrtc_media_track* track);
	LWRTC_API void lwrtc_media_track_set_enabled(lwrtc_media_track* track, bool enabled);
	LWRTC_API lwrtc_track_state lwrtc_media_track_state(lwrtc_media_track* track);
	/** @brief stop(): stops a camera's capture and disables the track, which then reads ended */
	LWRTC_API void lwrtc_media_track_stop(lwrtc_media_track* track);

	/** @brief volume of a remote audio track as played on the speaker, 0 (silent) to 1 (as
	 *  received). WebRTC plays every remote audio track from the start: an <audio> element that
	 *  isn't playing is volume 0. */
	LWRTC_API lwrtc_result lwrtc_audio_track_set_volume(lwrtc_media_track* track, double volume);

	/** @brief PCM of an audio track (interleaved, `bits_per_sample` 16 in practice) */
	typedef void (*lwrtc_audio_data_fn)(void* user, const void* data, int bits_per_sample,
	                                    int sample_rate, size_t channels, size_t frames);
	LWRTC_API lwrtc_audio_sink* lwrtc_audio_track_add_sink(lwrtc_media_track* track,
	                                                       lwrtc_audio_data_fn fn, void* user);
	/** @brief detaches and frees the sink: no call is running or will run once it returns */
	LWRTC_API void lwrtc_audio_sink_remove(lwrtc_audio_sink* sink);

	/** @brief decoded (remote) or captured (local) frames of a video track; `frame` is borrowed */
	typedef void (*lwrtc_video_frame_fn)(void* user, lwrtc_video_frame* frame);
	LWRTC_API lwrtc_video_sink* lwrtc_video_track_add_sink(lwrtc_media_track* track,
	                                                       lwrtc_video_frame_fn fn, void* user);
	/** @brief detaches and frees the sink: no call is running or will run once it returns */
	LWRTC_API void lwrtc_video_sink_remove(lwrtc_video_sink* sink);

	/* ---- video frames ---- */

	typedef enum
	{
		LWRTC_PIXEL_BGRA32, /* bytes B, G, R, A (FreeRDP's PIXEL_FORMAT_BGRA32) */
		LWRTC_PIXEL_RGBA32  /* bytes R, G, B, A */
	} lwrtc_pixel_format;

	/** @brief an I420 frame (copies the planes), mostly for tests */
	LWRTC_API lwrtc_video_frame* lwrtc_video_frame_new_i420(int width, int height, const uint8_t* y,
	                                                        int stride_y, const uint8_t* u,
	                                                        int stride_u, const uint8_t* v,
	                                                        int stride_v);
	LWRTC_API int lwrtc_video_frame_width(lwrtc_video_frame* frame);
	LWRTC_API int lwrtc_video_frame_height(lwrtc_video_frame* frame);
	/** @brief clockwise rotation to apply for display: 0, 90, 180 or 270 */
	LWRTC_API int lwrtc_video_frame_rotation(lwrtc_video_frame* frame);
	LWRTC_API void lwrtc_video_frame_i420(lwrtc_video_frame* frame, const uint8_t** y,
	                                      int* stride_y, const uint8_t** u, int* stride_u,
	                                      const uint8_t** v, int* stride_v);
	/** @brief converts to 32 bpp at the frame's size (`width` and `height` must be the frame's) */
	LWRTC_API lwrtc_result lwrtc_video_frame_convert(lwrtc_video_frame* frame,
	                                                 lwrtc_pixel_format format, uint8_t* dst,
	                                                 int dst_stride, int width, int height);

	/* ---- peer connection (RTCPeerConnection) ---- */

	typedef enum
	{
		LWRTC_ICE_TRANSPORT_ALL,
		LWRTC_ICE_TRANSPORT_RELAY
	} lwrtc_ice_transport_policy;

	typedef enum
	{
		LWRTC_BUNDLE_BALANCED,
		LWRTC_BUNDLE_MAX_BUNDLE,
		LWRTC_BUNDLE_MAX_COMPAT
	} lwrtc_bundle_policy;

	typedef struct
	{
		const char* const* urls;
		size_t url_count;
		const char* username;   /* may be NULL */
		const char* credential; /* may be NULL */
	} lwrtc_ice_server;

	/** at most LWRTC_MAX_ICE_URLS urls in all (the C++ wrapper's limit) */
#define LWRTC_MAX_ICE_URLS 8

	typedef struct
	{
		const lwrtc_ice_server* ice_servers;
		size_t ice_server_count;
		lwrtc_ice_transport_policy ice_transport_policy;
		lwrtc_bundle_policy bundle_policy; /* browsers default to balanced */
		int ice_candidate_pool_size;
	} lwrtc_configuration;

	/** @brief events of a peer connection, all optional. Objects passed are borrowed. */
	typedef struct
	{
		void* user;
		void (*on_signaling_state)(void* user, lwrtc_signaling_state state);
		void (*on_connection_state)(void* user, lwrtc_peer_connection_state state);
		void (*on_ice_connection_state)(void* user, lwrtc_ice_connection_state state);
		void (*on_ice_gathering_state)(void* user, lwrtc_ice_gathering_state state);
		/** a local candidate; end of gathering is on_ice_gathering_state(COMPLETE) */
		void (*on_ice_candidate)(void* user, const char* candidate, const char* sdp_mid,
		                         int sdp_mline_index);
		/** ontrack: the transceiver whose receiver got a new remote track; `stream_ids` are the
		 *  remote streams the track belongs to */
		void (*on_track)(void* user, lwrtc_rtp_transceiver* transceiver,
		                 const char* const* stream_ids, size_t stream_count);
		/** the remote side removed the track of this receiver */
		void (*on_remove_track)(void* user, lwrtc_rtp_receiver* receiver);
		/** ondatachannel: a data channel the remote side opened */
		void (*on_data_channel)(void* user, lwrtc_data_channel* channel);
		void (*on_negotiation_needed)(void* user);
	} lwrtc_peer_connection_observer;

	/** @brief new RTCPeerConnection(configuration). `configuration` may be NULL (no ICE
	 *  server). The observer is copied. */
	LWRTC_API lwrtc_peer_connection*
	lwrtc_peer_connection_new(lwrtc_factory* factory, const lwrtc_configuration* configuration,
	                          const lwrtc_peer_connection_observer* observer);

	/** @brief close(): no event comes after it returns. Also done by the last release. Then
	 *  operations fail ("InvalidStateError..." or LWRTC_ERROR_INVALID_STATE), getters return the
	 *  closed states and lists are empty. Must not run while another thread calls the same
	 *  connection (the C++ wrapper's connection is unusable once closed). */
	LWRTC_API void lwrtc_peer_connection_close(lwrtc_peer_connection* pc);

	typedef struct
	{
		bool ice_restart;
		bool voice_activity_detection; /* browsers default to true */
	} lwrtc_offer_options;

	/** result of createOffer/createAnswer and of the description getters: `error` NULL on
	 *  success; `type` and `sdp` NULL on failure, or when there is no description */
	typedef void (*lwrtc_description_fn)(void* user, const char* type, const char* sdp,
	                                     const char* error);
	/** result of operations that return nothing */
	typedef void (*lwrtc_done_fn)(void* user, const char* error);

	/** `options` may be NULL */
	LWRTC_API void lwrtc_peer_connection_create_offer(lwrtc_peer_connection* pc,
	                                                  const lwrtc_offer_options* options,
	                                                  lwrtc_description_fn fn, void* user);
	LWRTC_API void lwrtc_peer_connection_create_answer(lwrtc_peer_connection* pc,
	                                                   lwrtc_description_fn fn, void* user);
	/** @brief setLocalDescription(). With `sdp` NULL, the implicit form: creates the offer or
	 *  answer the signalling state calls for and sets it. */
	LWRTC_API void lwrtc_peer_connection_set_local_description(lwrtc_peer_connection* pc,
	                                                           const char* type, const char* sdp,
	                                                           lwrtc_done_fn fn, void* user);
	LWRTC_API void lwrtc_peer_connection_set_remote_description(lwrtc_peer_connection* pc,
	                                                            const char* type, const char* sdp,
	                                                            lwrtc_done_fn fn, void* user);
	LWRTC_API void lwrtc_peer_connection_get_local_description(lwrtc_peer_connection* pc,
	                                                           lwrtc_description_fn fn, void* user);
	LWRTC_API void lwrtc_peer_connection_get_remote_description(lwrtc_peer_connection* pc,
	                                                            lwrtc_description_fn fn,
	                                                            void* user);
	/** @brief addIceCandidate(); an empty or NULL `candidate` (end of candidates) is accepted
	 *  and ignored */
	LWRTC_API lwrtc_result lwrtc_peer_connection_add_ice_candidate(lwrtc_peer_connection* pc,
	                                                               const char* candidate,
	                                                               const char* sdp_mid,
	                                                               int sdp_mline_index);
	LWRTC_API void lwrtc_peer_connection_restart_ice(lwrtc_peer_connection* pc);

	LWRTC_API lwrtc_signaling_state
	lwrtc_peer_connection_signaling_state(lwrtc_peer_connection* pc);
	LWRTC_API lwrtc_peer_connection_state
	lwrtc_peer_connection_connection_state(lwrtc_peer_connection* pc);
	LWRTC_API lwrtc_ice_connection_state
	lwrtc_peer_connection_ice_connection_state(lwrtc_peer_connection* pc);
	LWRTC_API lwrtc_ice_gathering_state
	lwrtc_peer_connection_ice_gathering_state(lwrtc_peer_connection* pc);

	/** @brief addTrack(track, ...streams): NULL on failure */
	LWRTC_API lwrtc_rtp_sender* lwrtc_peer_connection_add_track(lwrtc_peer_connection* pc,
	                                                            lwrtc_media_track* track,
	                                                            const char* const* stream_ids,
	                                                            size_t stream_count);
	LWRTC_API lwrtc_result lwrtc_peer_connection_remove_track(lwrtc_peer_connection* pc,
	                                                          lwrtc_rtp_sender* sender);

	/** an RTCRtpEncodingParameters. Numbers <= 0 mean "not set", as does
	 *  scale_resolution_down_by == 1. */
	typedef struct
	{
		char* rid; /* "" none */
		bool active;
		int max_bitrate_bps;
		int min_bitrate_bps;
		double max_framerate;
		double scale_resolution_down_by;
		int num_temporal_layers;
		char* scalability_mode; /* "" none */
		double bitrate_priority;
		lwrtc_priority network_priority;
		bool adaptive_ptime;
		uint32_t ssrc; /* read only */
	} lwrtc_rtp_encoding;

	/** @brief an encoding with WebRTC's defaults (active, nothing set); the strings are owned
	 *  by the caller when passing them in */
	LWRTC_API void lwrtc_rtp_encoding_init(lwrtc_rtp_encoding* encoding);

	typedef struct
	{
		lwrtc_direction direction;
		const char* const* stream_ids;
		size_t stream_count;
		const lwrtc_rtp_encoding* send_encodings; /* simulcast layers */
		size_t send_encoding_count;
	} lwrtc_transceiver_init;

	/** @brief addTransceiver(track or kind, init): give `track`, or NULL and a `kind`. `init`
	 *  may be NULL (sendrecv). NULL on failure. */
	LWRTC_API lwrtc_rtp_transceiver*
	lwrtc_peer_connection_add_transceiver(lwrtc_peer_connection* pc, lwrtc_media_track* track,
	                                      lwrtc_media_kind kind,
	                                      const lwrtc_transceiver_init* init);

	LWRTC_API lwrtc_rtp_sender** lwrtc_peer_connection_senders(lwrtc_peer_connection* pc,
	                                                           size_t* count);
	LWRTC_API lwrtc_rtp_receiver** lwrtc_peer_connection_receivers(lwrtc_peer_connection* pc,
	                                                               size_t* count);
	LWRTC_API lwrtc_rtp_transceiver** lwrtc_peer_connection_transceivers(lwrtc_peer_connection* pc,
	                                                                     size_t* count);

	typedef struct
	{
		const char* label;
		bool ordered;
		int max_packet_life_time; /* ms, < 0 unset */
		int max_retransmits;      /* < 0 unset */
		const char* protocol;     /* sub-protocol, NULL or "" none */
		bool negotiated;
		int id; /* when negotiated */
	} lwrtc_data_channel_init;

	/** @brief createDataChannel(label, init): `init` NULL for an ordered, reliable channel.
	 *  NULL on failure. */
	LWRTC_API lwrtc_data_channel*
	lwrtc_peer_connection_create_data_channel(lwrtc_peer_connection* pc, const char* label,
	                                          const lwrtc_data_channel_init* init);

	/* ---- statistics (getStats) ---- */

	/** `report` is borrowed, NULL on failure */
	typedef void (*lwrtc_stats_fn)(void* user, const lwrtc_stats_report* report, const char* error);

	/** @brief getStats(): the whole connection */
	LWRTC_API void lwrtc_peer_connection_get_stats(lwrtc_peer_connection* pc, lwrtc_stats_fn fn,
	                                               void* user);
	/** @brief getStats(selector) for one sender's or receiver's track */
	LWRTC_API void lwrtc_peer_connection_get_sender_stats(lwrtc_peer_connection* pc,
	                                                      lwrtc_rtp_sender* sender,
	                                                      lwrtc_stats_fn fn, void* user);
	LWRTC_API void lwrtc_peer_connection_get_receiver_stats(lwrtc_peer_connection* pc,
	                                                        lwrtc_rtp_receiver* receiver,
	                                                        lwrtc_stats_fn fn, void* user);

	LWRTC_API size_t lwrtc_stats_report_count(const lwrtc_stats_report* report);
	/** an RTCStats: id, type ("outbound-rtp"...), timestamp in µs */
	LWRTC_API const char* lwrtc_stats_id(const lwrtc_stats_report* report, size_t index);
	LWRTC_API const char* lwrtc_stats_type(const lwrtc_stats_report* report, size_t index);
	LWRTC_API int64_t lwrtc_stats_timestamp_us(const lwrtc_stats_report* report, size_t index);
	/** the defined members, values as JavaScript would print them: numbers and booleans as
	 *  text, strings as is, sequences and maps as JSON */
	LWRTC_API size_t lwrtc_stats_member_count(const lwrtc_stats_report* report, size_t index);
	LWRTC_API const char* lwrtc_stats_member_name(const lwrtc_stats_report* report, size_t index,
	                                              size_t member);
	LWRTC_API const char* lwrtc_stats_member_value(const lwrtc_stats_report* report, size_t index,
	                                               size_t member);
	/** the whole RTCStats as JSON */
	LWRTC_API const char* lwrtc_stats_json(const lwrtc_stats_report* report, size_t index);

	/* ---- RTP (RTCRtpSender, RTCRtpReceiver, RTCRtpTransceiver) ---- */

	typedef struct
	{
		char* mime_type; /* "audio/opus" */
		int payload_type;
		int clock_rate;
		int channels;        /* 0 for video */
		char* sdp_fmtp_line; /* "minptime=10;useinbandfec=1", "" none */
	} lwrtc_rtp_codec;

	typedef struct
	{
		char* uri;
		int id;
		bool encrypted;
	} lwrtc_rtp_header_extension;

	/** RTCRtpSendParameters / RTCRtpReceiveParameters */
	typedef struct
	{
		char* transaction_id; /* senders only */
		char* mid;
		lwrtc_rtp_codec* codecs;
		size_t codec_count;
		lwrtc_rtp_header_extension* header_extensions;
		size_t header_extension_count;
		lwrtc_rtp_encoding* encodings; /* senders only */
		size_t encoding_count;
		char* rtcp_cname;
		bool rtcp_reduced_size;
		lwrtc_degradation_preference degradation_preference;
	} lwrtc_rtp_parameters;

	LWRTC_API void lwrtc_rtp_parameters_free(lwrtc_rtp_parameters* parameters);

	LWRTC_API char* lwrtc_rtp_sender_id(lwrtc_rtp_sender* sender);
	LWRTC_API lwrtc_media_kind lwrtc_rtp_sender_kind(lwrtc_rtp_sender* sender);
	/** @brief the track sent, NULL if none */
	LWRTC_API lwrtc_media_track* lwrtc_rtp_sender_track(lwrtc_rtp_sender* sender);
	/** @brief replaceTrack(): `track` NULL stops sending */
	LWRTC_API lwrtc_result lwrtc_rtp_sender_replace_track(lwrtc_rtp_sender* sender,
	                                                      lwrtc_media_track* track);
	LWRTC_API char** lwrtc_rtp_sender_stream_ids(lwrtc_rtp_sender* sender, size_t* count);
	LWRTC_API lwrtc_result lwrtc_rtp_sender_set_stream_ids(lwrtc_rtp_sender* sender,
	                                                       const char* const* stream_ids,
	                                                       size_t count);
	LWRTC_API uint32_t lwrtc_rtp_sender_ssrc(lwrtc_rtp_sender* sender);
	/** @brief getParameters(), to free with lwrtc_rtp_parameters_free() */
	LWRTC_API lwrtc_rtp_parameters* lwrtc_rtp_sender_get_parameters(lwrtc_rtp_sender* sender);
	/** @brief setParameters(): applies the encodings (in order, their count must not change)
	 *  and the degradation preference; the other members are read only */
	LWRTC_API lwrtc_result lwrtc_rtp_sender_set_parameters(lwrtc_rtp_sender* sender,
	                                                       const lwrtc_rtp_parameters* parameters);

	/** DTMF (RTCDTMFSender) of an audio sender */
	LWRTC_API bool lwrtc_rtp_sender_can_insert_dtmf(lwrtc_rtp_sender* sender);
	/** @brief insertDTMF(tones, duration, interToneGap), in ms */
	LWRTC_API lwrtc_result lwrtc_rtp_sender_insert_dtmf(lwrtc_rtp_sender* sender, const char* tones,
	                                                    int duration, int inter_tone_gap);
	/** ontonechange: `tone` "" once the buffer is played */
	typedef void (*lwrtc_dtmf_tone_fn)(void* user, const char* tone, const char* tone_buffer);
	/** @brief one observer per sender's DTMF sender; NULL `fn` removes it */
	LWRTC_API lwrtc_result lwrtc_rtp_sender_set_dtmf_observer(lwrtc_rtp_sender* sender,
	                                                          lwrtc_dtmf_tone_fn fn, void* user);

	LWRTC_API char* lwrtc_rtp_receiver_id(lwrtc_rtp_receiver* receiver);
	LWRTC_API lwrtc_media_kind lwrtc_rtp_receiver_kind(lwrtc_rtp_receiver* receiver);
	/** @brief the remote track (always there, even before any media) */
	LWRTC_API lwrtc_media_track* lwrtc_rtp_receiver_track(lwrtc_rtp_receiver* receiver);
	LWRTC_API char** lwrtc_rtp_receiver_stream_ids(lwrtc_rtp_receiver* receiver, size_t* count);
	LWRTC_API lwrtc_rtp_parameters* lwrtc_rtp_receiver_get_parameters(lwrtc_rtp_receiver* receiver);
	/** @brief jitterBufferTarget, in seconds */
	LWRTC_API void lwrtc_rtp_receiver_set_jitter_buffer_minimum_delay(lwrtc_rtp_receiver* receiver,
	                                                                  double seconds);

	/** @brief the mid, "" before negotiation */
	LWRTC_API char* lwrtc_rtp_transceiver_mid(lwrtc_rtp_transceiver* transceiver);
	LWRTC_API lwrtc_media_kind lwrtc_rtp_transceiver_kind(lwrtc_rtp_transceiver* transceiver);
	LWRTC_API lwrtc_rtp_sender* lwrtc_rtp_transceiver_sender(lwrtc_rtp_transceiver* transceiver);
	LWRTC_API lwrtc_rtp_receiver*
	lwrtc_rtp_transceiver_receiver(lwrtc_rtp_transceiver* transceiver);
	LWRTC_API lwrtc_direction lwrtc_rtp_transceiver_direction(lwrtc_rtp_transceiver* transceiver);
	/** @brief currentDirection: false if not negotiated yet */
	LWRTC_API bool lwrtc_rtp_transceiver_current_direction(lwrtc_rtp_transceiver* transceiver,
	                                                       lwrtc_direction* direction);
	/** @brief direction = ...; the error message (to lwrtc_free) if refused, else NULL */
	LWRTC_API char* lwrtc_rtp_transceiver_set_direction(lwrtc_rtp_transceiver* transceiver,
	                                                    lwrtc_direction direction);
	/** @brief stop(): the error message (to lwrtc_free) if refused, else NULL */
	LWRTC_API char* lwrtc_rtp_transceiver_stop(lwrtc_rtp_transceiver* transceiver);
	LWRTC_API bool lwrtc_rtp_transceiver_stopped(lwrtc_rtp_transceiver* transceiver);

	/* ---- data channels (RTCDataChannel) ---- */

	typedef struct
	{
		void* user;
		void (*on_state)(void* user, lwrtc_data_channel_state state);
		void (*on_message)(void* user, const uint8_t* data, size_t length, bool binary);
	} lwrtc_data_channel_observer;

	/** @brief sets (copied) or with NULL removes the channel's observer. Messages arriving
	 *  before an observer is set are lost: set it from on_data_channel. */
	LWRTC_API void lwrtc_data_channel_set_observer(lwrtc_data_channel* channel,
	                                               const lwrtc_data_channel_observer* observer);
	LWRTC_API char* lwrtc_data_channel_label(lwrtc_data_channel* channel);
	LWRTC_API int lwrtc_data_channel_id(lwrtc_data_channel* channel);
	LWRTC_API lwrtc_data_channel_state lwrtc_data_channel_state_get(lwrtc_data_channel* channel);
	LWRTC_API uint64_t lwrtc_data_channel_buffered_amount(lwrtc_data_channel* channel);
	LWRTC_API lwrtc_result lwrtc_data_channel_send(lwrtc_data_channel* channel, const uint8_t* data,
	                                               size_t length, bool binary);
	LWRTC_API void lwrtc_data_channel_close(lwrtc_data_channel* channel);

#ifdef __cplusplus
}
#endif

#endif /* LWRTC_H */
