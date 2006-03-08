/****************************************************************************
 * The main entry point and related JWM functions.
 * Copyright (C) 2004 Joe Wingbermuehle
 ****************************************************************************/

#include "jwm.h"
#include "main.h"
#include "lex.h"
#include "parse.h"
#include "help.h"
#include "error.h"
#include "event.h"

#include "border.h"
#include "client.h"
#include "color.h"
#include "command.h"
#include "cursor.h"
#include "confirm.h"
#include "font.h"
#include "hint.h"
#include "group.h"
#include "key.h"
#include "icon.h"
#include "outline.h"
#include "timing.h"
#include "taskbar.h"
#include "tray.h"
#include "traybutton.h"
#include "popup.h"
#include "pager.h"
#include "swallow.h"
#include "screen.h"
#include "root.h"
#include "desktop.h"
#include "place.h"
#include "clock.h"
#include "dock.h"

Display *display = NULL;
Window rootWindow;
int rootWidth, rootHeight;
int rootDepth;
int rootScreen;
Colormap rootColormap;
Visual *rootVisual;
int colormapCount;

int shouldExit = 0;
int shouldRestart = 0;
int initializing = 0;

unsigned int desktopCount = 4;
unsigned int currentDesktop = 0;

char *exitCommand = NULL;

int borderWidth = DEFAULT_BORDER_WIDTH;
int titleHeight = DEFAULT_TITLE_HEIGHT;

unsigned int doubleClickSpeed = DEFAULT_DOUBLE_CLICK_SPEED;
unsigned int doubleClickDelta = DEFAULT_DOUBLE_CLICK_DELTA;

FocusModelType focusModel = FOCUS_SLOPPY;

XContext clientContext;
XContext frameContext;

#ifdef USE_SHAPE
int haveShape;
int shapeEvent;
#endif


static const char *CONFIG_FILE = "/.jwmrc";

static void Initialize();
static void Startup();
static void Shutdown();
static void Destroy();

static void OpenConnection();
static void CloseConnection();
static void StartupConnection();
static void ShutdownConnection();
static void EventLoop();
static void HandleExit();
static void DoExit(int code);
static void SendRestart();
static void SendExit();

static char *configPath = NULL;
static char *displayString = NULL;

/****************************************************************************
 ****************************************************************************/
int main(int argc, char *argv[]) {
	char *temp;
	int x;

	StartDebug();

	temp = getenv("HOME");
	if(temp) {
		configPath = Allocate(strlen(temp) + strlen(CONFIG_FILE) + 1);
		strcpy(configPath, temp);
		strcat(configPath, CONFIG_FILE);
	} else {
		configPath = Allocate(strlen(CONFIG_FILE) + 1);
		strcpy(configPath, CONFIG_FILE);
	}

	for(x = 1; x < argc; x++) {
		if(!strcmp(argv[x], "-v")) {
			DisplayAbout();
			DoExit(0);
		} else if(!strcmp(argv[x], "-h")) {
			DisplayHelp();
			DoExit(0);
		} else if(!strcmp(argv[x], "-p")) {
			Initialize();
			ParseConfig(configPath);
			DoExit(0);
		} else if(!strcmp(argv[x], "-restart")) {
			SendRestart();
			DoExit(0);
		} else if(!strcmp(argv[x], "-exit")) {
			SendExit();
			DoExit(0);
		} else if(!strcmp(argv[x], "-display") && x + 1 < argc) {
			displayString = argv[++x];
		} else {
			DisplayUsage();
			DoExit(1);
		}
	}

	StartupConnection();
	do {
		shouldExit = 0;
		shouldRestart = 0;
		Initialize();
		ParseConfig(configPath);
		Startup();
		EventLoop();
		Shutdown();
		Destroy();
	} while(shouldRestart);
	ShutdownConnection();

	if(exitCommand) {
		execl(SHELL_NAME, SHELL_NAME, "-c", exitCommand, NULL);
		Warning("exec failed: (%s) %s", SHELL_NAME, exitCommand);
		DoExit(1);
	} else {
		DoExit(0);
	}

	/* Control shoud never get here. */
	return -1;

}

/****************************************************************************
 ****************************************************************************/
void DoExit(int code) {
	Destroy();

	if(configPath) {
		Release(configPath);
		configPath = NULL;
	}
	if(exitCommand) {
		Release(exitCommand);
		exitCommand = NULL;
	}

	StopDebug();
	exit(code);
}

/****************************************************************************
 ****************************************************************************/
void EventLoop() {
	XEvent event;

	while(!shouldExit) {
		WaitForEvent(&event);
		ProcessEvent(&event);
	}
}

/****************************************************************************
 ****************************************************************************/
void OpenConnection() {
	display = JXOpenDisplay(displayString);
	if(!display) {
		if(displayString) {
			printf("error: could not open display %s\n", displayString);
		} else {
			printf("error: could not open display\n");
		}
		DoExit(1);
	}
	rootScreen = DefaultScreen(display);
	rootWindow = RootWindow(display, rootScreen);
	rootWidth = DisplayWidth(display, rootScreen);
	rootHeight = DisplayHeight(display, rootScreen);
	rootDepth = DefaultDepth(display, rootScreen);
	rootColormap = DefaultColormap(display, rootScreen);
	rootVisual = DefaultVisual(display, rootScreen);
	colormapCount = MaxCmapsOfScreen(ScreenOfDisplay(display, rootScreen));
}

/****************************************************************************
 ****************************************************************************/
void StartupConnection() {
	XSetWindowAttributes attr;
	int temp;

	initializing = 1;
	OpenConnection();

#if 1
	XSynchronize(display, True);
#endif

	JXSetErrorHandler(ErrorHandler);

	clientContext = XUniqueContext();
	frameContext = XUniqueContext();

	attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
		| PropertyChangeMask | ColormapChangeMask | ButtonPressMask
		| ButtonReleaseMask;
	JXChangeWindowAttributes(display, rootWindow, CWEventMask, &attr);

	signal(SIGTERM, HandleExit);
	signal(SIGINT, HandleExit);
	signal(SIGHUP, HandleExit);

#ifdef USE_SHAPE
	haveShape = JXShapeQueryExtension(display, &shapeEvent, &temp);
	if (haveShape) {
		Debug("shape extension enabled");
	} else {
		Debug("shape extension disabled");
	}
#endif

	initializing = 0;

}

/****************************************************************************
 ****************************************************************************/
void CloseConnection() {
	JXFlush(display);
	JXCloseDisplay(display);
}

/****************************************************************************
 ****************************************************************************/
void ShutdownConnection() {
	CloseConnection();
}

/****************************************************************************
 ****************************************************************************/
void HandleExit() {
	signal(SIGTERM, HandleExit);
	signal(SIGINT, HandleExit);
	signal(SIGHUP, HandleExit);
	shouldExit = 1;
}

/****************************************************************************
 * This is called before the X connection is opened.
 ****************************************************************************/
void Initialize() {
	InitializeBorders();
	InitializeClients();
	InitializeClock();
	InitializeColors();
	InitializeCommands();
	InitializeCursors();
	InitializeDesktops();
	#ifndef DISABLE_CONFIRM
		InitializeDialogs();
	#endif
	InitializeDock();
	InitializeFonts();
	InitializeGroups();
	InitializeHints();
	InitializeIcons();
	InitializeKeys();
	InitializeOutline();
	InitializePager();
	InitializePlacement();
	InitializePopup();
	InitializeRootMenu();
	InitializeScreens();
	InitializeSwallow();
	InitializeTaskBar();
	InitializeTiming();
	InitializeTray();
	InitializeTrayButtons();
}

/****************************************************************************
 * This is called after the X connection is opened.
 ****************************************************************************/
void Startup() {

	/* This order is important. */

	StartupCommands();

	StartupScreens();

	StartupGroups();
	StartupColors();
	StartupIcons();
	StartupFonts();
	StartupCursors();
	StartupOutline();

	StartupPager();
	StartupSwallow();
	StartupClock();
	StartupTaskBar();
	StartupTrayButtons();
	StartupDock();
	StartupTray();
	StartupKeys();
	StartupDesktops();
	StartupHints();
	StartupBorders();
	StartupClients();
	StartupPlacement();

	StartupTiming();
	#ifndef DISABLE_CONFIRM
		StartupDialogs();
	#endif
	StartupPopup();

	StartupRootMenu();

	SetDefaultCursor(rootWindow);
	ReadCurrentDesktop();
	JXFlush(display);

	RestackClients();

}

/****************************************************************************
 * This is called before the X connection is closed.
 ****************************************************************************/
void Shutdown() {

	/* This order is important. */

	ShutdownOutline();
	#ifndef DISABLE_CONFIRM
		ShutdownDialogs();
	#endif
	ShutdownPopup();
	ShutdownKeys();
	ShutdownPager();
	ShutdownRootMenu();
	ShutdownDock();
	ShutdownTray();
	ShutdownTrayButtons();
	ShutdownTaskBar();
	ShutdownSwallow();
	ShutdownClock();
	ShutdownBorders();
	ShutdownClients();
	ShutdownIcons();
	ShutdownCursors();
	ShutdownFonts();
	ShutdownColors();
	ShutdownGroups();
	ShutdownDesktops();

	ShutdownPlacement();
	ShutdownHints();
	ShutdownTiming();
	ShutdownScreens();

	ShutdownCommands();

}

/****************************************************************************
 * This is called after the X connection is closed.
 * Note that it is possible for this to be called more than once.
 ****************************************************************************/
void Destroy() {
	DestroyBorders();
	DestroyClients();
	DestroyClock();
	DestroyColors();
	DestroyCommands();
	DestroyCursors();
	DestroyDesktops();
	#ifndef DISABLE_CONFIRM
		DestroyDialogs();
	#endif
	DestroyDock();
	DestroyFonts();
	DestroyGroups();
	DestroyHints();
	DestroyIcons();
	DestroyKeys();
	DestroyOutline();
	DestroyPager();
	DestroyPlacement();
	DestroyPopup();
	DestroyRootMenu();
	DestroyScreens();
	DestroySwallow();
	DestroyTaskBar();
	DestroyTiming();
	DestroyTray();
	DestroyTrayButtons();
}

/****************************************************************************
 * Send _JWM_RESTART to the root window.
 ****************************************************************************/
void SendRestart() {

	XEvent event;

	OpenConnection();

	memset(&event, 0, sizeof(event));
	event.xclient.type = ClientMessage;
	event.xclient.window = rootWindow;
	event.xclient.message_type = JXInternAtom(display, "_JWM_RESTART", False);
	event.xclient.format = 32;

	JXSendEvent(display, rootWindow, False, SubstructureRedirectMask, &event);

	CloseConnection();

}

/****************************************************************************
 * Send _JWM_EXIT to the root window.
 ****************************************************************************/
void SendExit() {

	XEvent event;

	OpenConnection();

	memset(&event, 0, sizeof(event));
	event.xclient.type = ClientMessage;
	event.xclient.window = rootWindow;
	event.xclient.message_type = JXInternAtom(display, "_JWM_EXIT", False);
	event.xclient.format = 32;

	JXSendEvent(display, rootWindow, False, SubstructureRedirectMask, &event);

	CloseConnection();
}

