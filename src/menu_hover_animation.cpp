#include "generated/default/outrun_init.h"
#include "menu_pointer.h"
#include <rex/hook.h>

REX_HOOK_RAW(sub_8223C548) {
  const auto vm=ctx.r3.u32,call=ctx.r4.u32;
  // Native20 starts the original animation, then yields if argument5 is true.
  // Hover is selection-only: allow successive card selections without waiting
  // for every intermediate card's animation. Keep playback and native selection.
  if(REX_LOAD_U32(call+16)==6 && outrun::pointer::FastHoverAnimation(vm,base)) {
    const auto args=REX_LOAD_U32(call+12);
    const auto wait=REX_LOAD_U32(args+20);
    REX_STORE_U32(args+20,0);
    __imp__sub_8223C548(ctx,base);
    REX_STORE_U32(args+20,wait);
    return;
  }
  __imp__sub_8223C548(ctx,base);
}
