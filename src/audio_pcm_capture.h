#pragma once
#include <cstdint>
namespace outrun::diagnostics {
void ArmPCM(uint16_t key,bool menu,bool title);
void ArmPCMRelease(uint16_t key,bool menu,bool title);
void CapturePCM(uint32_t driver,uint32_t address,const uint8_t* base);
void SavePCM();
uint64_t AudioTraceTime();
}
