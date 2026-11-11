// outrun - ReXGlue Recompiled Project

#include "generated/default/outrun_init.h"

#include "portable_app.h"
REXCVAR_DEFINE_BOOL(outrun_runtime_shortcuts,false,"OutRun/Diagnostics",
                   "Enable ReXGlue developer keyboard shortcuts")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_DEFINE_APP(outrun, OutrunPortableApp::Create)
