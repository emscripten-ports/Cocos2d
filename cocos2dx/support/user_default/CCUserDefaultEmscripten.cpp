/*
 * Author: Aleksandr Guryanov <caiiiycuk@gmail.com>
 */

#include "CCUserDefault.h"
#include "platform/CCCommon.h"
#include "platform/CCFileUtils.h"
#include "support/base64.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace std;

NS_CC_BEGIN

UserDefault* UserDefault::_userDefault = 0;
string UserDefault::_filePath = string("");
bool UserDefault::_isFilePathInitialized = false;

/**
 * If the user invoke delete UserDefault::getInstance(), should set _userDefault
 * to null to avoid error when he invoke UserDefault::getInstance() later.
 */
UserDefault::~UserDefault() {
    CC_SAFE_DELETE(_userDefault);
    _userDefault = NULL;
}

UserDefault::UserDefault() {
    _userDefault = NULL;
}

bool UserDefault::getBoolForKey(const char* pKey) {
    return getBoolForKey(pKey, false);
}

bool UserDefault::getBoolForKey(const char* pKey, bool defaultValue) {
    return getIntegerForKey(pKey, defaultValue ? 1 : 0) == 1;
}

int UserDefault::getIntegerForKey(const char* pKey) {
    return getIntegerForKey(pKey, 0);
}

int UserDefault::getIntegerForKey(const char* pKey, int defaultValue) {
    return getDoubleForKey(pKey, defaultValue);
}

float UserDefault::getFloatForKey(const char* pKey) {
    return getFloatForKey(pKey, 0.0f);
}

float UserDefault::getFloatForKey(const char* pKey, float defaultValue) {
    return getDoubleForKey(pKey, (double)defaultValue);
}

double  UserDefault::getDoubleForKey(const char* pKey)
{
    return getDoubleForKey(pKey, 0.0);
}

double UserDefault::getDoubleForKey(const char* pKey, double defaultValue) {
    char tmp[50];
    memset(tmp, 0, 50);
    sprintf(tmp, "%f", defaultValue);
    return stod(getStringForKey(pKey, tmp));
}

std::string UserDefault::getStringForKey(const char* pKey) {
    return getStringForKey(pKey, "");
}

Data* UserDefault::getDataForKey(const char* pKey) {
    return getDataForKey(pKey, NULL);
}

Data* UserDefault::getDataForKey(const char* pKey, Data* defaultValue) {
    std::string encodedData = getStringForKey(pKey, std::string());

    Data* ret = defaultValue;

    if (encodedData.length() > 0) {
        unsigned char * decodedData = NULL;
        int decodedDataLen = base64Decode((unsigned char*) encodedData.c_str(),
                                          (unsigned int) encodedData.length(),
                                          &decodedData);
        
        if (decodedData) {
            ret = Data::create(decodedData, decodedDataLen);
            delete decodedData;
        }
    }
    
    return ret;
}


void UserDefault::setBoolForKey(const char* pKey, bool value) {
    setIntegerForKey(pKey, value ? 1 : 0);
}

void UserDefault::setIntegerForKey(const char* pKey, int value) {
    setDoubleForKey(pKey, value);
}

void UserDefault::setFloatForKey(const char* pKey, float value) {
    setDoubleForKey(pKey, value);
}

void UserDefault::setDoubleForKey(const char* pKey, double value) {
    if (!pKey) {
        return;
    }

    char tmp[50];
    memset(tmp, 0, 50);
    sprintf(tmp, "%f", value);
    setStringForKey(pKey, tmp);
}

void UserDefault::setDataForKey(const char* pKey, const Data& value) {
    if (!pKey) {
        return;
    }

    char *encodedData = 0;
    base64Encode(value.getBytes(), value.getSize(), &encodedData);
    if (encodedData) {
        setStringForKey(pKey, encodedData);
        delete encodedData;
    } else {
        setStringForKey(pKey, "");
    }
}

#ifndef __EMSCRIPTEN__
std::map<std::string, std::string> localStorage;
#endif

string UserDefault::getStringForKey(const char* pKey, const std::string & defaultValue) {
#ifdef __EMSCRIPTEN__
    static char* cbuffer = new char[16 * 1024]; // 16kb
    bool isSet = EM_ASM_DOUBLE({
                                   var key = Pointer_stringify($0);
                                   var value = localStorage.getItem(key);
                                   if (value === null) {
                                       return 0;
                                   }
                                   stringToUTF8(value, $1, 16 * 1024);
                                   return 1;
                               }, pKey, (size_t) cbuffer);

    if (isSet) {
        return std::string(cbuffer);
    } else {
        return defaultValue;
    }
#else
    auto found = localStorage.find(std::string(pKey));
    if (found != localStorage.end()) {
        return found->second;
    }

    return defaultValue;
#endif
}

void UserDefault::setStringForKey(const char* pKey, const std::string & value) {
    if (!pKey) {
        return;
    }

#ifdef __EMSCRIPTEN__
    EM_ASM_ARGS({
                    var key = Pointer_stringify($0);
                    var value = Pointer_stringify($1);
                    localStorage.setItem(key, value);
                }, pKey, value.c_str());
#else
    localStorage[std::string(pKey)] = value;
#endif
}


UserDefault* UserDefault::getInstance()
{
    if (! _userDefault) {
        _userDefault = new UserDefault();
    }

    return _userDefault;
}

void UserDefault::destroyInstance() {
    _userDefault = NULL;
}

// XXX: deprecated
UserDefault* UserDefault::sharedUserDefault() {
    return UserDefault::getInstance();
}

// XXX: deprecated
void UserDefault::purgeSharedUserDefault() {
    return UserDefault::destroyInstance();
}

bool UserDefault::isXMLFileExist() {
    FILE *fp = fopen(_filePath.c_str(), "r");
    bool bRet = false;

    if (fp)
    {
        bRet = true;
        fclose(fp);
    }

    return bRet;
}

void UserDefault::initXMLFilePath() {
    // nothing to do
}

bool UserDefault::createXMLFile() {
    return true;
}

const string& UserDefault::getXMLFilePath() {
    return _filePath;
}

void UserDefault::flush() {
}

NS_CC_END
