/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/** \file
    \ingroup realmd
*/

#include "Common.h"
#include "RealmList.h"
#include "AuthCodes.h"
#include "Util.h"                                           // for Tokens typedef
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "Database/DatabaseEnv.h"

INSTANTIATE_SINGLETON_1( RealmList );

// list sorted from high to low build and first build used as low bound for accepted by default range (any > it will accepted by realmd at least)
std::vector<RealmBuildInfo> ExpectedRealmdClientBuilds;

std::vector<RealmBuildInfo const*> FindBuildInfo(uint16 build, uint32 os, uint32 platform)
{
    std::vector<RealmBuildInfo const*> matchingBuilds;
    for (auto const& itr : ExpectedRealmdClientBuilds)
    {
        if (itr.build == build && itr.os == os && itr.platform == platform)
            matchingBuilds.push_back(&itr);
    }

    // no appropriate build
    return matchingBuilds;
}

RealmBuildInfo const* FindBuildInfo(uint16 build)
{
    // first build is low bound of always accepted range
    if (build >= ExpectedRealmdClientBuilds[0].build)
        return &ExpectedRealmdClientBuilds[0];

    // continue from 1 with explicit equal check
    for (int i = 1; i < ExpectedRealmdClientBuilds.size(); ++i)
        if (build == ExpectedRealmdClientBuilds[i].build)
            return &ExpectedRealmdClientBuilds[i];

    // none appropriate build
    return nullptr;
}

RealmList::RealmList( ) : m_UpdateInterval(0), m_NextUpdateTime(time(nullptr))
{
}

RealmList& sRealmList
{
    static RealmList realmlist;
    return realmlist;
}

/// Load the realm list from the database
void RealmList::Initialize(uint32 updateInterval)
{
    m_UpdateInterval = updateInterval;

    LoadAllowedClients();

    ///- Get the content of the realmlist table in the database
    UpdateRealms(true);
}

void RealmList::UpdateRealm( uint32 ID, const std::string& name, const std::string& address, uint32 port, uint8 icon, RealmFlags realmflags, uint8 timezone, AccountTypes allowedSecurityLevel, float popu, const std::string& builds)
{
    ///- Create new if not exist or update existed
    Realm& realm = m_realms[name];

    realm.m_ID       = ID;
    realm.icon       = icon;
    realm.realmflags = realmflags;
    realm.timezone   = timezone;
    realm.allowedSecurityLevel = allowedSecurityLevel;
    realm.populationLevel      = popu;

    Tokens tokens = StrSplit(builds, " ");
    Tokens::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); ++iter)
    {
        uint32 build = atol((*iter).c_str());
        realm.realmbuilds.insert(build);
    }

    uint16 first_build = !realm.realmbuilds.empty() ? *realm.realmbuilds.begin() : 0;

    realm.realmBuildInfo.build = first_build;
    realm.realmBuildInfo.majorVersion = 0;
    realm.realmBuildInfo.minorVersion = 0;
    realm.realmBuildInfo.bugfixVersion = 0;
    realm.realmBuildInfo.hotfixVersion = ' ';

    if (first_build)
        if (RealmBuildInfo const* bInfo = FindBuildInfo(first_build))
            if (bInfo->build == first_build)
                realm.realmBuildInfo = *bInfo;

    ///- Append port to IP address.
    std::ostringstream ss;
    ss << address << ":" << port;
    realm.address   = ss.str();
}

void RealmList::UpdateIfNeed()
{
    // maybe disabled or updated recently
    if(!m_UpdateInterval || m_NextUpdateTime > time(nullptr))
        return;

    m_NextUpdateTime = time(nullptr) + m_UpdateInterval;

    // Clears Realm list
    m_realms.clear();

    // Get the content of the realmlist table in the database
    UpdateRealms(false);
}

void RealmList::UpdateRealms(bool init)
{
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "Updating realm list...");

    QueryResult *result = LoginDatabase.Query(
        //       0     1       2          3       4       5             6
        "SELECT `id`, `name`, `address`, `port`, `icon`, `realmflags`, `timezone`, "
        // 7                      8             9
        "`allowedSecurityLevel`, `population`, `realmbuilds` FROM `realmlist` "
        "WHERE (`realmflags` & 1) = 0 ORDER BY `name`");

    ///- Circle through results and add them to the realm map
    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            uint8 allowedSecurityLevel = fields[7].GetUInt8();

            uint8 realmflags = fields[5].GetUInt8();

            if (realmflags & ~(REALM_FLAG_OFFLINE|REALM_FLAG_NEW_PLAYERS|REALM_FLAG_RECOMMENDED|REALM_FLAG_SPECIFYBUILD))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Realm allowed have only OFFLINE Mask 0x2), or NEWPLAYERS (mask 0x20), or RECOMENDED (mask 0x40), or SPECIFICBUILD (mask 0x04) flags in DB");
                realmflags &= (REALM_FLAG_OFFLINE|REALM_FLAG_NEW_PLAYERS|REALM_FLAG_RECOMMENDED|REALM_FLAG_SPECIFYBUILD);
            }

            UpdateRealm(
                fields[0].GetUInt32(), fields[1].GetCppString(),fields[2].GetCppString(),fields[3].GetUInt32(),
                fields[4].GetUInt8(), RealmFlags(realmflags), fields[6].GetUInt8(),
                (allowedSecurityLevel <= SEC_ADMINISTRATOR ? AccountTypes(allowedSecurityLevel) : SEC_ADMINISTRATOR),
                fields[8].GetFloat(), fields[9].GetCppString());

            if(init)
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Added realm \"%s\"", fields[1].GetString());
        } while( result->NextRow() );
        delete result;
    }
}

void RealmList::LoadAllowedClients()
{
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "Loading allowed clients...");

    QueryResult *result = LoginDatabase.Query(
        //       0                 1               2                 3                 4        5     6           7
        "SELECT `major_version`, `minor_version`, `bugfix_version`, `hotfix_version`, `build`, `os`, `platform`, `integrity_hash` "
        "FROM `allowed_clients`");

    if (result)
    {
        do
        {
            Field *fields = result->Fetch();

            RealmBuildInfo buildInfo;
            buildInfo.majorVersion = fields[0].GetUInt8();
            buildInfo.minorVersion = fields[1].GetUInt8();
            buildInfo.bugfixVersion = fields[2].GetUInt8();
            std::string hotfixVersion = fields[3].GetCppString();
            buildInfo.hotfixVersion = hotfixVersion.empty() ? 0 : hotfixVersion[0];
            buildInfo.build = fields[4].GetUInt16();
            
            std::string os = fields[5].GetCppString();
            MANGOS_ASSERT(os.size() == 3);
            memcpy(&buildInfo.os, os.data(), 4);

            std::string platform = fields[6].GetCppString();
            MANGOS_ASSERT(platform.size() == 3);
            memcpy(&buildInfo.platform, platform.data(), 4);

            std::string integrityHash = fields[7].GetCppString();
            if (!integrityHash.empty())
            {
                MANGOS_ASSERT(integrityHash.size() == (20 * 2));
                HexStrToByteArray(integrityHash, buildInfo.integrityHash.data());
            }

            ExpectedRealmdClientBuilds.push_back(buildInfo);

        } while (result->NextRow());
        delete result;
    }
}
