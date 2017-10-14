/****************************************************************************
Copyright (c) 2010 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "CCEGLView.h"
#include "cocoa/CCSet.h"
#include "ccMacros.h"
#include "touch_dispatcher/CCTouch.h"
#include "touch_dispatcher/CCTouchDispatcher.h"
#include "text_input_node/CCIMEDispatcher.h"
#include "keyboard_dispatcher/CCKeyboardDispatcher.h"
#include "CCDirector.h"
#include "CCGL.h"
#include "CCAccelerometer.h"
#include "CCApplication.h"

#include <emscripten/emscripten.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_mixer.h>

#include <ctype.h>

#include <stdlib.h>

extern "C" {
void glutInit(int *argcp, char **argv);
void glutSpecialFunc(void (*func)(int key, int x, int y));
void glutSpecialUpFunc(void (*func)(int key, int x, int y));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutKeyboardUpFunc(void (*func)(unsigned char key, int x, int y));
}

// Constants for mouse events (inferred from experiment)
static const int glutLeftButton = 0;
static const int glutMouseDown = 0;
static const int glutMouseUp = 1;

NS_CC_BEGIN

bool EGLView::_initializedFunctions = false;
const GLubyte *EGLView::_extensions = 0;

enum Orientation
{
	PORTRAIT,
	LANDSCAPE,
	AUTO
};

static Orientation orientation = LANDSCAPE;

static EGLView* s_pInstance = NULL;

extern "C" void EMSCRIPTEN_KEEPALIVE ccEglViewMultiTouchEvent(int finger, int fstate, float fx, float fy) {
    EGLView* pEGLView = EGLView::getInstance();
    switch (fstate) {
        case 0:
            pEGLView->handleTouchesBegin(1, &finger, &fx, &fy);
            break;
        case 1:
            pEGLView->handleTouchesMove(1, &finger, &fx, &fy);
            break;
        case 2:
            pEGLView->handleTouchesEnd(1, &finger, &fx, &fy);
            break;
        default:
            assert(false);
    }
}

void onPressKeyInt(int key, int x, int y) {
    if (key == 120 /*BACKSPACE*/) {
        IMEDispatcher::sharedDispatcher()->dispatchDeleteBackward();
    }
    Director::sharedDirector()->getKeyboardDispatcher()->dispatchKeyboardEvent(key, true);
}

void onReleaseKeyInt(int key, int x, int y) {
    Director::sharedDirector()->getKeyboardDispatcher()->dispatchKeyboardEvent(key, false);
}

void onPressKey(unsigned char key, int x, int y) {
    onPressKeyInt((int) key, x, y);
}

void onReleaseKey(unsigned char key, int x, int y) {
    onReleaseKeyInt((int) key, x, y);
}


EGLView::EGLView()
{
    // mouse & touches handler
    EM_ASM((
        var getMousePos = function(event) {
            var rect = Module["canvas"].getBoundingClientRect();
            var cw = Module["canvas"].width;
            var ch = Module["canvas"].height;
            var scrollX = ((typeof window.scrollX !== 'undefined') ? window.scrollX : window.pageXOffset);
            var scrollY = ((typeof window.scrollY !== 'undefined') ? window.scrollY : window.pageYOffset);
            
            var x = event.pageX - (scrollX + rect.left);
            var y = event.pageY - (scrollY + rect.top);

            x = x * (cw / rect.width);
            y = y * (ch / rect.height);
            return [x, y];
        };

        var isTouchHeld = false;
        var isTouchDevice = 'ontouchstart' in document.documentElement;
        if (isTouchDevice) {
            var heldTouches = {};
            var multitouch = function(event) {
                if (event.target != Module['canvas']) {
                  return;
                }

                var touches = event.changedTouches,
                    main = touches[0],
                    identifier = main.identifier;

                var xy = getMousePos(main);
                var type;

                switch(event.type) {
                    case "touchstart": type = 0; break;
                    case "touchmove": type = 1; break;
                    case "touchend": type = 2; break;
                    default: return;
                }

                if (type == 0) {
                    heldTouches[identifier] = 1;
                }

                if (type == 2) {
                    delete heldTouches[identifier];
                }

                isTouchHeld = Object.keys(heldTouches).length > 0;

                Module['_ccEglViewMultiTouchEvent'](identifier, type, xy[0], xy[1]);
                event.preventDefault();
            };

            window.addEventListener("touchmove", multitouch, true);
            window.addEventListener("touchstart", multitouch, true);
            window.addEventListener("touchend", multitouch, true);
        };

        var isMousePressed = false;
        var onMouseButtonDown =function(event) {
            if (isTouchHeld || event.button != 0 || event.target != Module["canvas"]) {
                return;
            }
            var xy = getMousePos(event);
            Module['_ccEglViewMultiTouchEvent'](0, 0, xy[0], xy[1]);
            isMousePressed = true;
            event.preventDefault();
        };
        var onMouseMove = function(event) {
            if (!isMousePressed || event.target != Module["canvas"]) {
                return;
            }
            var xy = getMousePos(event);
            Module['_ccEglViewMultiTouchEvent'](0, 1, xy[0], xy[1]);
            event.preventDefault();
        };
        var onMouseButtonUp = function(event) {
            if (!isMousePressed || event.button != 0 || event.target != Module["canvas"]) {
                return;
            }
            var xy = getMousePos(event);
            Module['_ccEglViewMultiTouchEvent'](0, 2, xy[0], xy[1]);
            isMousePressed = false;
            event.preventDefault();
        };

        window.addEventListener("mousemove", onMouseMove, true);
        window.addEventListener("mousedown", onMouseButtonDown, true);
        window.addEventListener("mouseup", onMouseButtonUp, true);
    ));

	_eglDisplay = EGL_NO_DISPLAY;
	_eglContext = EGL_NO_CONTEXT;
	_eglSurface = EGL_NO_SURFACE;

    strcpy(_windowGroupID, "");
    snprintf(_windowGroupID, sizeof(_windowGroupID), "%d", 1);

    _isGLInitialized = initGL();

    if (_isGLInitialized)
    	initEGLFunctions();

    // Initialize SDL: used for font rendering, sprite loading and audio
    // playing.
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    // Emscripten ignores all these values.
    Mix_OpenAudio(0, 0, 0, 0);
    TTF_Init();

    char *arg1 = (char*)malloc(1);
    char **dummyArgv = (char**)malloc(sizeof(char*));
    dummyArgv[0] = arg1;
    glutInit(0, dummyArgv);
    free(dummyArgv[0]);
    free(dummyArgv);

    glutSpecialFunc(&onPressKeyInt);
    glutSpecialUpFunc(&onReleaseKeyInt);
    glutKeyboardFunc(&onPressKey);
    glutKeyboardUpFunc(&onReleaseKey);
}

EGLView::~EGLView()
{
}

const char* EGLView::getWindowGroupId() const
{
	return _windowGroupID;
}

void EGLView::release()
{
    if (_eglDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (_eglSurface != EGL_NO_SURFACE)
    {
        eglDestroySurface(_eglDisplay, _eglSurface);
        _eglSurface = EGL_NO_SURFACE;
    }

    if (_eglContext != EGL_NO_CONTEXT)
    {
        eglDestroyContext(_eglDisplay, _eglContext);
        _eglContext = EGL_NO_CONTEXT;
    }

    if (_eglDisplay != EGL_NO_DISPLAY)
    {
        eglTerminate(_eglDisplay);
        _eglDisplay = EGL_NO_DISPLAY;
    }

	_isGLInitialized = false;

	exit(0);
}

void EGLView::initEGLFunctions()
{
	_extensions = glGetString(GL_EXTENSIONS);
	_initializedFunctions = true;
}

bool EGLView::isOpenGLReady()
{
	return (_isGLInitialized && _screenSize.height != 0 && _screenSize.width != 0);
}

void EGLView::end()
{
    release();
}

void EGLView::swapBuffers()
{
	eglSwapBuffers(_eglDisplay, _eglSurface);
}

EGLView* EGLView::getInstance()
{
	if (!s_pInstance)
	{
		s_pInstance = new EGLView();
	}

	CCAssert(s_pInstance != NULL, "CCEGLView wasn't constructed yet");
	return s_pInstance;
}

// XXX: deprecated
EGLView* EGLView::sharedOpenGLView()
{
    return EGLView::getInstance();
}

void EGLView::showKeyboard()
{
}

void EGLView::hideKeyboard()
{
}

void EGLView::setIMEKeyboardState(bool bOpen)
{
}

bool EGLView::isGLExtension(const char *searchName) const
{
	const GLubyte *start;
	GLubyte *where, *terminator;

	/* It takes a bit of care to be fool-proof about parsing the
	 OpenGL extensions string. Don't be fooled by sub-strings,
	 etc. */
	start = _extensions;
	for (;;)
	{
		where = (GLubyte *) strstr((const char *) start, searchName);
		if (!where)
			break;

		terminator = where + strlen(searchName);
		if (where == start || *(where - 1) == ' ')
			if (*terminator == ' ' || *terminator == '\0')
				return true;

		start = terminator;
	}

	return false;
}


static EGLenum checkErrorEGL(const char* msg)
{
    assert(msg);
    static const char* errmsg[] =
    {
        "EGL function succeeded",
        "EGL is not initialized, or could not be initialized, for the specified display",
        "EGL cannot access a requested resource",
        "EGL failed to allocate resources for the requested operation",
        "EGL fail to access an unrecognized attribute or attribute value was passed in an attribute list",
        "EGLConfig argument does not name a valid EGLConfig",
        "EGLContext argument does not name a valid EGLContext",
        "EGL current surface of the calling thread is no longer valid",
        "EGLDisplay argument does not name a valid EGLDisplay",
        "EGL arguments are inconsistent",
        "EGLNativePixmapType argument does not refer to a valid native pixmap",
        "EGLNativeWindowType argument does not refer to a valid native window",
        "EGL one or more argument values are invalid",
        "EGLSurface argument does not name a valid surface configured for rendering",
        "EGL power management event has occurred",
    };
    EGLenum error = eglGetError();
    fprintf(stderr, "%s: %s\n", msg, errmsg[error - EGL_SUCCESS]);
    return error;
}

bool EGLView::initGL()
{
    EGLint eglConfigCount;
    EGLConfig config;

    // Hard-coded to 32-bit/OpenGL ES 2.0.
    const EGLint eglConfigAttrs[] =
    {
        EGL_RED_SIZE,           8,
        EGL_GREEN_SIZE,         8,
        EGL_BLUE_SIZE,          8,
        EGL_ALPHA_SIZE,         8,
        EGL_DEPTH_SIZE,         24,
        EGL_STENCIL_SIZE,       8,
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    const EGLint eglContextAttrs[] =
    {
        EGL_CONTEXT_CLIENT_VERSION,    2,
        EGL_NONE
    };

    const EGLint eglSurfaceAttrs[] =
    {
        EGL_RENDER_BUFFER,    EGL_BACK_BUFFER,
        EGL_NONE
    };

    // Get the EGL display and initialize.
    _eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (_eglDisplay == EGL_NO_DISPLAY)
    {
        perror("eglGetDisplay");
        return false;
    }

    if (eglInitialize(_eglDisplay, NULL, NULL) != EGL_TRUE)
    {
        perror("eglInitialize");
        return false;
    }

    if (eglChooseConfig(_eglDisplay, eglConfigAttrs, &config, 1, &eglConfigCount) != EGL_TRUE || eglConfigCount == 0)
    {
        checkErrorEGL("eglChooseConfig");
        return false;
    }

    _eglContext = eglCreateContext(_eglDisplay, config, EGL_NO_CONTEXT, eglContextAttrs);
    if (_eglContext == EGL_NO_CONTEXT)
    {
        checkErrorEGL("eglCreateContext");
        return false;
    }

    _eglSurface = eglCreateWindowSurface(_eglDisplay, config, NULL, eglSurfaceAttrs);
    if (_eglSurface == EGL_NO_SURFACE)
    {
        checkErrorEGL("eglCreateWindowSurface");
        return false;
    }

    if (eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext) != EGL_TRUE)
    {
        checkErrorEGL("eglMakeCurrent");
        return false;
    }

    // FIXME: Get the actual canvas size somehow.
    EGLint width;
    EGLint height;

    if ((_eglDisplay == EGL_NO_DISPLAY) || (_eglSurface == EGL_NO_SURFACE) )
    	return EXIT_FAILURE;

	eglQuerySurface(_eglDisplay, _eglSurface, EGL_WIDTH, &width);
    eglQuerySurface(_eglDisplay, _eglSurface, EGL_HEIGHT, &height);

    _screenSize.width = width;
    _screenSize.height = height;

    glViewport(0, 0, width, height);

    // Default the frame size to be the whole canvas. In general we want to be
    // setting the size of the viewport by adjusting the canvas size (so
    // there's no weird letterboxing).
    setFrameSize(width, height);

    return true;
}

static long time2millis(struct timespec *times)
{
    return times->tv_sec*1000 + times->tv_nsec/1000000;
}

bool EGLView::handleEvents()
{
	return true;
}

NS_CC_END
