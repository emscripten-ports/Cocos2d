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
    LanguageType ret = LanguageType::ENGLISH;
    char pLanguageName[16];

    EM_ASM_ARGS({
        var lang = localStorage.getItem('localization_language');
        if (lang == null) {
            stringToUTF8(window.navigator.language.replace(/-.*/,''), $0, 16);
        } else {
            stringToUTF8(lang, $0, 16);
        }
    }, pLanguageName);

    if (0 == strcmp("zh", pLanguageName))
    {
        ret = LanguageType::CHINESE;
    }
    else if (0 == strcmp("en", pLanguageName))
    {
        ret = LanguageType::ENGLISH;
    }
    else if (0 == strcmp("fr", pLanguageName))
    {
        ret = LanguageType::FRENCH;
    }
    else if (0 == strcmp("it", pLanguageName))
    {
        ret = LanguageType::ITALIAN;
    }
    else if (0 == strcmp("de", pLanguageName))
    {
        ret = LanguageType::GERMAN;
    }
    else if (0 == strcmp("es", pLanguageName))
    {
        ret = LanguageType::SPANISH;
    }
    else if (0 == strcmp("ru", pLanguageName))
    {
        ret = LanguageType::RUSSIAN;
    }
    else if (0 == strcmp("ko", pLanguageName))
    {
        ret = LanguageType::KOREAN;
    }
    else if (0 == strcmp("ja", pLanguageName))
    {
        ret = LanguageType::JAPANESE;
    }
    else if (0 == strcmp("hu", pLanguageName))
    {
        ret = LanguageType::HUNGARIAN;
    }
    else if (0 == strcmp("pt", pLanguageName))
    {
        ret = LanguageType::PORTUGUESE;
    }
    else if (0 == strcmp("ar", pLanguageName))
    {
        ret = LanguageType::ARABIC;
    }
    else if (0 == strcmp("nb", pLanguageName))
    {
        ret = LanguageType::NORWEGIAN;
    }
    else if (0 == strcmp("pl", pLanguageName))
    {
        ret = LanguageType::POLISH;
    }
    return ret;
}

int Application::getCurrentLanguageID()
{
    return (int) getCurrentLanguage();
}

NS_CC_END;
