#include "Audio.hpp"
#include "Memory.hpp"
#include "Common.hpp"

#include <SDL.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <atomic>

constexpr uint32_t HALF_CLOCK_RATE = CLOCK_RATE / 2;
constexpr uint32_t OUTPUT_FREQ = 65536;
constexpr uint32_t CLOCKS_PER_SAMPLE = HALF_CLOCK_RATE / OUTPUT_FREQ;
constexpr uint32_t SEQUENCER_FREQ = 512;
constexpr uint32_t C1_C2_FREQ = 131072;
constexpr uint32_t C3_FREQ = 65536;
constexpr uint32_t C4_FREQ = 524288;

static const int SQUARE_WAVE_PATTERNS[4][8] = 
{
	{ -1, 1, 1, 1, 1, 1, 1, 1 },
	{ -1, -1, 1, 1, 1, 1, 1, 1 },
	{ -1, -1, -1, -1, 1, 1, 1, 1 },
	{ -1, -1, -1, -1, -1, -1, 1, 1 }
};

bool audioActive = false;
int audioDeviceId;

AudioRegisterState audioReg;

static constexpr size_t REG_QUEUE_LEN = 32768;
static AudioRegisterState regStateQueue[REG_QUEUE_LEN];
static std::atomic_uint32_t regStateQueueFront;
static std::atomic_uint32_t regStateQueueBack;

inline const AudioRegisterState& PopRegisterState()
{
	static AudioRegisterState regState;
	
	uint32_t front = regStateQueueFront.load(std::memory_order_acquire);
	uint32_t back = regStateQueueBack.load(std::memory_order_relaxed);
	
	if (back != front)
	{
		regState = regStateQueue[back];
		regStateQueueBack.store((back + 1) % REG_QUEUE_LEN, std::memory_order_release);
	}
	
	return regState;
}

struct ChannelData
{
	uint32_t volSweepTimer = 0;
	uint32_t timer = 0;
	uint32_t pos = 0;
	uint32_t lengthCounter = 0;
};

ChannelData channel1;
ChannelData channel2;
ChannelData channel3;
ChannelData channel4;
ChannelData* channels[] = { nullptr, &channel1, &channel2, &channel3, &channel4 };

uint32_t channel1FreqSweepSteps = 0;

uint32_t seqStep;
int seqTimer;

void SetAudioChannelLen(int channel, uint32_t length)
{
	channels[channel]->lengthCounter = (channel == 3 ? 256 : 64) - length;
}

uint32_t GetChannelFrequency(uint8_t regLo, uint8_t regHi)
{
	return (uint32_t)regLo | ((uint32_t)(regHi & 7) << 8);
}

constexpr uint8_t CPAN_4L = 1 << 7;
constexpr uint8_t CPAN_3L = 1 << 6;
constexpr uint8_t CPAN_2L = 1 << 5;
constexpr uint8_t CPAN_1L = 1 << 4;
constexpr uint8_t CPAN_4R = 1 << 3;
constexpr uint8_t CPAN_3R = 1 << 2;
constexpr uint8_t CPAN_2R = 1 << 1;
constexpr uint8_t CPAN_1R = 1 << 0;
constexpr uint8_t NRX4_RESET = 1 << 7;
constexpr uint8_t NRX4_ENABLE_LC = 1 << 6;

inline void UpdateChannelElapsed(ChannelData& channel, uint8_t enableReg, int channelIdx)
{
	if (channel.lengthCounter > 0 && (enableReg & NRX4_ENABLE_LC))
	{
		channel.lengthCounter--;
		if (channel.lengthCounter == 0)
		{
			audioReg.NR52 &= ~(uint8_t)(1 << channelIdx);
		}
	}
}

inline void UpdateChannelVolume(ChannelData& channel, uint8_t& volume, uint8_t reg)
{
	channel.volSweepTimer++;
	uint32_t sweepTime = reg & 7;
	if (channel.volSweepTimer >= sweepTime && sweepTime != 0)
	{
		channel.volSweepTimer = 0;
		
		bool subtract = reg & (1 << 3);
		if (subtract && volume > 0)
			volume--;
		else if (!subtract && volume < 15)
			volume++;
	}
}

uint16_t channel4LFSR = 0;

std::pair<double, double> GenerateClockSample(const AudioRegisterState& reg)
{
	if (!(reg.NR52 & (1 << 7)) || audioDeviceId == 0)
	{
		channel1.pos = 0;
		channel2.pos = 0;
		channel3.pos = 0;
		return { 0.0, 0.0 };
	}
	
	if (reg.NR14 & NRX4_RESET)
	{
		channel1.timer = 1;
		channel1.pos = 0;
	}
	if (reg.NR24 & NRX4_RESET)
	{
		channel2.timer = 1;
		channel2.pos = 0;
	}
	if (reg.NR34 & NRX4_RESET)
	{
		channel3.timer = 1;
		channel3.pos = 0;
	}
	if (reg.NR44 & NRX4_RESET)
	{
		channel4.timer = 1;
		channel4LFSR = 0x7fff;
	}
	
	uint8_t channelPan = reg.NR51;
	
	double volL = (double)((reg.NR50 >> 4) & 7) / 7.0;
	double volR = (double)(reg.NR50 & 7) / 7.0;
	
	double sampleL = 0;
	double sampleR = 0;
	
	constexpr double MAX_PER_CHANNEL = 127.0 / 4.0;
	
	//Channel 1
	if ((reg.NR52 & 1) && reg.channel1Volume > 0)
	{
		const uint32_t c1Pattern = reg.NR11 >> 6;
		const double c1Vol =
			SQUARE_WAVE_PATTERNS[c1Pattern][channel1.pos] *
			(int)reg.channel1Volume *
			(MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		
		if (channelPan & CPAN_1L)
			sampleL += c1Vol * volL;
		if (channelPan & CPAN_1R)
			sampleR += c1Vol * volR;
	}
	
	//Channel 2
	if ((reg.NR52 & 2) && reg.channel2Volume > 0)
	{
		const uint32_t c2Pattern = reg.NR21 >> 6;
		const double c2Vol =
			SQUARE_WAVE_PATTERNS[c2Pattern][channel2.pos] *
			(int)reg.channel2Volume *
			(MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		
		if (channelPan & CPAN_2L)
			sampleL += c2Vol * volL;
		if (channelPan & CPAN_2R)
			sampleR += c2Vol * volR;
	}
	
	//Channel 3
	if ((reg.NR52 & 4) && false)
	{
		uint32_t c3Volume = (reg.NR32 >> 5) & 3;
		if (c3Volume != 0)
		{
			uint8_t c3Sample = (reg.waveMem[channel3.pos / 2] >> ((channel3.pos % 2) ? 0 : 4)) & 0xF;
			c3Sample >>= (c3Volume - 1);
			if (channelPan & CPAN_3L)
				sampleL += c3Sample * volL * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - MAX_PER_CHANNEL / 2;
			if (channelPan & CPAN_3R)
				sampleR += c3Sample * volR * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - MAX_PER_CHANNEL / 2;
		}
	}
	
	//Channel 4
	if ((reg.NR52 & 8) && reg.channel4Volume > 0 && false) //Channel disabled because it doesn't work
	{
		const double c4Vol =
			(1 - 2 * (int)(channel4LFSR & 1)) *
			(int)reg.channel4Volume *
			(MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		
		if (channelPan & CPAN_4L)
			sampleL += c4Vol * volL;
		if (channelPan & CPAN_4R)
			sampleR += c4Vol * volR;
	}
	
	if (channel1.timer-- == 0)
	{
		channel1.timer = (HALF_CLOCK_RATE / (C1_C2_FREQ * 8)) * (2048 - reg.channel1Freq);
		channel1.pos = (channel1.pos + 1) % 8;
	}
	
	if (channel2.timer-- == 0)
	{
		uint32_t freq = GetChannelFrequency(reg.NR23, reg.NR24);
		channel2.timer = (HALF_CLOCK_RATE / (C1_C2_FREQ * 8)) * (2048 - freq);
		channel2.pos = (channel2.pos + 1) % 8;
	}
	
	if (channel3.timer-- == 0)
	{
		uint32_t freq = GetChannelFrequency(reg.NR33, reg.NR34);
		channel3.timer = (HALF_CLOCK_RATE / (C3_FREQ * 32)) * (2048 - freq);
		channel3.pos = (channel3.pos + 1) % 32;
	}
	
	if (channel4.timer-- == 0)
	{
		uint16_t nextLFSR = channel4LFSR >> 1;
		uint16_t x = (channel4LFSR & 1) ^ (nextLFSR & 1);
		if (reg.NR43 & 8)
			nextLFSR = (nextLFSR & 0x3F) | (x << 6);
		nextLFSR |= x << 14;
		channel4LFSR = nextLFSR;
		
		channel4.timer = ((HALF_CLOCK_RATE / C4_FREQ) * (uint32_t)(reg.NR43 & 7)) >> ((reg.NR43 >> 4) + 1);
	}
	
	return std::make_pair(sampleL, sampleR);
}

inline int8_t SampleToU8(double sample)
{
	return std::max(std::min((int)std::round(sample), 127), -127);
}

void AudioCallback(void* userData, uint8_t* stream, int len)
{
	for (int s = 0; s < len; s += 2)
	{
		double sampleL = 0;
		double sampleR = 0;
		
		for (size_t c = 0; c < CLOCKS_PER_SAMPLE; c++)
		{
			auto [genL, genR] = GenerateClockSample(PopRegisterState());
			sampleL += genL;
			sampleR += genR;
		}
		
		stream[s + 0] = SampleToU8(sampleL);
		stream[s + 1] = SampleToU8(sampleR);
	}
}

void InitAudio()
{
	SDL_AudioSpec audioSpec = { };
	audioSpec.freq = OUTPUT_FREQ;
	audioSpec.callback = AudioCallback;
	audioSpec.channels = 2;
	audioSpec.samples = 4096;
	audioSpec.format = AUDIO_S8;
	
	SDL_AudioSpec realAudioSpec;
	audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &audioSpec, &realAudioSpec, 0);
	if (audioDeviceId == 0 || realAudioSpec.freq != OUTPUT_FREQ)
	{
		std::cout << SDL_GetError() << std::endl;
		return;
	}
	
	SDL_PauseAudioDevice(audioDeviceId, 0);
}

void UpdateAudio()
{
	if (!(audioReg.NR52 & (1 << 7)))
	{
		memset(&audioReg, 0, offsetof(AudioRegisterState, waveMem));
		seqStep = 0;
		seqTimer = 0;
	}
	else
	{
		//Extra length clocking
		static bool lengthClockWasEnabled[4];
		bool lengthClockIsEnabled[4] =
		{
			(bool)(audioReg.NR14 & NRX4_ENABLE_LC),
			(bool)(audioReg.NR24 & NRX4_ENABLE_LC),
			(bool)(audioReg.NR34 & NRX4_ENABLE_LC),
			(bool)(audioReg.NR44 & NRX4_ENABLE_LC)
		};
		for (int i = 0; i < 4; i++)
		{
			if (!lengthClockWasEnabled[i] && lengthClockIsEnabled[i] && (seqStep % 2) != 0)
			{
				UpdateChannelElapsed(*channels[i + 1], NRX4_ENABLE_LC, i);
			}
			lengthClockWasEnabled[i] = lengthClockIsEnabled[i];
		}
		
		if (audioReg.NR14 & NRX4_RESET)
		{
			//Reset channel 1
			channel1FreqSweepSteps = 0;
			channel1.volSweepTimer = 0;
			if (channel1.lengthCounter == 0)
				channel1.lengthCounter = 64 - ((seqStep % 2) && (audioReg.NR14 & NRX4_ENABLE_LC));
			audioReg.channel1Volume = audioReg.NR12 >> 4;
			audioReg.channel1Freq = GetChannelFrequency(audioReg.NR13, audioReg.NR14);
			audioReg.NR52 |= 1 << 0;
		}
		
		if (audioReg.NR24 & NRX4_RESET)
		{
			//Reset channel 2
			channel2.volSweepTimer = 0;
			if (channel2.lengthCounter == 0)
				channel2.lengthCounter = 64 - ((seqStep % 2) && (audioReg.NR24 & NRX4_ENABLE_LC));
			audioReg.channel2Volume = audioReg.NR22 >> 4;
			audioReg.NR52 |= 1 << 1;
		}
		
		if (audioReg.NR34 & NRX4_RESET)
		{
			//Reset channel 3
			if (channel3.lengthCounter == 0)
				channel3.lengthCounter = 256 - ((seqStep % 2) && (audioReg.NR34 & NRX4_ENABLE_LC));
			audioReg.NR52 |= 1 << 2;
		}
		
		if (audioReg.NR44 & NRX4_RESET)
		{
			//Reset channel 4
			channel4.volSweepTimer = 0;
			if (channel4.lengthCounter == 0)
				channel4.lengthCounter = 64 - ((seqStep % 2) && (audioReg.NR44 & NRX4_ENABLE_LC));
			audioReg.channel4Volume = audioReg.NR42 >> 4;
			audioReg.NR52 |= 1 << 3;
		}
		
		if (!(audioReg.NR12 & 0xF8))
			audioReg.NR52 &= ~1;
		if (!(audioReg.NR22 & 0xF8))
			audioReg.NR52 &= ~2;
		if (!(audioReg.NR30 & (1 << 7)))
			audioReg.NR52 &= ~4;
		if (!(audioReg.NR42 & 0xF8))
			audioReg.NR52 &= ~8;
	}
	
	if (--seqTimer <= 0)
	{
		seqTimer = HALF_CLOCK_RATE / SEQUENCER_FREQ;
		
		if (seqStep == 2 || seqStep == 6)
		{
			uint32_t sweepTime = (audioReg.NR10 >> 4) & 7;
			uint32_t shift = audioReg.NR10 & 7;
			if (sweepTime != 0)
			{
				if (channel1FreqSweepSteps++ == sweepTime)
				{
					channel1FreqSweepSteps = 0;
					int deltaFreq = audioReg.channel1Freq >> shift;
					audioReg.channel1Freq = std::max(std::min((int)audioReg.channel1Freq + ((audioReg.NR10 & (1 << 3)) ? -deltaFreq : deltaFreq), 2048), 0);
					if (audioReg.channel1Freq >= 2048)
					{
						audioReg.NR52 &= ~(uint8_t)1;
					}
				}
			}
		}
		
		if ((seqStep % 2) == 0)
		{
			UpdateChannelElapsed(channel1, audioReg.NR14, 0);
			UpdateChannelElapsed(channel2, audioReg.NR24, 1);
			UpdateChannelElapsed(channel3, audioReg.NR34, 2);
			UpdateChannelElapsed(channel4, audioReg.NR44, 3);
		}
		
		if (seqStep == 7)
		{
			UpdateChannelVolume(channel1, audioReg.channel1Volume, audioReg.NR12);
			UpdateChannelVolume(channel2, audioReg.channel2Volume, audioReg.NR22);
			UpdateChannelVolume(channel4, audioReg.channel4Volume, audioReg.NR42);
		}
		
		seqStep = (seqStep + 1) % 8;
	}
	
	uint32_t queueBack = regStateQueueBack.load(std::memory_order_acquire);
	uint32_t queueFront = regStateQueueFront.load(std::memory_order_relaxed);
	uint32_t nextQueueFront = (queueFront + 1) % REG_QUEUE_LEN;
	if (nextQueueFront != queueBack)
	{
		regStateQueue[queueFront] = audioReg;
		regStateQueueFront.store(nextQueueFront, std::memory_order_release);
	}
	
	audioReg.NR14 &= ~NRX4_RESET;
	audioReg.NR24 &= ~NRX4_RESET;
	audioReg.NR34 &= ~NRX4_RESET;
	audioReg.NR44 &= ~NRX4_RESET;
}
