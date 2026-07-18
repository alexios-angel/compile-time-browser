#ifndef CTBROWSER__AUDIO__HPP
#define CTBROWSER__AUDIO__HPP

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

	bool play(const std::string & path) {
		if (!ready()) { return false; }
		MIX_Audio * audio = load(path);
		if (audio == nullptr) { return false; }
		MIX_Track * track = idle_track();
		if (track == nullptr) { return false; }
		return MIX_SetTrackAudio(track, audio) && MIX_PlayTrack(track, 0);
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
		MIX_Audio * a = MIX_LoadAudio(mixer_, path.c_str(), true);
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
	audio_mixer() = default;
	audio_mixer(const audio_mixer &) = delete;
	audio_mixer & operator=(const audio_mixer &) = delete;

	~audio_mixer() {
		for (SDL_AudioStream * s : streams_) { SDL_DestroyAudioStream(s); }
		for (auto & [path, wav] : cache_) { SDL_free(wav.data); }
	}

	void set_volume(float v) { volume_ = v < 0 ? 0.0f : v > 1 ? 1.0f : v; }

	// load (cached) and play a WAV; false if the file will not load
	bool play(const std::string & path) {
		if (!SDL_WasInit(SDL_INIT_AUDIO)) {
			if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { return false; }
		}
		wav_data * wav = load(path);
		if (wav == nullptr) { return false; }
		SDL_AudioStream * stream = SDL_OpenAudioDeviceStream(
		    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav->spec, nullptr, nullptr);
		if (stream == nullptr) { return false; }
		SDL_SetAudioStreamGain(stream, volume_);
		SDL_PutAudioStreamData(stream, wav->data, static_cast<int>(wav->len));
		SDL_FlushAudioStream(stream);
		SDL_ResumeAudioStreamDevice(stream);
		streams_.push_back(stream);
		reap();
		return true;
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
		if (!SDL_LoadWAV(path.c_str(), &wav.spec, &wav.data, &wav.len)) {
			cache_.emplace(path, wav_data{}); // negative-cache the miss
			return nullptr;
		}
		return &cache_.emplace(path, wav).first->second;
	}

	// drop streams that finished playing
	void reap() {
		std::vector<SDL_AudioStream *> live;
		for (SDL_AudioStream * s : streams_) {
			if (SDL_GetAudioStreamQueued(s) > 0) {
				live.push_back(s);
			} else {
				SDL_DestroyAudioStream(s);
			}
		}
		streams_ = std::move(live);
	}

	std::map<std::string, wav_data> cache_;
	std::vector<SDL_AudioStream *> streams_;
	float volume_ = 1.0f;
};

#endif // CTBROWSER_WITH_MIXER

} // namespace ctbrowser

#endif
