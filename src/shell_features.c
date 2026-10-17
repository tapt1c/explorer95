#define WIN32_LEAN_AND_MEAN
#include "shell_features.h"

static BOOL Flag(HKEY root, const char *path, const char *name)
{
    HKEY key; DWORD value = 0, bytes = sizeof(value), type = 0; LONG result;
    if (RegOpenKeyExA(root, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return FALSE;
    result = RegQueryValueExA(key, name, NULL, &type, (BYTE *)&value, &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && bytes == sizeof(value) && value != 0;
}

static BOOL LogonProvider(HKEY root)
{
    HKEY key; char name[256]; DWORD bytes = sizeof(name), type = 0; LONG result;
    if (RegOpenKeyExA(root, "Network\\Logon", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return FALSE;
    name[0] = 0;
    result = RegQueryValueExA(key, "PrimaryProvider", NULL, &type, (BYTE *)name, &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && bytes > 1 && name[0] != 0;
}

UINT ShellAvailableStartItems(DWORD version, int networkMetric, HKEY machine, HKEY user)
{
    const char *policies = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
    BOOL network = (networkMetric & 1) != 0;
    /* NT always supplies user sessions. Win95 needs user profiles or an
     * installed network logon provider; a disconnected cable is irrelevant. */
    BOOL logoff = !(version & 0x80000000UL) || Flag(machine, "Network\\Logon", "UserProfiles") ||
        (network && LogonProvider(machine));
    UINT items = ~0U;
    if (!network || Flag(user, policies, "NoNetHood") || Flag(machine, policies, "NoNetHood")) items &= ~START_ITEM_NETWORK;
    if (!logoff || Flag(user, policies, "NoLogoff") || Flag(machine, policies, "NoLogoff")) items &= ~START_ITEM_LOGOFF;
    return items;
}
