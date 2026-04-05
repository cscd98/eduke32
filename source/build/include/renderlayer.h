#if defined RENDERTYPEWIN
# include "winlayer.h"
#elif defined RENDERTYPESDL
# include "sdlayer.h"
#elif defined LIBRETRO
# include "retrolayer.h"
#endif

#undef STARTUP_SETUP_WINDOW

#ifdef STARTUP_WINDOW
#if defined _WIN32 || defined EDUKE32_OSX || defined HAVE_GTK2
# define STARTUP_SETUP_WINDOW
#endif
#endif

#undef WM_MSGBOX_WINDOW
#if defined _WIN32 || defined EDUKE32_OSX || defined HAVE_GTK2 || (defined RENDERTYPESDL && SDL_MAJOR_VERSION >= 2)
# define WM_MSGBOX_WINDOW
#endif
