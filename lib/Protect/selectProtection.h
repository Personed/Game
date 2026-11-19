/*!
\file
Conditional include of copy protection macros
Dummy macros are selected when protection is disabled

Supported protections:

  _SECUROM SecuROM
  otherwise StarForce is assumed

*/

# if _SECUROM
    // SecuROM
#   include <Es/Common/win.h> // SecuROM is using windows.h - use our version
#   include "securom_api.h"
#   define CDP_DECL
#   define CHECK_PROTECTION() (SecuROM_Tripwire())
# else

#   define SECUROM_MARKER_PERFORMANCE_ON(id) {
#   define SECUROM_MARKER_PERFORMANCE_OFF(id) }

#   define SECUROM_MARKER_SECURITY_ON(id) {
#   define SECUROM_MARKER_SECURITY_OFF(id) }

#   define SECUROM_MARKER_HIGH_SECURITY_ON(id) {
#   define SECUROM_MARKER_HIGH_SECURITY_OFF(id) }

        // StarForce requires exports
#   if defined _WIN32
#     define CDP_DECL __declspec(dllexport)
#   else
#     define CDP_DECL
#   endif

#   define CHECK_PROTECTION() (true)

#endif
