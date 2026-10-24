// Vector Strike - synthesized sound effects and a driving music loop, mixed in an SDL audio callback.
// If music.wav sits in the game folder it replaces the built-in loop.
#pragma once
#include <SDL.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include "gfx.h"

enum Snd { SND_LASER, SND_BOOM_S, SND_BOOM_L, SND_BALL, SND_BALLHIT, SND_SHIELD, SND_ALARM, SND_UI, SND_UI_OK,
           SND_PORT, SND_WAVE, SND_LOSE, SND_DOWN, SND_BANNER, SND_TOWER, SND_TIEFLY, SND_TIELASER, SND_N };
// file names for the optional sound pack: <game folder>/sounds/<name>.wav replaces the built-in sound
static const char* SND_NAMES[SND_N] = { "laser", "boom_small", "boom_big", "fireball", "fireball_hit", "shield_hit", "alarm", "ui", "ui_ok",
                                        "port_hit", "wave_clear", "game_over", "player_down", "banner", "tower_hit", "enemy_flyby", "enemy_laser" };

struct Audio {
    static const int RATE = 44100;
    SDL_AudioDeviceID dev = 0;
    std::vector<float> snd[SND_N];
    std::vector<float> music;
    struct Inst { int s; double pos; float rate, vol, pan; };
    std::vector<Inst> inst;
    float sfxVol = 0.8f, musVol = 0.5f;
    Uint32 lastPlay[SND_N] = {};
    bool musicOn = false; double mpos = 0; std::string musicSrc = "built-in";

    static float sq(float ph) { return std::sin(ph) >= 0 ? 1.f : -1.f; }
    std::vector<float> gen(float dur, float (*f)(float t, Rng& r, float& st)) {
        int n = (int)(dur * RATE); std::vector<float> v(n); Rng r; r.s = 99; float st = 0;
        for (int i = 0; i < n; i++) v[i] = f(i / (float)RATE, r, st);
        return v;
    }
    void build() {
        snd[SND_LASER] = gen(0.30f, [](float t, Rng& r, float& ph) {
            float f = 3400 * std::exp(-t * 26) + 180; ph += 2 * PI * f / RATE;            // falling "pew"
            float ring = std::sin(ph * 2.01f) * std::exp(-t * 9) * 0.35f;                     // metallic ring on top
            float click = t < 0.004f ? r.range(-1, 1) * 0.8f : 0;
            return (std::sin(ph) * 0.6f + 0.25f * sq(ph) + ring + click) * std::exp(-t * 11) * 0.34f; });
        // enemy fighter fly-by: a howl made of two detuned buzzy tones through a sweeping resonance, pitch falling as it passes
        snd[SND_TIEFLY] = gen(1.5f, [](float t, Rng& r, float& lp) {
            static float ph1, ph2, bp, bv; if (t == 0) { ph1 = ph2 = bp = bv = 0; }
            float pass = 1.0f / (1 + std::exp((t - 0.55f) * 9));                            // doppler: high -> low around 0.55 s
            float f = 210 + 260 * pass; ph1 += 2 * PI * f / RATE; ph2 += 2 * PI * f * 1.507f / RATE;
            float saw = (std::fmod(ph1 / (2 * PI), 1.f) * 2 - 1) * 0.6f + (std::fmod(ph2 / (2 * PI), 1.f) * 2 - 1) * 0.4f;
            float x = saw + r.range(-1, 1) * 0.5f;
            float fc = 900 + 1700 * pass, q = 0.08f, w = 2 * PI * fc / RATE;              // resonant band-pass (state variable)
            bp += w * bv; float hp = x - bp - q * bv * 2; bv += w * hp; lp += (bp - lp) * 0.5f;
            float growl = 0.75f + 0.25f * std::sin(2 * PI * 31 * t);
            float env = std::min(1.f, t * 4) * std::exp(-std::pow((t - 0.6f) / 0.5f, 2)) ;
            return lp * growl * env * 0.55f; });
        snd[SND_TIELASER] = gen(0.22f, [](float t, Rng&, float& ph) { float f = 1400 * std::exp(-t * 16) + 120; ph += 2 * PI * f / RATE; float saw = std::fmod(ph / (2 * PI), 1.f) * 2 - 1; return (saw * 0.6f + std::sin(ph * 0.5f) * 0.4f) * std::exp(-t * 13) * 0.3f; });
        snd[SND_BOOM_S] = gen(0.7f, [](float t, Rng& r, float& lp) { float k = 0.25f * std::exp(-t * 5) + 0.02f; lp += (r.range(-1, 1) - lp) * k; return lp * 2.2f * std::exp(-t * 6.0f) * 0.65f; });
        snd[SND_BOOM_L] = gen(1.8f, [](float t, Rng& r, float& lp) { float k = 0.12f * std::exp(-t * 3) + 0.01f; lp += (r.range(-1, 1) - lp) * k; float sub = std::sin(2 * PI * (55 - 15 * t) * t) * std::exp(-t * 2.5f); return (lp * 3.0f * std::exp(-t * 2.4f) + sub * 0.7f) * 0.6f; });
        snd[SND_BALL] = gen(0.4f, [](float t, Rng& r, float& lp) { float n = r.range(-1, 1); lp += (n - lp) * (0.05f + t * 0.5f); float tone = std::sin(2 * PI * (300 + 900 * t) * t); return (lp * 1.4f + tone * 0.18f) * std::min(1.f, t * 30) * std::exp(-t * 6) * 0.45f; });
        snd[SND_BALLHIT] = gen(0.25f, [](float t, Rng& r, float& lp) { float n = r.range(-1, 1); float hp = n - lp; lp = n; return (hp * 0.5f + (r.f() < 0.05f ? 0.6f : 0)) * std::exp(-t * 14) * 0.4f; });
        snd[SND_SHIELD] = gen(0.45f, [](float t, Rng& r, float& ph) { ph += 2 * PI * 92 / RATE; float saw = std::fmod(ph / (2 * PI), 1.f) * 2 - 1; return (saw * 0.6f + r.range(-1, 1) * 0.4f) * std::exp(-t * 5) * 0.45f; });
        snd[SND_ALARM] = gen(0.9f, [](float t, Rng&, float&) { float f = std::fmod(t, 0.3f) < 0.15f ? 760.f : 540.f; return (sq(2 * PI * f * t) * 0.3f + std::sin(2 * PI * f * t) * 0.7f) * 0.2f * std::min(1.f, (0.9f - t) * 8); });
        snd[SND_UI] = gen(0.04f, [](float t, Rng&, float&) { return std::sin(2 * PI * 1300 * t) * 0.22f * std::exp(-t * 70); });
        snd[SND_UI_OK] = gen(0.14f, [](float t, Rng&, float&) { float f = t < 0.05f ? 880.f : 1320.f; return std::sin(2 * PI * f * t) * 0.28f * std::exp(-std::fmod(t, 0.05f) * 25); });
        snd[SND_PORT] = gen(3.2f, [](float t, Rng& r, float& lp) { float k = 0.09f * std::exp(-t * 1.5f) + 0.006f; lp += (r.range(-1, 1) - lp) * k; float sub = std::sin(2 * PI * (48 - 10 * t) * t) * std::exp(-t * 1.2f); return (lp * 3.4f * std::exp(-t * 1.1f) + sub * 0.8f) * 0.65f; });
        snd[SND_WAVE] = gen(1.4f, [](float t, Rng&, float&) { const float nf[5] = { 523, 659, 784, 1046, 1318 }; int i = std::min(4, (int)(t / 0.14f)); float lt = t - i * 0.14f; float v = 0; for (int k = 0; k <= i; k++) v += std::sin(2 * PI * nf[k] * t) * std::exp(-(t - k * 0.14f) * 2.2f); return v * 0.12f * std::exp(-lt * 0.5f); });
        snd[SND_LOSE] = gen(1.6f, [](float t, Rng&, float& ph) { float f = 330 * std::exp(-t * 0.9f); ph += 2 * PI * f / RATE; return (std::sin(ph) + 0.3f * sq(ph)) * 0.2f * std::exp(-t * 1.6f); });
        snd[SND_DOWN] = gen(1.0f, [](float t, Rng& r, float& ph) { float f = 700 * std::exp(-t * 2.5f) + 60; ph += 2 * PI * f / RATE; return (sq(ph) * 0.5f + r.range(-1, 1) * 0.3f) * std::exp(-t * 2.5f) * 0.3f; });
        snd[SND_BANNER] = gen(0.45f, [](float t, Rng&, float& ph) { float f = 300 + 1500 * t / 0.45f; ph += 2 * PI * f / RATE; return std::sin(ph) * 0.22f * std::min(1.f, (0.45f - t) * 10); });
        snd[SND_TOWER] = gen(0.5f, [](float t, Rng& r, float& lp) { lp += (r.range(-1, 1) - lp) * 0.2f; return (lp * 1.5f + std::sin(2 * PI * 140 * t) * 0.4f) * std::exp(-t * 7) * 0.55f; });
        auto normTo = [&](int i, float pk) { float m = 0; for (float v : snd[i]) m = std::max(m, std::fabs(v)); if (m > 1e-6f) for (float& v : snd[i]) v *= pk / m; };
        normTo(SND_TIEFLY, 0.6f); normTo(SND_TIELASER, 0.42f);
        buildMusic();
    }
    // 32 s driving loop at 120 bpm: pulsing bass, minor pad, soft hats.
    void buildMusic() {
        const float secs = 32; int n = (int)(secs * RATE); music.assign(n * 2, 0.f);
        const float roots[8] = { 55.0f, 55.0f, 43.65f, 43.65f, 49.0f, 49.0f, 41.2f, 46.25f };
        const float pads[8][3] = { { 220, 261.63f, 329.63f }, { 220, 261.63f, 329.63f }, { 174.61f, 220, 261.63f }, { 174.61f, 220, 261.63f },
                                   { 196, 246.94f, 293.66f }, { 196, 246.94f, 293.66f }, { 164.81f, 196, 246.94f }, { 185, 233.08f, 277.18f } };
        Rng rg; rg.s = 777; float lp = 0, bph = 0;
        for (int i = 0; i < n; i++) {
            float t = i / (float)RATE; int bar = (int)(t / 2) % 8; float bt = std::fmod(t, 2.f);
            float e8 = std::fmod(t, 0.25f); int step = (int)(bt / 0.25f);
            float bf = roots[bar] * ((step == 3 || step == 7) ? 2.f : 1.f);
            bph += 2 * PI * bf / RATE; float saw = std::fmod(bph / (2 * PI), 1.f) * 2 - 1;
            lp += (saw - lp) * (0.05f + 0.25f * std::exp(-e8 * 18));
            float bass = lp * std::exp(-e8 * 6) * 0.55f;
            float pad = 0; for (int k = 0; k < 3; k++) { float f = pads[bar][k]; pad += std::sin(2 * PI * f * t) + 0.25f * std::sin(2 * PI * f * 2.004f * t); }
            float penv = std::min(1.f, bt * 2) * (0.75f + 0.25f * std::sin(t * 1.3f));
            pad *= 0.045f * penv;
            float hat = 0; float e16 = std::fmod(t, 0.125f); if (e16 < 0.03f) hat = rg.range(-1, 1) * std::exp(-e16 * 160) * ((int)(t / 0.125f) % 2 ? 0.05f : 0.09f);
            float kick = 0; float eq = std::fmod(t, 0.5f); kick = std::sin(2 * PI * (110 * std::exp(-eq * 30) + 45) * eq) * std::exp(-eq * 14) * 0.5f;
            float v = bass + kick * (t > 4 ? 1 : 0.3f);
            music[i * 2] = v + pad * 1.1f + hat * 0.7f;
            music[i * 2 + 1] = v + pad * 0.9f + hat * 1.2f;
        }
        float pk = 0; for (float v : music) pk = std::max(pk, std::fabs(v));
        if (pk > 0.6f) for (float& v : music) v *= 0.6f / pk;
    }
    bool loadMusicWav(const std::string& path) {
        SDL_AudioSpec spec; Uint8* buf = nullptr; Uint32 blen = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &blen)) return false;
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 2, RATE) < 0) { SDL_FreeWAV(buf); return false; }
        cvt.len = (int)blen; std::vector<Uint8> tmp((size_t)blen * (cvt.len_mult > 0 ? cvt.len_mult : 1)); memcpy(tmp.data(), buf, blen); cvt.buf = tmp.data();
        SDL_FreeWAV(buf);
        if (SDL_ConvertAudio(&cvt) < 0) return false;
        int nf = cvt.len_cvt / 4; if (nf < RATE) return false;
        music.assign((float*)tmp.data(), (float*)tmp.data() + nf); musicSrc = path;
        return true;
    }
    void exportWavs(const std::string& dir) {   // VS_EXPORT_SOUNDS: write every built-in sound (16-bit mono)
        for (int i = 0; i < SND_N; i++) {
            std::string path = dir + "/" + SND_NAMES[i] + ".wav"; FILE* f = fopen(path.c_str(), "wb"); if (!f) continue;
            uint32_t n = (uint32_t)snd[i].size(), data = n * 2, riff = 36 + data, sr = RATE, br = RATE * 2, fmt = 16; uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
            fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
            fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
            for (float v : snd[i]) { int16_t q = (int16_t)clampf(v * 32767, -32767, 32767); fwrite(&q, 2, 1, f); }
            fclose(f);
        }
        printf("audio: exported %d sounds to %s\n", (int)SND_N, dir.c_str());
    }
    int loadSoundPack(const std::string& dir) {
        int n = 0;
        for (int i = 0; i < SND_N; i++) {
            std::string path = dir + SND_NAMES[i] + ".wav";
            SDL_AudioSpec spec; Uint8* buf = nullptr; Uint32 blen = 0;
            if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &blen)) continue;
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, RATE) < 0) { SDL_FreeWAV(buf); continue; }
            cvt.len = (int)blen; std::vector<Uint8> tmp((size_t)blen * (cvt.len_mult > 0 ? cvt.len_mult : 1)); memcpy(tmp.data(), buf, blen); cvt.buf = tmp.data();
            SDL_FreeWAV(buf);
            if (SDL_ConvertAudio(&cvt) < 0) continue;
            int nf = cvt.len_cvt / 4; if (nf < 16) continue;
            snd[i].assign((float*)tmp.data(), (float*)tmp.data() + nf); n++;
            printf("audio: %s from %s\n", SND_NAMES[i], path.c_str());
        }
        return n;
    }
    static void cb(void* ud, Uint8* stream, int bytes) {
        Audio* a = (Audio*)ud; float* out = (float*)stream; int frames = bytes / 8;
        for (int i = 0; i < frames * 2; i++) out[i] = 0;
        if (a->musicOn && a->music.size() > 2) {
            int mn = (int)a->music.size() / 2;
            for (int i = 0; i < frames; i++) { int p = (int)a->mpos; out[i * 2] += a->music[p * 2] * a->musVol; out[i * 2 + 1] += a->music[p * 2 + 1] * a->musVol; a->mpos += 1; if (a->mpos >= mn) a->mpos -= mn; }
        }
        for (size_t k = 0; k < a->inst.size(); ) {
            Inst& in = a->inst[k]; const std::vector<float>& s = a->snd[in.s];
            float vl = in.vol * a->sfxVol * (1 - std::max(0.f, in.pan)), vr = in.vol * a->sfxVol * (1 + std::min(0.f, in.pan));
            bool done = false;
            for (int i = 0; i < frames; i++) {
                int p = (int)in.pos; if (p >= (int)s.size()) { done = true; break; }
                float v = s[p]; out[i * 2] += v * vl; out[i * 2 + 1] += v * vr; in.pos += in.rate;
            }
            if (done) { a->inst[k] = a->inst.back(); a->inst.pop_back(); } else k++;
        }
        for (int i = 0; i < frames * 2; i++) { float v = out[i]; out[i] = v / (1 + std::fabs(v) * 0.6f); }
    }
    bool open(const std::string& dir) {
        build();
        if (loadMusicWav(dir + "music.wav")) printf("audio: music from %s\n", musicSrc.c_str());
        loadSoundPack(dir + "sounds/");
        SDL_AudioSpec want = {}, have = {};
        want.freq = RATE; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024; want.callback = cb; want.userdata = this;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev) { printf("audio: open failed: %s\n", SDL_GetError()); return false; }
        printf("audio: %d Hz, %d ch, music %s\n", have.freq, have.channels, musicSrc.c_str());
        musicOn = true;
        SDL_PauseAudioDevice(dev, 0);
        return true;
    }
    void play(int s, float vol = 1, float pan = 0, float pitch = 1, Uint32 minGapMs = 30) {
        if (!dev || vol <= 0.01f) return;
        Uint32 now = SDL_GetTicks(); if (now - lastPlay[s] < minGapMs) return; lastPlay[s] = now;
        SDL_LockAudioDevice(dev);
        if (inst.size() < 48) inst.push_back({ s, 0.0, pitch, vol, clampf(pan, -1, 1) });
        SDL_UnlockAudioDevice(dev);
    }
    void close() { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }
};
