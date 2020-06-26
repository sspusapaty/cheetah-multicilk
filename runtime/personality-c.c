#include <unwind.h>
#include <stdio.h>

_Unwind_Reason_Code
__cilk_personality_c_v0(int version, _Unwind_Action actions,
                      _Unwind_Exception_Class exception_class,
                      struct _Unwind_Exception *ue_header,
                      struct _Unwind_Context *context);

_Unwind_Reason_Code
__cilk_personality_v0(int version, _Unwind_Action actions,
                      _Unwind_Exception_Class exception_class,
                      struct _Unwind_Exception *ue_header,
                      struct _Unwind_Context *context) {
    return __cilk_personality_c_v0(version, actions, exception_class,
                                   ue_header, context);
}

