// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

#include "SilverLiningTypes.h"
#include "Atmosphere.h"
#include "ResourceLoader.h"
#include "Configuration.h"
#include "Utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sstream>

using namespace SilverLining;
using namespace std;

SL_LAZY<Configuration::Settings> Configuration::settings;
int Configuration::refCount = 0;

#define MAX_LINE 1024


ConfigEntry::ConfigEntry(const char *value)
{
    stringVal = 0;

    if (value) {
        stringVal = SL_NEW char[strlen(value) + 1];
        strcpy(stringVal, value);

        intVal = atoi(value);
        floatVal = (float)LocaleSafeAtoF(value);
        doubleVal = LocaleSafeAtoF(value);
        boolVal = strcmp(value, "yes") == 0 || strcmp(value, "true") == 0;
    }
}

ConfigEntry::~ConfigEntry()
{
    if (stringVal) SL_DELETE[] stringVal;
}

bool Configuration::Initialize()
{
    refCount++;

    if (refCount > 1) return true;

    Clear();

    bool ok = Configuration::Load("SilverLining.config");
    Configuration::Load("SilverLining.override");

    return ok;
}

void Configuration::Destroy()
{
    refCount--;

    if (refCount <= 0) {
        Clear();
        refCount = 0;
    }
}

void Configuration::Clear()
{
    for (Settings::iterator it = settings.Get().begin(); it != settings.Get().end(); ++it) {
        // Delete the config entry object
        if (it->first) {
            SL_DELETE[] (char *)it->first;
        }

        if (it->second) {
            SL_DELETE it->second;
        }
    }

    settings.Get().clear();
}

bool Configuration::Load(const SL_STRING& path)
{
    char *data;
    unsigned int len;

    char line[MAX_LINE], scratch[MAX_LINE];

    if (Atmosphere::GetResourceLoader()->LoadResource(path.c_str(), data, len, true)) {
        std::stringstream *stream = SL_NEW stringstream(string(data));

        while (stream->good()) {
            stream->getline(line, MAX_LINE);

            // Remove whitespace (anything outside of ascii 33-125)
            char *src = line;
            char *dst = scratch;
            char c;
            do {
                c = *src;

                // Preserve null terminator too
                if ((c > 32 && c < 126) || c == 0) {
                    *dst++ = c;
                }

                src++;

            } while (c);

            // Toss out blanks and comments
            if (strlen(scratch) == 0) {
                continue;
            }

            if (scratch[0] == '#') {
                continue;
            }

            // Appears to be a valid entry. Tokenize on the =
            char seps[] = "=\0";
            char *skey = strtok(scratch, seps);
            if (skey) {
                char * tok = strtok(NULL, seps);
                if (tok)
                    Set(skey,tok);
            }
        }

        SL_DELETE stream;

        Atmosphere::GetResourceLoader()->FreeResource(data);

        return true;
    }

    return false;
}

void Configuration::Set(const char * key, const char * val)
{
    ConfigEntry *entry = SL_NEW ConfigEntry(val);

    Settings::iterator it = settings.Get().find(key);

    if (it != settings.Get().end()) {
        // Delete the config entry object
        if (it->second) {
            SL_DELETE it->second;
        }

        it->second = entry;
    } else {
        char * keyCopy = SL_NEW char[strlen(key) + 1];
        strcpy(keyCopy, key);

        settings.Get()[keyCopy] = entry;
    }
}

ConfigEntry * Configuration::GetConfigEntry(const char *key)
{
    Settings::iterator it = settings.Get().find(key);

    if (it != settings.Get().end()) {
        return it->second;
    }

    return NULL;
}

bool Configuration::GetStringValue(const char *key, const char *& val)
{
    ConfigEntry *ce = GetConfigEntry(key);

    if (ce) {
        val = ce->stringVal;
        return true;
    }

    return false;
}

bool Configuration::GetDoubleValue(const char * key, double& val)
{
    ConfigEntry *ce = GetConfigEntry(key);

    if (ce) {
        val = ce->doubleVal;
        return true;
    }

    return false;
}

bool Configuration::GetFloatValue(const char * key, float& val)
{
    ConfigEntry *ce = GetConfigEntry(key);

    if (ce) {
        val = ce->floatVal;
        return true;
    }

    return false;
}

bool Configuration::GetBoolValue(const char * key, bool& val)
{
    ConfigEntry *ce = GetConfigEntry(key);

    if (ce) {
        val = ce->boolVal;
        return true;
    }

    return false;
}

bool Configuration::GetIntValue(const char * key, int& val)
{
    ConfigEntry *ce = GetConfigEntry(key);

    if (ce) {
        val = ce->intVal;
        return true;
    }

    return false;
}

