#include <unwind.h>
#include <stdio.h>

_Unwind_Reason_Code
__gcc_personality_v0(int version, _Unwind_Action actions,
                     uint64_t exception_class,
                     struct _Unwind_Exception *ue_header,
                     struct _Unwind_Context *context);

_Unwind_Reason_Code
__cilk_personality_internal(__personality_routine std_lib_personality,
                            int version, _Unwind_Action actions,
                            uint64_t exception_class,
                            struct _Unwind_Exception *ue_header,
                            struct _Unwind_Context *context);

_Unwind_Reason_Code
__cilk_personality_c_v0(int version, _Unwind_Action actions,
                        uint64_t exception_class,
                        struct _Unwind_Exception *ue_header,
                        struct _Unwind_Context *context) {
    return __cilk_personality_internal(__gcc_personality_v0,
                                       version, actions, exception_class,
                                       ue_header, context);
}

_Unwind_Reason_Code
__cilk_personality_v0(int version, _Unwind_Action actions,
                      uint64_t exception_class,
                      struct _Unwind_Exception *ue_header,
                      struct _Unwind_Context *context) {
    return __cilk_personality_c_v0(version, actions, exception_class,
                                   ue_header, context);
}

