#ifndef CTBROWSER__AUDIO__HPP
#define CTBROWSER__AUDIO__HPP

#include <cstdint>

#include <cstddef>

#include "image.hpp" // embedded_asset / find_asset
#include <SDL3/SDL.h>
#ifdef CTBROWSER_WITH_MIXER
#include <SDL3_mixer/SDL_mixer.h>
#endif
#ifndef CTBROWSER_IN_A_MODULE
#include <map>
#include <string>
#include <vector>
#endif

// Sound for games. With SDL3_mixer (CTBROWSER_WITH_MIXER, set by the
// build when the library is found) sounds load once as MIX_Audio -
// WAV, OGG, MP3, FLAC - and play on pooled tracks with proper mixing
// and a master gain. Without it, a built-in fallback plays plain WAV
// files through raw SDL audio streams, so core SDL3 alone still makes
// noise. Either way the surface is the same: play(path), set_volume.
// Under SDL_AUDIODRIVER=dummy everything succeeds silently (headless
// runs, CI).

namespace ctbrowser {

#ifdef CTBROWSER_WITH_MIXER

class audio_mixer {
public:
	// compile-time-embedded assets, consulted before the filesystem
	const std::vector<embedded_asset> * embedded = nullptr;

	audio_mixer() = default;
	audio_mixer(const audio_mixer &) = delete;
	audio_mixer & operator=(const audio_mixer &) = delete;

	~audio_mixer() {
		for (MIX_Track * t : tracks_) { MIX_DestroyTrack(t); }
		for (auto & [path, a] : cache_) {
			if (a != nullptr) { MIX_DestroyAudio(a); }
		}
		if (mixer_ != nullptr) { MIX_DestroyMixer(mixer_); }
	}

	void set_volume(float v) {
		volume_ = v < 0 ? 0.0f : v > 1 ? 1.0f : v;
		if (mixer_ != nullptr) { MIX_SetMixerGain(mixer_, volume_); }
	}

	bool play(const std::string & path) { return play(path, false) != 0; }

	// play `path` (looping if `loop`); returns a handle for stop(), 0 on failure.
	// SDL3_mixer's MIX_PlayTrack takes options as an SDL_PropertiesID - loops are
	// the MIX_PROP_PLAY_LOOPS_NUMBER property (-1 = infinite).
	std::int32_t play(const std::string & path, bool loop) {
		if (!ready()) { return 0; }
		MIX_Audio * audio = load(path);
		if (audio == nullptr) { return 0; }
		MIX_Track * track = idle_track();
		if (track == nullptr) { return 0; }
		if (!MIX_SetTrackAudio(track, audio)) { return 0; }
		SDL_PropertiesID options = 0;
		if (loop) {
			options = SDL_CreateProperties();
			SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
		}
		const bool ok = MIX_PlayTrack(track, options);
		if (options != 0) { SDL_DestroyProperties(options); }
		if (!ok) { return 0; }
		for (std::size_t i = 0; i < tracks_.size(); ++i) {
			if (tracks_[i] == track) { return static_cast<std::int32_t>(i) + 1; }
		}
		return 0;
	}
	// stop a track started by play(); a stale handle (track reused) is a no-op-ish
	void stop(std::int32_t handle) {
		if (handle > 0 && handle <= static_cast<std::int32_t>(tracks_.size())) {
			MIX_StopTrack(tracks_[static_cast<std::size_t>(handle - 1)], 0);
		}
	}

private:
	bool ready() {
		if (mixer_ != nullptr) { return true; }
		if (!MIX_Init()) { return false; }
		mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
		if (mixer_ == nullptr) { return false; }
		MIX_SetMixerGain(mixer_, volume_);
		return true;
	}
	MIX_Audio * load(const std::string & path) {
		if (const auto it = cache_.find(path); it != cache_.end()) { return it->second; }
		MIX_Audio * a = nullptr;
		if (const embedded_asset * em = find_asset(embedded, path)) {
			a = MIX_LoadAudio_IO(mixer_, SDL_IOFromConstMem(em->data, em->size), true,
			                     true);
		}
		if (a == nullptr) { a = MIX_LoadAudio(mixer_, path.c_str(), true); }
		cache_.emplace(path, a); // nullptr negative-caches the miss
		return a;
	}
	MIX_Track * idle_track() {
		for (MIX_Track * t : tracks_) {
			if (!MIX_TrackPlaying(t)) { return t; }
		}
		if (tracks_.size() >= 32) { return tracks_.front(); } // steal the oldest
		MIX_Track * t = MIX_CreateTrack(mixer_);
		if (t != nullptr) { tracks_.push_back(t); }
		return t;
	}

	MIX_Mixer * mixer_ = nullptr;
	std::map<std::string, MIX_Audio *> cache_;
	std::vector<MIX_Track *> tracks_;
	float volume_ = 1.0f;
};

#else // fallback: plain WAV through raw SDL audio streams

class audio_mixer {
public:
	// compile-time-embedded assets, consulted before the filesystem
	const std::vector<embedded_asset> * embedded = nullptr;

	audio_mixer() = default;
	audio_mixer(const audio_mixer &) = delete;
	audio_mixer & operator=(const audio_mixer &) = delete;

	~audio_mixer() {
		for (SDL_AudioStream * s : streams_) { SDL_DestroyAudioStream(s); }
		for (auto & [path, wav] : cache_) { SDL_free(wav.data); }
	}

	void set_volume(float v) { volume_ = v < 0 ? 0.0f : v > 1 ? 1.0f : v; }

	bool play(const std::string & path) { return play(path, false) != 0; }

	// load (cached) and play a WAV. The raw-stream fallback plays ONCE (looping
	// needs re-queueing; SDL3_mixer handles it) and its handle is a monotonic id
	// used by stop(); 0 on failure.
	std::int32_t play(const std::string & path, bool /*loop*/) {
		if (!SDL_WasInit(SDL_INIT_AUDIO)) {
			if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { return 0; }
		}
		wav_data * wav = load(path);
		if (wav == nullptr) { return 0; }
		SDL_AudioStream * stream = SDL_OpenAudioDeviceStream(
		    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav->spec, nullptr, nullptr);
		if (stream == nullptr) { return 0; }
		SDL_SetAudioStreamGain(stream, volume_);
		SDL_PutAudioStreamData(stream, wav->data, static_cast<std::int32_t>(wav->len));
		SDL_FlushAudioStream(stream);
		SDL_ResumeAudioStreamDevice(stream);
		const std::int32_t id = ++next_id_;
		live_.emplace(id, stream);
		streams_.push_back(stream);
		reap();
		return id;
	}
	// stop a stream started by play() (best-effort: it may already have reaped)
	void stop(std::int32_t handle) {
		const auto it = live_.find(handle);
		if (it == live_.end()) { return; }
		SDL_AudioStream * s = it->second;
		live_.erase(it);
		std::erase(streams_, s);
		SDL_DestroyAudioStream(s);
	}

private:
	struct wav_data {
		SDL_AudioSpec spec{};
		Uint8 * data = nullptr;
		Uint32 len = 0;
	};

	wav_data * load(const std::string & path) {
		if (const auto it = cache_.find(path); it != cache_.end()) {
			return it->second.data != nullptr ? &it->second : nullptr;
		}
		wav_data wav;
		bool ok = false;
		if (const embedded_asset * em = find_asset(embedded, path)) {
			ok = SDL_LoadWAV_IO(SDL_IOFromConstMem(em->data, em->size), true, &wav.spec,
			                    &wav.data, &wav.len);
		}
		if (!ok && !SDL_LoadWAV(path.c_str(), &wav.spec, &wav.data, &wav.len)) {
			cache_.emplace(path, wav_data{}); // negative-cache the miss
			return nullptr;
		}
		return &cache_.emplace(path, wav).first->second;
	}

	// drop streams that finished playing
	void reap() {
		std::erase_if(streams_, [this](SDL_AudioStream * s) {
			if (SDL_GetAudioStreamQueued(s) > 0) { return false; }
			std::erase_if(live_, [s](const auto & kv) { return kv.second == s; });
			SDL_DestroyAudioStream(s);
			return true;
		});
	}

	std::map<std::string, wav_data> cache_;
	std::vector<SDL_AudioStream *> streams_;
	std::map<std::int32_t, SDL_AudioStream *> live_; // handle -> stream, for stop()
	std::int32_t next_id_ = 0;
	float volume_ = 1.0f;
};

#endif // CTBROWSER_WITH_MIXER

} // namespace ctbrowser

#endif
