# libwebrtc-c

A C API over [webrtc-sdk/libwebrtc](https://github.com/webrtc-sdk/libwebrtc) (the portable C++
wrapper around Google's WebRTC), limited to what FreeRDP's multimedia redirection channel needs
for WebRTC redirection (`com.microsoft.rdc.dvc.mmr.1`, `MSRDC_WEBRTC_RPC`). The host proxies a
browser's W3C WebRTC API object by object; this library gives the client the same objects:

| MMR object / operation | libwebrtc-c |
|---|---|
| `MEDIA_DEVICES` `ENUMERATE_DEVICES`, `DEVICE_CHANGE_EVENT` | `lwrtc_factory_enumerate_devices`, `lwrtc_factory_set_device_change_callback` |
| `MEDIA_DEVICES` `GET_USER_MEDIA` | `lwrtc_factory_create_microphone_track`, `lwrtc_factory_create_camera_track` |
| `MEDIA_STREAM_TRACK` enable/disable/stop | `lwrtc_media_track_set_enabled`, `lwrtc_media_track_stop` |
| `RTCPEERCONNECTION` create/close | `lwrtc_peer_connection_new` (observer = the events), `lwrtc_peer_connection_close` |
| `CREATE_OFFER`, `CREATE_ANSWER` | `lwrtc_peer_connection_create_offer/answer` |
| `SET_LOCAL_DESCRIPTION` (also implicit), `SET_REMOTE_DESCRIPTION` | `lwrtc_peer_connection_set_local/remote_description` |
| `ADD_ICE_CANDIDATE`, `ICE_CANDIDATE_EVENT` | `lwrtc_peer_connection_add_ice_candidate`, `on_ice_candidate` (+ `get_local_description`) |
| state events, `SIGNALING_STATE_CHANGE_EVENT` | observer callbacks, `*_name()` give the W3C strings |
| `ADD_TRACK`, `ADD_TRANSCEIVER`, `ON_TRACK_EVENT`, `REMOVE_TRACK_EVENT` | `lwrtc_peer_connection_add_track/add_transceiver`, `on_track`, `on_remove_track` |
| `REPLACE_TRACK`, `SET_TRANSCEIVER_DIRECTION` | `lwrtc_rtp_sender_replace_track`, `lwrtc_rtp_transceiver_set_direction` |
| `SET_PARAMETERS`, sender/receiver parameters in `SIGNALING_STATE` | `lwrtc_rtp_sender_get/set_parameters`, `lwrtc_rtp_receiver_get_parameters` |
| `GET_STATS` (with a track selector) | `lwrtc_peer_connection_get_stats/get_sender_stats/get_receiver_stats` |
| `INSERT_DTMF`, `DTMF_TONE_CHANGE_EVENT` | `lwrtc_rtp_sender_insert_dtmf`, `lwrtc_rtp_sender_set_dtmf_observer` |
| `RTCDATACHANNEL` | `lwrtc_peer_connection_create_data_channel`, `on_data_channel`, `lwrtc_data_channel_*` |
| `AUDIO_ELEMENT` muted/volume/srcObject, `SET_SINK_ID` | `lwrtc_audio_track_set_volume`, `lwrtc_factory_set_playout_device` |
| `VIDEO_ELEMENT` | `lwrtc_video_track_add_sink` + `lwrtc_video_frame_convert` (BGRA32 for FreeRDP) |
| `AUDIO_NODE` gain / analyser | `lwrtc_audio_track_add_sink` (PCM), the FFT is the channel's |

Conventions (reference counting, strings, threads) are at the top of
[`include/lwrtc/lwrtc.h`](include/lwrtc/lwrtc.h).

## Building

```sh
PKG_CONFIG_PATH=$HOME/dev/freerdp-deps64/lib/pkgconfig \
  meson setup build -Dcpp_link_args=-Wl,-rpath,$HOME/dev/freerdp-deps64/lib \
                    -Dc_link_args=-Wl,-rpath,$HOME/dev/freerdp-deps64/lib
ninja -C build
meson test -C build                    # two connections calling each other
./build/lwrtc-loopback --media         # also sends the microphone, the camera and DTMF
LWRTC_LOG=1 ./build/lwrtc-loopback     # with WebRTC's logs
```

It installs `lwrtc/lwrtc.h`, `libwebrtc-c.so` (exporting only `lwrtc_*`) and `libwebrtc-c.pc`.

## About the C++ wrapper

- `libwebrtc.so` carries its own libc++ and BoringSSL, hidden: it doesn't clash with OpenSSL or
  libstdc++ in the process. Only the wrapper's `portable::` types and ref-counted interfaces may
  cross the boundary; the few methods passing `std::map` can't be used (see `src/internal.h`).
- `libwebrtc.pc` defines `RTC_DESKTOP_DEVICE`, which changes the factory's vtable: always build
  with its flags.
- It translates WebRTC's states into its own enums, in a different order (ICE: completed before
  connected); the library converts by meaning.
- Once closed, its peer connection crashes when used: the library refuses calls after
  `lwrtc_peer_connection_close()`.
- It calls both `RTCDtmfSenderObserver::OnToneChange` overloads for each tone; only one is
  forwarded.

## Not available (from the wrapper)

- `GET_CSRCS` / `GET_SSRCS` (`getContributingSources()` / `getSynchronizationSources()`).
- `SET_CONFIGURATION` (`setConfiguration()`): the configuration is fixed at creation.
- Stats members of map type (`qualityLimitationDurations`...): only in `lwrtc_stats_json()`.
- Track settings (`getSettings()`), and labels of remote tracks: the channel builds them.
- Unsetting an encoding parameter once set (only setting one).
- One microphone and one speaker per factory: `setSinkId` applies to all remote audio, and every
  remote audio track plays as soon as it arrives (volume 0 until an `<audio>` element plays it).
- PulseAudio has no device GUIDs: ids are the device names, `"default"` for the default device.

Only built and tested on Linux x86_64 (gcc 13, libwebrtc 150.7871.04).
