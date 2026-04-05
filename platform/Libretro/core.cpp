#include "libretro.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// --------------------------------------------------------------------------
// eDuke32 headers
// --------------------------------------------------------------------------
#include "duke3d.h"
#include "anim.h"
#include "build.h"
#include "baselayer.h"
#include "common_game.h"
#include "control.h"
#include "function.h"
#include "game.h"
#include "gamedef.h"
#include "global.h"
#include "keyboard.h"
#include "sounds.h"
#include "renderlayer.h"
#include "mmulti.h"
#include "osdcmds.h"

// --------------------------------------------------------------------------
// core headers
// --------------------------------------------------------------------------
#include "core.h"
#include "driver_libretro.h"
#include "options.h"

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------
static constexpr int RETRO_BASE_WIDTH = 640;
static constexpr int RETRO_BASE_HEIGHT = 480;
static float retro_target_fps = 60.0f;
static int audio_frames_per_run = 800;     // SAMPLE_RATE / fps
static unsigned audio_buf_occupancy  = 50; // 0-100, from status callback
double target_sample_rate = DEFAULT_SAMPLE_RATE;
static constexpr int AUDIO_SAMPLE_RATE = DEFAULT_SAMPLE_RATE;
static int16_t audio_tmp[DEFAULT_SAMPLE_RATE / 15 * 2];

// --------------------------------------------------------------------------
// RetroArch callbacks
// --------------------------------------------------------------------------
retro_environment_t environ_cb = nullptr;
static retro_video_refresh_t video_cb = nullptr;
static retro_audio_sample_t audio_cb = nullptr;
static retro_audio_sample_batch_t audio_batch_cb  = nullptr;
static retro_input_poll_t  input_poll_cb = nullptr;
static retro_input_state_t input_state_cb = nullptr;
static retro_log_printf_t log_cb  = nullptr;

// --------------------------------------------------------------------------
// HW render
// --------------------------------------------------------------------------
static retro_hw_render_callback hw_render = {};
static bool use_hw = false;
retro_hw_get_proc_address_t get_proc_address_cb = nullptr;
static GLuint libretro_vao = 0;
bool context_ready       = false;

// --------------------------------------------------------------------------
// Input state
// --------------------------------------------------------------------------
struct ButtonMap {
    unsigned retro_btn;
    int      gamefunc;
};

static const ButtonMap button_map[] = {
    { RETRO_DEVICE_ID_JOYPAD_UP,     gamefunc_Move_Forward      },
    { RETRO_DEVICE_ID_JOYPAD_DOWN,   gamefunc_Move_Backward     },
    { RETRO_DEVICE_ID_JOYPAD_LEFT,   gamefunc_Turn_Left         },
    { RETRO_DEVICE_ID_JOYPAD_RIGHT,  gamefunc_Turn_Right        },
    { RETRO_DEVICE_ID_JOYPAD_A,      gamefunc_Fire              },
    { RETRO_DEVICE_ID_JOYPAD_B,      gamefunc_Open              },
    { RETRO_DEVICE_ID_JOYPAD_L,      gamefunc_Previous_Weapon   },
    { RETRO_DEVICE_ID_JOYPAD_R,      gamefunc_Next_Weapon       },
    { RETRO_DEVICE_ID_JOYPAD_L2,     gamefunc_Alt_Fire          },
    { RETRO_DEVICE_ID_JOYPAD_R2,     gamefunc_Run               },
    { RETRO_DEVICE_ID_JOYPAD_L3,     gamefunc_Map               },
    { RETRO_DEVICE_ID_JOYPAD_R3,     gamefunc_Toggle_Crosshair  },
    { RETRO_DEVICE_ID_JOYPAD_SELECT, gamefunc_Inventory         },
};
static constexpr int NUM_BUTTON_MAP = (int)(sizeof(button_map) / sizeof(button_map[0]));

static float analog_lx = 0.f, analog_ly = 0.f;
static float analog_rx = 0.f, analog_ry = 0.f;
static int32_t retro_mouse_dx = 0, retro_mouse_dy = 0;

static const struct { unsigned retro_key; uint8_t sc; } keymap[] = {
    { RETROK_UP,        sc_UpArrow    },
    { RETROK_DOWN,      sc_DownArrow  },
    { RETROK_LEFT,      sc_LeftArrow  },
    { RETROK_RIGHT,     sc_RightArrow },
    { RETROK_SPACE,     sc_Space      },
    { RETROK_RETURN,    sc_Enter      },
    { RETROK_ESCAPE,    sc_Escape     },
    { RETROK_LCTRL,     sc_LeftControl  },
    { RETROK_RCTRL,     sc_RightControl },
    { RETROK_LALT,      sc_LeftAlt      },
    { RETROK_RALT,      sc_RightAlt     },
    { RETROK_LSHIFT,    sc_LeftShift    },
    { RETROK_RSHIFT,    sc_RightShift   },
    { RETROK_TAB,       sc_Tab          },
    { RETROK_BACKSPACE, sc_BackSpace    },
    { RETROK_CAPSLOCK,  sc_CapsLock     },
    { RETROK_F1,  sc_F1  }, { RETROK_F2,  sc_F2  }, { RETROK_F3,  sc_F3  },
    { RETROK_F4,  sc_F4  }, { RETROK_F5,  sc_F5  }, { RETROK_F6,  sc_F6  },
    { RETROK_F7,  sc_F7  }, { RETROK_F8,  sc_F8  }, { RETROK_F9,  sc_F9  },
    { RETROK_F10, sc_F10 }, { RETROK_F11, sc_F11 }, { RETROK_F12, sc_F12 },
    { RETROK_a, sc_A }, { RETROK_b, sc_B }, { RETROK_c, sc_C },
    { RETROK_d, sc_D }, { RETROK_e, sc_E }, { RETROK_f, sc_F },
    { RETROK_g, sc_G }, { RETROK_h, sc_H }, { RETROK_i, sc_I },
    { RETROK_j, sc_J }, { RETROK_k, sc_K }, { RETROK_l, sc_L },
    { RETROK_m, sc_M }, { RETROK_n, sc_N }, { RETROK_o, sc_O },
    { RETROK_p, sc_P }, { RETROK_q, sc_Q }, { RETROK_r, sc_R },
    { RETROK_s, sc_S }, { RETROK_t, sc_T }, { RETROK_u, sc_U },
    { RETROK_v, sc_V }, { RETROK_w, sc_W }, { RETROK_x, sc_X },
    { RETROK_y, sc_Y }, { RETROK_z, sc_Z },
    { RETROK_0, sc_0 }, { RETROK_1, sc_1 }, { RETROK_2, sc_2 },
    { RETROK_3, sc_3 }, { RETROK_4, sc_4 }, { RETROK_5, sc_5 },
    { RETROK_6, sc_6 }, { RETROK_7, sc_7 }, { RETROK_8, sc_8 },
    { RETROK_9, sc_9 },
};

static constexpr int NUM_KEYS = (int)(sizeof(keymap)/sizeof(keymap[0]));

static bool key_held[NUM_KEYS] = {};

static void poll_input(void)
{
    input_poll_cb();

    // Joypad
    for (int i = 0; i < NUM_BUTTON_MAP; ++i)
        CONTROL_ButtonFlags[button_map[i].gamefunc] =
            input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, button_map[i].retro_btn) ? 1 : 0;

    // Keyboard
    KB_KeyDown[sc_LeftAlt]      = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    KB_KeyDown[sc_RightControl] = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);

    // Analog sticks
    analog_lx = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X) / 32767.f;
    analog_ly = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y) / 32767.f;
    analog_rx = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.f;
    analog_ry = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.f;

    localInput.fvel += (int16_t)(-analog_ly * 2048.f);
    localInput.svel += (int16_t)( analog_lx * 2048.f);

    auto &myplayer = *g_player[myconnectindex].ps;
    myplayer.q16ang = fix16_add(myplayer.q16ang, fix16_from_float(analog_rx * 256.f));
    myplayer.q16ang = fix16_from_int(fix16_to_int(myplayer.q16ang) & 2047);
    myplayer.q16horiz = fix16_clamp(
        fix16_add(myplayer.q16horiz, fix16_from_float(analog_ry * 64.f)),
        F16(HORIZ_MIN), F16(HORIZ_MAX));

    static bool prev_keys[NUM_KEYS] = {};
    for (int i = 0; i < NUM_KEYS; i++) {
        bool pressed = input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keymap[i].retro_key) != 0;
        KB_KeyDown[keymap[i].sc] = pressed ? 1 : 0;
        prev_keys[i] = pressed;
    }

    extern int32_t g_mouseBits;

    int32_t mx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    int32_t my = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    bool mb_l  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)   != 0;
    bool mb_r  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)  != 0;
    bool mb_m  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) != 0;

    g_mouseEnabled = 1;
    g_mouseGrabbed = 1;
    g_mouseInsideWindow = 1;
    g_mousePos.x += mx * g_mouse_sensitivity;
    g_mousePos.y += (g_mouse_invert ? -my : my) * g_mouse_sensitivity;
    g_mouseBits = (mb_l ? 1 : 0) | (mb_r ? 2 : 0) | (mb_m ? 4 : 0);
}

// --------------------------------------------------------------------------
// OpenGL context callbacks
// --------------------------------------------------------------------------
static void context_reset(void)
{
#ifdef USE_OPENGL
    libretro_glad_init();

    glGenVertexArrays(1, &libretro_vao);
    glBindVertexArray(libretro_vao);

    GLuint fbo = hw_render.get_current_framebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity,
                            GLsizei length, const GLchar *message, const void *userParam) {
    }, nullptr);
#endif

    context_ready = true;
}

static void context_destroy(void)
{
}

// --------------------------------------------------------------------------
// Game-state
// --------------------------------------------------------------------------
bool game_initialised = false;
static bool level_needs_enter = true;
static bool level_loaded = false;
static std::string content_path;

// --------------------------------------------------------------------------
//  libretro API implementation
// --------------------------------------------------------------------------
extern "C" {

// Forward declarations
static void audio_buffer_status_cb(bool active, unsigned occupancy, bool underrun_likely);
void main_loop_restart();

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
}

RETRO_API void retro_init(void)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "XRGB8888 not supported\n");
    }

    double rate = DEFAULT_SAMPLE_RATE;

    /* Ask frontend for its preferred output rate */
    if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE, &rate) && rate > 0.0)
        target_sample_rate = rate;

    // Query frontend's target refresh rate to size audio buffers correctly.
    float fps = 0.0f;
    if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &fps) && fps > 0.0f)
        retro_target_fps = fps;
    audio_frames_per_run = (int)((float)target_sample_rate / retro_target_fps + 0.5f);

    // register occupancy cb
    retro_audio_buffer_status_callback audio_status = { audio_buffer_status_cb };
    environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &audio_status);

        // Register options v2
    if (!environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_v2))
        if (log_cb) log_cb(RETRO_LOG_WARN, "libretro-eduke32: options v2 not supported by frontend\n");

    // Read initial option values so ud.setup is populated before retro_load_game
    apply_core_options();

    // Controller description
    static const retro_controller_description controllers[] = {
        { "Dual Analog Gamepad", RETRO_DEVICE_JOYPAD },
        { nullptr, 0 },
    };

    static const retro_controller_info ports[] = {
        { controllers, 1 },
        { nullptr, 0 },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

    // Input descriptors
    static const retro_input_descriptor descs[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Move Forward"     },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Move Backward"    },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Turn Left"        },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Turn Right"       },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Fire"             },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Open / Interact"  },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Jump"             },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Crouch"           },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Previous Weapon"  },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Next Weapon"      },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Alt Fire"         },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Run"              },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "Automap"          },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Inventory"        },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Automap (Start)"  },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_X, "Strafe (Analog)"  },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_Y, "Move (Analog)"    },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_X, "Turn (Analog)"    },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_Y, "Look (Analog)"    },
        { 0, 0, 0, 0, nullptr },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descs);

    static const char *fake_argv[] = { "eduke32_libretro", nullptr };
    int fake_argc = 1;

    engineSetupAllocator();
    engineSetupLogging(fake_argc, const_cast<char**>(fake_argv));
    engineSetLogFile(APPBASENAME ".log", LOG_GAME_MAX);

    osdcallbacks_t callbacks = {};
    callbacks.drawchar        = dukeConsolePrintChar;
    callbacks.drawstr         = dukeConsolePrintString;
    callbacks.drawcursor      = dukeConsolePrintCursor;
    callbacks.getcolumnwidth  = dukeConsoleGetColumnWidth;
    callbacks.getrowheight    = dukeConsoleGetRowHeight;
    callbacks.clear           = dukeConsoleClearBackground;
    callbacks.gettime         = BGetTime;
    callbacks.onshowosd       = dukeConsoleOnShowCallback;

    LOG_F(INFO, HEAD2 " %s", s_buildRev);
    PrintBuildInfo();

    g_maxDefinedSkill = 4;
    ud.multimode = 1;

    G_MaybeAllocPlayer(0);

    hash_init(&h_gamefuncs);
    for (bssize_t i=NUMGAMEFUNCTIONS-1; i>=0; i--)
    {
        if (gamefunctions[i][0] == '\0')
            continue;
        hash_add(&h_gamefuncs,gamefunctions[i],i,0);
    }

    CONFIG_ReadSetup();

    apply_core_options();
}

RETRO_API void retro_deinit(void)
{
    if (game_initialised)
    {
        S_SoundShutdown();
        S_MusicShutdown();
        engineUnInit();
        game_initialised = false;
    }
}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(retro_system_info *info)
{
    Bmemset(info, 0, sizeof(*info));
    info->library_name     = "EDuke32";
    info->library_version  = "1.0.0";
    info->valid_extensions = "grp|con|zip|pk3|pk4";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info *info)
{
    info->geometry.base_width   = (unsigned)RETRO_BASE_WIDTH;
    info->geometry.base_height  = (unsigned)RETRO_BASE_HEIGHT;
    info->geometry.max_width    = (unsigned)RETRO_MAX_WIDTH;
    info->geometry.max_height   = (unsigned)RETRO_MAX_HEIGHT;
    info->geometry.aspect_ratio = (float)RETRO_BASE_WIDTH / (float)RETRO_BASE_HEIGHT;
    info->timing.fps            = retro_target_fps;
    info->timing.sample_rate    = target_sample_rate;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port; (void)device;
}

RETRO_API void retro_reset(void)
{
    g_player[myconnectindex].ps->gm = MODE_RESTART;
}

bool init_game()
{
    if (enginePreInit())
       log_cb(RETRO_LOG_INFO, "libretro-eduke32: engine failed to preinit\n");
    {
        std::string dir = content_path;
        auto slash = dir.find_last_of("/\\");
        std::string grpfile = (slash != std::string::npos) ? dir.substr(slash + 1) : dir;
        if (slash != std::string::npos) dir = dir.substr(0, slash);
        else dir = ".";

        addsearchpath(dir.c_str());
        if (initgroupfile(grpfile.c_str()) == -1) {
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "libretro-eduke32: could not open GRP '%s'\n",
                       content_path.c_str());
            return false;
        }
    }

    G_ScanGroups();
    G_LoadGroups(!g_noAutoLoad && !ud.setup.noautoload);

    numplayers = 1;
    g_mostConcurrentPlayers = ud.multimode;
    connectpoint2[0] = -1;

    for (int i=0; i<MAXPLAYERS; i++)
        G_MaybeAllocPlayer(i);

    G_Startup();

    g_player[0].playerquitflag = 1;

    auto &myplayer = *g_player[myconnectindex].ps;
    myplayer.palette = BASEPAL;

    for (int i=1, j=numplayers; j<ud.multimode; j++)
    {
        Bsprintf(g_player[j].user_name,"PLAYER %d",j+1);
        g_player[j].ps->team = g_player[j].pteam = i;
        g_player[j].ps->weaponswitch = 3;
        g_player[j].ps->auto_aim = 0;
        i = 1-i;
    }

    Anim_Init();

    char const *deffile = G_DefFile();
    if (!loaddefinitionsfile(deffile))
        if (log_cb) log_cb(RETRO_LOG_INFO, "libretro-eduke32: loaded DEF file '%s'\n", deffile);
    loaddefinitions_game(deffile, FALSE);

    for (char * m : g_defModules)
        Xfree(m);
    g_defModules.clear();

    cacheAllSounds();

    if (enginePostInit()) {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "libretro-eduke32: enginePostInit() failed\n");
        return false;
    }

    G_PostLoadPalette();
    tileDelete(MIRROR);
    Gv_ResetSystemDefaults();

    ud.m_level_number  = 0;
    ud.m_volume_number = 0;
    ud.warp_on         = 1;

    g_player[0].ps->palette = BASEPAL;

    connecthead = 0;
    connectpoint2[0] = -1;
    myconnectindex = 0;
    g_mostConcurrentPlayers = ud.multimode;

    ++ud.executions;

    char const * rtsname = g_rtsNamePtr ? g_rtsNamePtr : ud.rtsname;
    RTS_Init(rtsname);

    ud.last_level = -1;

    Bsprintf(tempbuf, HEAD2 " %s", s_buildRev);
    OSD_SetVersion(tempbuf, 10,0);
    OSD_SetParameters(0, 0, 0, 12, 2, 12, OSD_ERROR, OSDTEXT_RED, OSDTEXT_DARKRED, gamefunctions[gamefunc_Show_Console][0] == '\0' ? OSD_PROTECTED : 0);
    registerosdcommands();

    extern int32_t BGetTime(void);
    CONTROL_Startup(controltype_keyboardandmouse, &BGetTime, TICRATE);

    G_SetupGameButtons();
    CONFIG_SetupMouse();
    CONFIG_SetupJoystick();

    CONTROL_JoystickEnabled = (ud.setup.usejoystick && CONTROL_JoyPresent);
    CONTROL_MouseEnabled = ud.setup.usemouse;

    CONFIG_ReadSettings();

    OSD_Exec("autoexec.cfg");
    CONFIG_SetDefaultKeys(keydefaults, true);
    system_getcvars();

    if (videoSetGameMode(ud.setup.fullscreen, ud.setup.xdim, ud.setup.ydim, ud.setup.bpp, ud.detail) < 0)
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "Failure setting video mode %dx%dx%d %s! Trying next mode...",
            ud.setup.xdim, ud.setup.ydim, ud.setup.bpp,
            ud.setup.fullscreen ? "fullscreen" : "windowed");
    }
#ifdef USE_OPENGL
    polymost_debugShaders();
#endif

    retro_game_geometry geom = {};
    geom.base_width   = (unsigned)xdim;
    geom.base_height  = (unsigned)ydim;
    geom.max_width    = (unsigned)RETRO_MAX_WIDTH;
    geom.max_height   = (unsigned)RETRO_MAX_HEIGHT;
    geom.aspect_ratio = (float)xdim / (float)ydim;
    environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);

    videoSetPalette(ud.brightness>>2, myplayer.palette, 0);

    ud.config.MusicDevice  = ASS_OPL3;
    S_SoundStartup();
    S_MusicStartup();
    G_InitText();
    Menu_Init();
    ReadSaveGameHeaders();
    FX_StopAllSounds();
    S_ClearSoundLocks();

    VM_OnEvent(EVENT_INITCOMPLETE);

    main_loop_restart();

    ud.overhead_on = 0;
    ud.last_overhead = 0;
    tileLoad(MENUSCREEN);
    g_player[myconnectindex].ps->over_shoulder_on = 0;
    g_player[myconnectindex].ps->gm = MODE_MENU;

    libretro_alloc_sw_frame(xdim, ydim);

    game_initialised = true;

    return true;
}

void main_loop_restart()
{
    totalclock = 0;
    ototalclock = 0;
    lockclock = 0;

    auto &myplayer = *g_player[myconnectindex].ps;
    myplayer.fta = 0;
    for (int32_t & q : user_quote_time)
        q = 0;

    Menu_Change(MENU_MAIN);

    if (myplayer.gm & MODE_NEWGAME)
    {
        G_NewGame(ud.m_volume_number, ud.m_level_number, ud.m_player_skill);
        myplayer.gm = MODE_RESTART;
    }
    else
    {
        if (ud.warp_on == 1)
            G_NewGame_EnterLevel();

        if (ud.warp_on == 0)
        {
            if ((g_netServer || ud.multimode > 1) && boardfilename[0] != 0)
            {
                ud.m_level_number = 7;
                ud.m_volume_number = 0;
                ud.m_respawn_monsters = !!(ud.m_player_skill == 4);

                for (int TRAVERSE_CONNECT(i))
                {
                    P_ResetWeapons(i);
                    P_ResetInventory(i);
                }

                G_NewGame_EnterLevel();
                Net_WaitForServer();
            }
            else if (g_networkMode != NET_DEDICATED_SERVER)
                G_DisplayLogo();

            if (g_networkMode != NET_DEDICATED_SERVER)
            {
                if (G_PlaybackDemo())
                {
                    FX_StopAllSounds();
                    g_noLogoAnim = 1;
                    main_loop_restart();
                }
            }
        }
        else G_UpdateScreenArea();
    }

    ud.showweapons = ud.config.ShowWeapons;
    P_SetupMiscInputSettings();
    g_player[myconnectindex].pteam = ud.team;

    if (g_gametypeFlags[ud.coop] & GAMETYPE_TDM)
        myplayer.palookup = g_player[myconnectindex].pcolor = G_GetTeamPalette(g_player[myconnectindex].pteam);
    else
    {
        if (ud.color) myplayer.palookup = g_player[myconnectindex].pcolor = ud.color;
        else myplayer.palookup = g_player[myconnectindex].pcolor;
    }

    ud.warp_on = 0;
    KB_KeyDown[sc_Pause] = 0;
}

// --------------------------------------------------------------------------
// retro_load_game
// --------------------------------------------------------------------------
RETRO_API bool retro_load_game(const retro_game_info *info)
{
    if (!info || !info->path)
        return false;

    content_path = info->path;

#ifdef USE_OPENGL
    hw_render.context_type  = RETRO_HW_CONTEXT_OPENGL;
    hw_render.version_major = 3;
#if defined EDUKE32_GLES
    hw_render.context_type  = RETRO_HW_CONTEXT_OPENGLES2;
    hw_render.version_major = 2;
#endif
#else
    hw_render.context_type = RETRO_HW_CONTEXT_NONE;
#endif
    hw_render.version_minor      = 0;
    hw_render.context_reset      = context_reset;
    hw_render.context_destroy    = context_destroy;
    hw_render.depth              = true;
    hw_render.stencil            = false;
    hw_render.bottom_left_origin = true;

#ifdef USE_OPENGL
    if (environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render)) {
        use_hw = true;
        get_proc_address_cb = hw_render.get_proc_address;
        nogl = 0;
    } else
#endif
    {
        hw_render.context_type = RETRO_HW_CONTEXT_NONE;
        context_ready = true;
        if (log_cb) log_cb(RETRO_LOG_INFO, "Using software rendering\n");
    }

    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t)
{
    return false;
}

RETRO_API void retro_unload_game(void)
{
    if (game_initialised) {
        S_SoundShutdown();
        S_MusicShutdown();
        engineUnInit();
        FreeGroups();
        game_initialised = false;
    }
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API void retro_run(void)
{
    // Re-apply options whenever the user changes them in the Quick Menu
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        apply_core_options();

    if (!context_ready) {
        video_cb(NULL, xdim, ydim, 0);
        return;
    }

    if (!game_initialised)
        init_game();

    auto &myplayer = *g_player[myconnectindex].ps;

    Net_GetPackets();
    handleevents();

    poll_input();

    auto &myps = *g_player[myconnectindex].ps;

    if (((myps.gm & (MODE_MENU|MODE_DEMO)) == 0) &&
        (int32_t)(totalclock - ototalclock) >= TICSPERFRAME)
    {
        do {
            if (g_frameJustDrawn && (myps.gm & (MODE_MENU|MODE_DEMO)) == 0)
                dukeFillInputForTic();

            ototalclock += TICSPERFRAME;

            if (((ud.show_help == 0 && (myps.gm & MODE_MENU) != MODE_MENU) || ud.recstat == 2)
                && (myps.gm & MODE_GAME))
            {
                g_frameJustDrawn = false;
                Net_GetPackets();
                G_DoMoveThings();
            }
        } while ((myps.gm & (MODE_MENU|MODE_DEMO)) == 0 &&
                 (int32_t)(totalclock - ototalclock) >= TICSPERFRAME);
    }

    G_DoCheats();

    if (myps.gm & MODE_NEWGAME) {
        main_loop_restart();
        return;
    }

    if (myps.gm & (MODE_EOL|MODE_RESTART)) {
        polymost_resetShaderIDs();
        switch (G_EndOfLevel()) {
            case 1:
                video_cb(NULL, (unsigned)xdim, (unsigned)ydim, 0);
                return;
            case 2:
                main_loop_restart();
                break;
        }
    }

    if (use_hw)
    {
        GLuint fbo = hw_render.get_current_framebuffer();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, xdim, ydim);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        polymost_forceShaderRebind();
    }

    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    drawframe_do();

    if (myplayer.gm & MODE_DEMO)
        main_loop_restart();

    if (use_hw)
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)xdim, (unsigned)ydim, 0);
    else
        video_cb(libretro_sw_frame, xdim, ydim, bytesperline);

    const int nominal = audio_frames_per_run;
    const int adjust  = ((int)audio_buf_occupancy - 50) * nominal / 200;
    const int frames  = nominal - adjust;  // more frames when underrunning
    const int clamped = frames < nominal * 3 / 4 ? nominal * 3 / 4
                    : frames > nominal * 5 / 4 ? nominal * 5 / 4
                    : frames;

    static int16_t *audio_tmp = nullptr;
    static size_t audio_tmp_frames = 0;
    audio_tmp_frames = (size_t)(target_sample_rate / 10.0 * 2.0); // 100ms stereo
    audio_tmp = (int16_t*)malloc(audio_tmp_frames * sizeof(int16_t));

    CustomDrv_PCM_Service(audio_tmp, clamped * 2 * (int)sizeof(int16_t));
    audio_batch_cb(audio_tmp, (unsigned)clamped);
}

// --------------------------------------------------------------------------
// Misc
// --------------------------------------------------------------------------
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb  = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

// --------------------------------------------------------------------------
// Save / load state
// --------------------------------------------------------------------------
RETRO_API size_t retro_serialize_size(void) { return 16 * 1024 * 1024; }
RETRO_API bool retro_serialize(void * /*data*/, size_t /*size*/)   { return false; }
RETRO_API bool retro_unserialize(const void * /*data*/, size_t /*size*/) { return false; }

// --------------------------------------------------------------------------
// Misc stubs
// --------------------------------------------------------------------------
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

} // extern "C"

// --------------------------------------------------------------------------
// Call backs
// --------------------------------------------------------------------------
static void audio_buffer_status_cb(bool active, unsigned occupancy, bool underrun_likely)
{
    audio_buf_occupancy = occupancy;
    (void)active; (void)underrun_likely;
}