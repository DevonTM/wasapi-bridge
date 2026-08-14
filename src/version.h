#ifndef WB_VERSION_H
#define WB_VERSION_H

// Single source of truth for the project version. Bump the three numbers
// here; both the About tab (WB_VERSION) and the .rc VERSIONINFO block
// (which includes this file) derive from them.
#define WB_VERSION_MAJOR 0
#define WB_VERSION_MINOR 7
#define WB_VERSION_PATCH 0

// Stringize helpers: WB_VER_STR(WB_VERSION_MAJOR) -> "0", etc. The two-level
// indirection is required so the macro *value* is expanded before #.
#define WB_VER_STR2(x) #x
#define WB_VER_STR(x)  WB_VER_STR2(x)

// "MAJOR.MINOR.PATCH" via adjacent string-literal concatenation. Usable from
// both C++ (About tab) and the RC compiler (VERSIONINFO string values).
#define WB_VERSION  WB_VER_STR(WB_VERSION_MAJOR) "." \
                    WB_VER_STR(WB_VERSION_MINOR) "." \
                    WB_VER_STR(WB_VERSION_PATCH)

#endif // WB_VERSION_H
