#pragma once

#include <mutex>
#include <cstdint>

void InitAudio();

void ResetAudioChannel(int channel);

void SetAudioFrequency(int channel, uint32_t freq);
void SetAudioChannelLen(int channel, uint32_t length);
void SetAudioVolume(int channel, uint32_t vol);

uint8_t GetRegisterNR52();

void UpdateAudio();
