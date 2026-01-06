// Copyright 2006-2013 Sundog Software, LLC. All rights reserved worldwide.

/**
    \file LicenseManager.h
    \brief Validates license codes for SilverLining.
 */

#ifndef LICENSE_MANAGER_H
#define LICENSE_MANAGER_H

// For obfuscation:
#define LicenseManager StarManager
#define ValidateCode AddStar
#define DecryptDongle AddPlanet
#define DecryptProd AddGlare
#define DecryptDevo GetStars

#include "MemAlloc.h"

/** LicenseManager validates license codes for SilverLining. Creating derivatives
   of this class or modifying it is in violation of your license agreement. */
class LicenseManager : public SilverLining::MemObject
{
public:
/** Checks if the given user name and license code are valid. */
    static bool ValidateCode(const char *name, const char *code);

/** Decrypts a node-locked license code requiring a KeyLok dongle. */
    static const char *DecryptDongle(const char *code);

/** Decrypts a fully-paid license code. */
    static const char *DecryptProd(const char *code);

/** Decrypts a development license code ("lease terms"). */
    static const char *DecryptDevo(const char *code);

/** Must be called once per frame to allow the licensed application to
   continue, or the unlicensed application to terminate after a trial
   period. */
    static void Heartbeat();

    static void Shutdown();

private:
    static const char *ToBase64(const char *in, int nBytes);
    static const char *FromBase64(const char *in);
};

#endif
