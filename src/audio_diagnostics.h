#pragma once
#include <rex/input/input_driver.h>
#include <cstdint>
namespace outrun::diagnostics {
uint64_t AudioTraceTime();
void Input(const char* source,uint32_t user,const rex::input::X_INPUT_STATE& state);
void Key(uint16_t key,bool down,bool repeat);
void Sound(uint32_t event,uint32_t caller,const char* stage);
void Voice(uint32_t voice,uint32_t flags,uint32_t caller,double p1,double p2,double p3,double p4,double p5);
void Parameter(uint32_t index,uint32_t control,uint32_t value,uint32_t caller);
void LoopVoice(uint32_t group,uint32_t layer,uint32_t sample,int32_t result);
bool AudioTraceRecording();
bool AudioTransitionWindow();
void LoopTransition(const char* stage,uint32_t group,uint32_t layer,uint32_t object,int32_t handle,double left,double right);
void LoopMapping(uint32_t group,uint32_t layer,uint32_t sample,uint32_t voice);
}
