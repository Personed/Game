#if _BULDOZER
#define APP_NAME				"Buldozer Viewer"
#define APP_NAME_SHORT	"Buldozer"
#define ICONNAME        "buldozer.ico"
#define APP_REGISTRY(XXX)  XXX(APP_NAME)
#define APP_ICONS(XX)
#elif _DEMO
#define APP_NAME				"DayZ Demo"
#define APP_NAME_SHORT	"DayZDemo"
#define ICONNAME        "dayz.ico"
#define APP_REGISTRY(XXX)  XXX(APP_NAME)
#define APP_ICONS(XX)
#else
#define APP_NAME				"DayZ"
#define APP_NAME_SHORT	"DayZ"
#define ICONNAME        "dayz.ico"
#define APP_REGISTRY(XXX)  XXX(APP_NAME)
#define APP_ICONS(XX) \
  XX(APPICON+1, "dayz.ico") \
  XX(APPICON+2, "dayz.ico") \
  XX(APPICON+3, "dayz.ico") \
  XX(APPICON+4, "dayz.ico") \
  XX(APPICON+5, "dayz.ico") \
  XX(APPICON+6, "dayz.ico")
#endif

#define RC_ICONS(id, filename) \
  id ICON DISCARDABLE filename

#define AppName APP_NAME

