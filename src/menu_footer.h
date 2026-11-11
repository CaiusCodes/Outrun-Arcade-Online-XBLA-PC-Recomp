#pragma once
// Where the menus' footer prompts sit (Back, Select, Color..., and the
// popups' Cancel/Accept), in game picture coordinates (1280x720): the label
// and its key cap or button, measured from native frames. Each is clickable
// (menu_pointer.cpp) and gets the hover highlight (menu_affordances.cpp).
#include <atomic>
#include <cstdint>
#include <vector>
namespace outrun::pointer {
struct FooterButton {
  float left,top,right,bottom;
  uint32_t action;  // the pad button a click presses
  bool Contains(float x,float y) const {return x>=left-4 && x<=right+4 && y>=top-4 && y<=bottom+4;}
};
// The card menus' footer (menu_manager widgets): Back on the left, Select on
// the right, and under Select the X prompt (Color on car select) or Y.
inline constexpr FooterButton kCardBack{229,610,337,638,0x200};
// The main menu's B prompt reads "Exit Game" (exit_game.cpp), wider than Back.
inline constexpr FooterButton kMainMenuExit{229,610,378,638,0x200};
inline constexpr FooterButton kCardSelect{925,610,1050,638,0x100};
inline constexpr FooterButton kCardLower{912,639,1023,665,0x400};
// The pause popup, measured from where its rows end.
inline FooterButton PauseBack(float footerTop){return {256,footerTop+3,360,footerTop+31,0x200};}
inline FooterButton PauseSelect(float footerTop){return {880,footerTop+3,998,footerTop+31,0x100};}
// The options popup (Settings, Graphics, Audio Options): Cancel and Accept.
inline FooterButton PopupCancel(float footerTop){return {256,footerTop+8,378,footerTop+36,0x200};}
inline FooterButton PopupAccept(float footerTop){return {873,footerTop+8,998,footerTop+36,0x100};}
// The hover highlight is off for now (user's choice, 6 Nov): the prompts stay
// clickable with the hand cursor. Set true to bring it back.
inline constexpr bool kFooterHighlight=false;
// The prompt under the mouse, for the native renderer's output pass to fill
// behind the game's lettering (canvas coordinates, padding included); shown
// while `until` (GetTickCount64 ms) is ahead.
struct Highlight {float left=0,top=0,right=0,bottom=0;uint64_t until=0;};
inline std::atomic<Highlight> highlight{};
// The footer on screen now, as the menu polling input last laid it out
// (empty once no menu has polled for a moment).
std::vector<FooterButton> Footer();
}
