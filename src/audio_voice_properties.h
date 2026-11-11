#pragma once
#include <bit>
#include <cstdint>
#include <optional>
namespace outrun::diagnostics {
struct VoiceGain {uint32_t target=0,leftBits=0,rightBits=0;};
inline std::optional<VoiceGain> ReadVoiceGain(const uint8_t* base,uint32_t voice,uint32_t root,uint64_t size=0x100000000ull){
  const auto valid=[&](uint32_t p,uint32_t bytes){return p && uint64_t(p)+bytes<=size;};
  const auto read=[&](uint32_t p){return uint32_t(base[p])<<24 | uint32_t(base[p+1])<<16 | uint32_t(base[p+2])<<8 | base[p+3];};
  // Exact list constructed by82118140: one volume property, two channels.
  if(!base || !valid(root,8) || base[root]!=1)return {};
  const auto item=read(root+4);
  if(!valid(item,8) || base[item]!=0)return {};
  const auto channels=read(item+4);
  if(!valid(channels,8) || base[channels]!=2)return {};
  const auto entries=read(channels+4);
  if(!valid(entries,16) || base[entries]!=0 || base[entries+8]!=1 || !valid(voice,4))return {};
  const auto table=read(voice);
  if(!valid(table,44))return {};
  return VoiceGain{read(table+40),read(entries+4),read(entries+12)};
}
}
