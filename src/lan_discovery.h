#pragma once
#include "local_session_search.h"
#include <string_view>
namespace outrun::local_sessions {
struct DiscoveryRoute {uint32_t source=0,destination=0;bool broadcast=false;};
struct NetworkInterface {uint32_t address=0,broadcast=0;std::string label;};
std::vector<NetworkInterface> LocalInterfaces();
std::vector<DiscoveryRoute> BuildDiscoveryRoutes(std::string_view sourceOverride,std::string_view hostOverride);
std::vector<Match> DiscoverRoutes(std::span<const DiscoveryRoute> routes,size_t maximum,uint32_t timeoutMs=900);
std::vector<Match> DiscoverLobbies(std::string_view sourceOverride,std::string_view hostOverride,size_t maximum);
}
