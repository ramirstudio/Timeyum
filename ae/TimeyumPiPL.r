#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
    #include "AE_General.r"
#endif

#include "TimeyumFlags.h"

resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { TY_NAME },
        Category { TY_CATEGORY },
#ifdef AE_OS_WIN
    #ifdef AE_PROC_INTELx64
        CodeWin64X86 { "EffectMain" },
    #endif
#else
    #ifdef AE_PROC_INTELx64
        CodeMacIntel64 { "EffectMain" },
    #endif
    #ifdef AE_PROC_ARM64
        CodeMacARM64 { "EffectMain" },
    #endif
#endif
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        AE_Effect_Version { TY_VERSION_VALUE },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { TY_OUT_FLAGS },
        AE_Effect_Global_OutFlags_2 { TY_OUT_FLAGS2 },
        AE_Effect_Match_Name { TY_MATCH_NAME },
        AE_Reserved_Info { 0 },
        AE_Effect_Support_URL { "https://github.com/ramirstudio/Timeyum" }
    }
};
