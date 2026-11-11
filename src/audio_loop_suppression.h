#pragma once
#include <string>
struct PPCContext;
namespace outrun::audio {
bool SuppressSteeringLoopStart(PPCContext& ctx);
// Whether the steering loop sounds are blocked (outrun_audio_steering_loops), and how that was decided.
bool SteeringLoopsBlocked();
std::string SteeringLoopsSetting();
}
