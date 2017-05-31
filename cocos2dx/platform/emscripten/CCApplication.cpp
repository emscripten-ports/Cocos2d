#include "CCApplication.h"
#include "platform/CCFileUtils.h"
#include "CCDirector.h"
#include "CCEGLView.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <emscripten/emscripten.h>

#define  LOGD(...)  fprintf(stderr, __VA_ARGS__)

NS_CC_BEGIN;

// sharedApplication pointer
Application * Application::sm_pSharedApplication = 0;
long Application::_animationInterval = 1.0f/60.0f*1000.0f;


Application::Application()
{
    CC_ASSERT(! sm_pSharedApplication);
    sm_pSharedApplication = this;
}

Application::~Application()
{
    CC_ASSERT(this == sm_pSharedApplication);
    sm_pSharedApplication = NULL;
}


extern "C" void mainLoopIter(void)
{
    EGLView::getInstance()->handleEvents();
    Director::getInstance()->mainLoop();
}

int Application::run()
{
	// Initialize instance and cocos2d.
	if (!applicationDidFinishLaunching())
	{
		return 0;
	}

    emscripten_set_main_loop(&mainLoopIter, 0, 1);
	
	return -1;
}

void Application::setAnimationInterval(double interval)
{
	// interval in milliseconds
	_animationInterval = (long)(interval * 1000);
}

void Application::setResourceRootPath(const std::string& rootResDir)
{
    _resourceRootPath = rootResDir;
    if (_resourceRootPath[_resourceRootPath.length() - 1] != '/')
    {
        _resourceRootPath += '/';
    }
    FileUtils* pFileUtils = FileUtils::getInstance();
    std::vector<std::string> searchPaths = pFileUtils->getSearchPaths();
    searchPaths.insert(searchPaths.begin(), _resourceRootPath);
    pFileUtils->setSearchPaths(searchPaths);
}

const std::string& Application::getResourceRootPath(void)
{
    return _resourceRootPath;
}

Application::Platform Application::getTargetPlatform()
{
    return Platform::OS_EMSCRIPTEN;
}

//////////////////////////////////////////////////////////////////////////
// static member function
//////////////////////////////////////////////////////////////////////////
Application* Application::getInstance()
{
    CC_ASSERT(sm_pSharedApplication);
    return sm_pSharedApplication;
}

// @deprecated Use getInstance() instead
Application* Application::sharedApplication()
{
    return Application::getInstance();
}

LanguageType Application::getCurrentLanguage()
{
    return LanguageType::ENGLISH;
}

int Application::getCurrentLanguageID()
{
    return (int) getCurrentLanguage();
}

NS_CC_END;
