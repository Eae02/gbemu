#pragma once

#include <mutex>
#include <cstdint>

void InitAudio();

void ResetAudioChannel1();
void ResetAudioChannel2();
void ResetAudioChannel3();

void SetAudioChannelLen(int channel, uint32_t length);

uint32_t GetChannelFrequency(uint8_t regLo, uint8_t regHi);

struct AudioRegisterState
{
	uint8_t NR10 = 0x80;
	uint8_t NR11 = 0xBF;
	uint8_t NR12 = 0xF3;
	uint8_t NR13 = 0x00;
	uint8_t NR14 = 0xBF;
	uint8_t NR21 = 0x3F;
	uint8_t NR22 = 0x00;
	uint8_t NR23 = 0x00;
	uint8_t NR24 = 0xBF;
	uint8_t NR30 = 0x7F;
	uint8_t NR31 = 0xFF;
	uint8_t NR32 = 0x9F;
	uint8_t NR33 = 0xBF;
	uint8_t NR34 = 0x00;
	uint8_t NR41 = 0xFF;
	uint8_t NR42 = 0x00;
	uint8_t NR43 = 0x00;
	uint8_t NR44 = 0xBF;
	uint8_t NR50 = 0x77;
	uint8_t NR51 = 0xF3;
	uint8_t NR52 = 0xF1;
	uint8_t waveMem[16];
	uint8_t channel1Volume;
	uint8_t channel2Volume;
	uint8_t channel4Volume;
	uint32_t channel1Freq;
};

extern AudioRegisterState audioReg;

void UpdateAudio();
