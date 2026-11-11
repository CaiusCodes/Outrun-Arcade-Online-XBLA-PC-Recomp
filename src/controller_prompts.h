#pragma once
#include <cstdint>
struct PPCContext;
namespace outrun::prompts {
void Refresh(PPCContext& ctx, uint8_t* base);
}
