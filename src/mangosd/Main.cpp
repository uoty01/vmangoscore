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

/// \addtogroup mangosd Mangos Daemon
/// @{
/// \file

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "ProgressBar.h"
#include "Log.h"
#include "Master.h"
#include "SystemConfig.h"
#include "revision.h"
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <ace/Version.h>
#include <ace/Get_Opt.h>

#ifdef WIN32
#include "ServiceWin32.h"
char serviceName[] = "mangosd";
char serviceLongName[] = "MaNGOS world service";
char serviceDescription[] = "Massive Network Game Object Server";
/*
 * -1 - not in service mode
 *  0 - stopped
 *  1 - running
 *  2 - paused
 */
int m_ServiceStatus = -1;
#else
#include "PosixDaemon.h"
#endif

DatabaseType WorldDatabase;                                 ///< Accessor to the world database
DatabaseType CharacterDatabase;                             ///< Accessor to the character database
DatabaseType LoginDatabase;                                 ///< Accessor to the realm/login database
DatabaseType LogsDatabase;                                  ///< Accessor to the logs database

uint32 realmID;                                             ///< Id of the realm

/// Print out the usage string for this program on the console.
void usage(const char *prog)
{
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Usage: \n %s [<options>]\n"
        "    -v, --version            print version and exist\n\r"
        "    -c config_file           use config_file as configuration file\n\r"
        #ifdef WIN32
        "    Running as service functions:\n\r"
        "    -s run                   run as service\n\r"
        "    -s install               install service\n\r"
        "    -s uninstall             uninstall service\n\r"
        #else
        "    Running as daemon functions:\n\r"
        "    -s run                   run as daemon\n\r"
        "    -s stop                  stop daemon\n\r"
        #endif
        ,prog);
}

char const* g_mainLogFileName = "Server.log";

/// Launch the mangos server
extern int main(int argc, char **argv)
{
    ///- Command line parsing
    char const* cfg_file = _MANGOSD_CONFIG;


    char const *options = ":c:s:";

    ACE_Get_Opt cmd_opts(argc, argv, options);
    cmd_opts.long_option("version", 'v');

    char serviceDaemonMode = '\0';

    int option;
    while ((option = cmd_opts()) != EOF)
    {
        switch (option)
        {
            case 'c':
                cfg_file = cmd_opts.opt_arg();
                break;
            case 'v':
                printf("Core revision: %s\n", _FULLVERSION);
                return 0;
            case 's':
            {
                const char *mode = cmd_opts.opt_arg();

                if (!strcmp(mode, "run"))
                    serviceDaemonMode = 'r';
#ifdef WIN32
                else if (!strcmp(mode, "install"))
                    serviceDaemonMode = 'i';
                else if (!strcmp(mode, "uninstall"))
                    serviceDaemonMode = 'u';
#else
                else if (!strcmp(mode, "stop"))
                    serviceDaemonMode = 's';
#endif
                else
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Runtime-Error: -%c unsupported argument %s", cmd_opts.opt_opt(), mode);
                    usage(argv[0]);
                    Log::WaitBeforeContinueIfNeed();
                    return 1;
                }
                break;
            }
            case ':':
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Runtime-Error: -%c option requires an input argument", cmd_opts.opt_opt());
                usage(argv[0]);
                Log::WaitBeforeContinueIfNeed();
                return 1;
            default:
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Runtime-Error: bad format of commandline arguments");
                usage(argv[0]);
                Log::WaitBeforeContinueIfNeed();
                return 1;
        }
    }

#ifdef WIN32                                                // windows service command need execute before config read
    switch (serviceDaemonMode)
    {
        case 'i':
            if (WinServiceInstall())
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Installing service");
            return 1;
        case 'u':
            if (WinServiceUninstall())
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Uninstalling service");
            return 1;
        case 'r':
            WinServiceRun();
            break;
    }
#endif

    if (!sConfig.SetSource(cfg_file))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Could not find configuration file %s.", cfg_file);
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

#ifndef WIN32                                               // posix daemon commands need apply after config read
    switch (serviceDaemonMode)
    {
    case 'r':
        startDaemon();
        break;
    case 's':
        stopDaemon();
        break;
    }
#endif

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Core revision: %s [world-daemon]", _FULLVERSION);
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "<Ctrl-C> to stop." );
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "\n\n"
        "MM   MM         MM   MM  MMMMM   MMMM   MMMMM\n"
        "MM   MM         MM   MM MMM MMM MM  MM MMM MMM\n"
        "MMM MMM         MMM  MM MMM MMM MM  MM MMM\n"
        "MM M MM         MMMM MM MMM     MM  MM  MMM\n"
        "MM M MM  MMMMM  MM MMMM MMM     MM  MM   MMM\n"
        "MM M MM M   MMM MM  MMM MMMMMMM MM  MM    MMM\n"
        "MM   MM     MMM MM   MM MM  MMM MM  MM     MMM\n"
        "MM   MM MMMMMMM MM   MM MMM MMM MM  MM MMM MMM\n"
        "MM   MM MM  MMM MM   MM  MMMMMM  MMMM   MMMMM\n"
        "        MM  MMM http://getmangos.com\n"
        "        MMMMMM\n\n");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "VMaNGOS : https://github.com/vmangos");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Using configuration file %s.", cfg_file);

#define STR(s) #s
#define XSTR(s) STR(s)

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Alloc library: " MANGOS_ALLOC_LIB "");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Core Revision: " _FULLVERSION);

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "%s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
    if (SSLeay() < 0x009080bfL )
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "WARNING: Outdated version of OpenSSL lib. Logins to server may not work!");
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "WARNING: Minimal required version [OpenSSL 0.9.8k]");
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "Using ACE: %s", ACE_VERSION);

    ///- Set progress bars show mode
    BarGoLink::SetOutputState(sConfig.GetBoolDefault("ShowProgressBars", true));

    ///- and run the 'Master'
    /// \todo Why do we need this 'Master'? Can't all of this be in the Main as for Realmd?
    return sMaster.Run();

    // at sMaster return function exist with codes
    // 0 - normal shutdown
    // 1 - shutdown at error
    // 2 - restart command used, this code can be used by restarter for restart mangosd
}

/// @}
