#pragma once

/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/engine-globals.h>
#include <imagine/util/audio/PcmFormat.hh>

#if defined CONFIG_AUDIO_ALSA
#include <imagine/audio/alsa/config.hh>
#else
#include <imagine/audio/config.hh>
#endif

namespace Audio
{

	namespace Config
	{
	#define CONFIG_AUDIO_LATENCY_HINT

	#if defined CONFIG_AUDIO_OPENSL_ES || defined CONFIG_AUDIO_COREAUDIO
	#define CONFIG_AUDIO_SOLO_MIX
	#endif
	}

struct BufferContext
{
	void *data = nullptr;
	uframes frames = 0;

	constexpr BufferContext() {}
	constexpr BufferContext(void *data, uframes frames): data{data}, frames{frames} {}

	operator bool() const
	{
		return data;
	}
};

extern PcmFormat preferredPcmFormat;
extern PcmFormat pcmFormat; // the currently playing format

[[gnu::cold]] CallResult init();
CallResult openPcm(const PcmFormat &format);
void closePcm();
void pausePcm();
void resumePcm();
void clearPcm();
bool isOpen();
bool isPlaying();
void writePcm(const void *samples, uint framesToWrite);
BufferContext getPlayBuffer(uint wantedFrames);
void commitPlayBuffer(BufferContext buffer, uint frames);
int frameDelay();
int framesFree();
void setHintOutputLatency(uint us);
uint hintOutputLatency();
void setHintStrictUnderrunCheck(bool on);
bool hintStrictUnderrunCheck();
int maxRate();

#ifdef CONFIG_AUDIO_SOLO_MIX
void setSoloMix(bool newSoloMix);
bool soloMix();
#else
static void setSoloMix(bool newSoloMix) {}
static bool soloMix() { return 0; }
#endif

// shortcuts
static PcmFormat &pPCM = preferredPcmFormat;

static CallResult openPcm(int rate) { return openPcm({ rate, pPCM.sample, pPCM.channels }); }
static CallResult openPcm(int rate, int channels) { return openPcm({ rate, pPCM.sample, channels }); }
static CallResult openPcm() { return openPcm(pPCM); }
static bool supportsRateNative(int rate) { return rate <= pPCM.rate; }
}
