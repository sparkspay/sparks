// Copyright (c) 2012-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>

#include <tinyformat.h>


/**
 * Name of client reported in the 'version' message. Report the same name
 * for both sparksd and sparks-qt, to make it harder for attackers to
 * target servers or GUI users specifically.
 */
const std::string CLIENT_NAME("Sparks Core");


#ifdef HAVE_BUILD_INFO
#include <obj/build.h>
// The <obj/build.h>, which is generated by the build environment (share/genbuild.sh),
// could contain only one line of the following:
//   - "#define BUILD_GIT_DESCRIPTION ...", if the top commit is not tagged
//   - "// No build information available", if proper git information is not available
#endif

//! git will put "#define ARCHIVE_GIT_DESCRIPTION ..." on the next line inside archives. $Format:%n#define ARCHIVE_GIT_DESCRIPTION "%(describe:abbrev=12)"$

#if CLIENT_VERSION_IS_RELEASE
    #define BUILD_DESC "v" PACKAGE_VERSION
    #define BUILD_SUFFIX ""
#else
    #if defined(BUILD_GIT_DESCRIPTION)
        // build in a cloned folder
        #define BUILD_DESC BUILD_GIT_DESCRIPTION
        #define BUILD_SUFFIX ""
    #elif defined(ARCHIVE_GIT_DESCRIPTION)
        // build in a folder from git archive
        #define BUILD_DESC ARCHIVE_GIT_DESCRIPTION
        #define BUILD_SUFFIX ""
    #else
        #define BUILD_DESC "v" PACKAGE_VERSION
        #define BUILD_SUFFIX "-unk"
    #endif
#endif

std::string FormatVersion(int nVersion)
{
    return strprintf("%d.%d.%d", nVersion / 10000, (nVersion / 100) % 100, nVersion % 100);
}

std::string FormatFullVersion()
{
    static const std::string CLIENT_BUILD(BUILD_DESC BUILD_SUFFIX);
    return CLIENT_BUILD;
}

/**
 * Format the subversion field according to BIP 14 spec (https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki)
 */
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments)
{
    std::ostringstream ss;
    ss << "/";
    ss << name << ":" << FormatVersion(nClientVersion);
    if (!comments.empty())
    {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "(" << *it;
        for(++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    }
    ss << "/";
    return ss.str();
}
