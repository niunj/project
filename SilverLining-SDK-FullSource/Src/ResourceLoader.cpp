// Copyright (c) 2010-2021 Sundog Software, LLC. All rights reserved worldwide.

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "ResourceLoader.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <algorithm>

#if (!defined(WIN32) && !defined(WIN64))
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#else
#include <windows.h>
#endif

using namespace SilverLining;

ResourceLoader::~ResourceLoader()
{
    if (resourcePath) {
        SL_FREE(resourcePath);
        resourcePath = 0;
    }

    if (resourceDllPath) {
        SL_FREE(resourceDllPath);
        resourceDllPath = 0;
    }
}

void ResourceLoader::SetResourceDirPath(const char *path)
{
    if (resourcePath) SL_FREE(resourcePath);

    resourcePath = (char *)SL_MALLOC(strlen(path) + 1);
    strcpy(resourcePath, path);
}

void ResourceLoader::SetRendererDLLDirPath(const char *path)
{
    if (resourceDllPath) SL_FREE(resourceDllPath);

    resourceDllPath = (char *)SL_MALLOC(strlen(path) + 1);
    strcpy(resourceDllPath, path);
}

// Enforce proper use of forward or back slashes depending on the OS, and strip duplicate slashes.
static SL_STRING ScrubPath(SL_STRING& path)
{
    SL_STRING newPath;

    bool prevCharWasSlash = false;
    bool atStart = true;

    for (unsigned int i = 0; i < path.length(); i++) {
#if defined(WIN32) || defined(WIN64)
        if (path[i] == '/') {
            if (!prevCharWasSlash || atStart) {
                newPath += '\\';
            }
        }
#else
        if (path[i] == '\\') {
            if (!prevCharWasSlash || atStart) {
                newPath += '/';
            }
        }
#endif
        else {
            if (path[i] == '/' || path[i] == '\\') {
                if (!prevCharWasSlash || atStart) {
                    newPath += path[i];
                }
            } else {
                newPath += path[i];
            }
        }

        prevCharWasSlash = (path[i] == '/' || path[i] == '\\');
        if (atStart && !prevCharWasSlash) {
            atStart = false;
        }
    }

    return newPath;
}

const char * ResourceLoader::GetRendererDLLDirPath() const
{
    if (resourceDllPath) {
        SL_STRING filePath(resourceDllPath);
        filePath += "/";
        static SL_STRING scrubbedPath;
        scrubbedPath  = ScrubPath(filePath);
        return scrubbedPath.c_str();
    }

    static SL_STRING dllName;

    dllName = GetResourceDirPath();

#if _MSC_VER >= 1930
    dllName += "/VC143/";
#elif _MSC_VER >= 1900
    dllName += "/VC14/";
#elif _MSC_VER >= 1800
    dllName += "/VC12/";
#elif _MSC_VER >= 1700
    dllName += "/VC11/";
#elif _MSC_VER >= 1600
    dllName += "/VC10/";
#elif _MSC_VER >= 1500
    dllName += "/VC9/";
#elif _MSC_VER >= 1400
    dllName += "/VC8/";
#elif _MSC_VER >= 1300
    dllName += "/VC7/";
#else
    dllName += "/VC6/";
#endif

#if _MSC_VER >= 1400
#ifdef WIN64
    dllName += "x64/";
#else
    dllName += "win32/";
#endif
#endif

    return dllName.c_str();
}

bool ResourceLoader::LoadResource(const char *pathName, char *&data, unsigned int &dataLen, bool text)
{
    SL_STRING filePath(resourcePath);
    filePath += "/";
    filePath += pathName;

    data = 0;
    dataLen = 0;

    SL_STRING scrubbedPath = ScrubPath(filePath);

#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
    FILE* f = 0;
    fopen_s(&f, scrubbedPath.c_str(), text ? "rt" : "rb");
#else
    FILE* f = fopen(scrubbedPath.c_str(), text ? "rt" : "rb");
#endif
    if (f) {
        fseek(f, 0, SEEK_END);
        dataLen = ftell(f);
        rewind(f);

        data = (char *)SL_MALLOC(text ? dataLen + 1 : dataLen);
        if (data) {
            dataLen = (unsigned int)fread(data, 1, dataLen, f);
            fclose(f);
            if (text) {
                data[dataLen] = '\0';
                dataLen++;
            }
            return true;
        }
    }

    return false;
}

void ResourceLoader::FreeResource(char *data)
{
    if (data) {
        SL_FREE(data);
    }
}

bool ResourceLoader::GetFilesInDirectory(const char *pathName, SL_VECTOR(SL_STRING)& dirContents)
{
    SL_STRING dirPath(resourcePath);
    dirPath += "/";
    dirPath += pathName;
    dirPath += "/";

    dirContents.clear();

    SL_STRING scrubbedDir = ScrubPath(dirPath);

#if defined(WIN32) || defined(WIN64)
    // Windows-specific code below.
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    SL_STRING dirSearch = scrubbedDir + "*.*";
    hFind = FindFirstFileA(dirSearch.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                dirContents.push_back(SL_STRING(ffd.cFileName));
            }
        } while (FindNextFileA(hFind, &ffd) != 0);

        FindClose(hFind);

        std::sort(dirContents.begin(), dirContents.end());

        return true;
    } else {
        return false;
    }


#else

    DIR *dir = opendir(scrubbedDir.c_str());
    if (dir) {
        struct dirent *entry;
        do {
            entry = readdir(dir);
            if (entry) {
                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
                    dirContents.push_back(SL_STRING(entry->d_name));
                }
            }
        } while (entry);
        closedir(dir);

        std::sort(dirContents.begin(), dirContents.end());

        return true;
    } else {
        return false;
    }
#endif
}

bool ResourceLoader::ResourceTimeStamp(const char* fileName, time_t& timeStamp, const char* resourcePathToUse)
{
    SL_STRING filePath(resourcePathToUse);
    filePath += "/";
    filePath += fileName;


    SL_STRING scrubbedPath = ScrubPath(filePath);

    struct stat st;
    int ierr = stat(scrubbedPath.c_str(), &st);
    if (ierr != 0) {
        return false;
    }
    timeStamp = st.st_mtime;
    return true;
}
