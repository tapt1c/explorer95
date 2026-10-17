#include "../src/shell_features.h"

static void FeatureValue(HKEY root, const char *path, const char *name, DWORD type, const void *data, DWORD bytes)
{
    HKEY key;
    Check(RegCreateKeyExA(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS,
        "create disposable capability key");
    Check(RegSetValueExA(key, name, 0, type, (const BYTE *)data, bytes) == ERROR_SUCCESS, "write capability fixture");
    RegCloseKey(key);
}
static void ShellFeatureTests(void)
{
    const char *policy = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
    const char *cleanup[] = {"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies", "Software\\Microsoft\\Windows\\CurrentVersion",
        "Software\\Microsoft\\Windows", "Software\\Microsoft", "Software", "Network\\Logon", "Network"};
    char path[128]; HKEY root, machine, user; DWORD one = 1, zero = 0; UINT items; int i;
    wsprintfA(path, "Software\\Explorer95 Feature Test %lu-%lu", GetCurrentProcessId(), GetTickCount());
    Check(RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &root, NULL) == ERROR_SUCCESS &&
        RegCreateKeyExA(root, "Machine", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &machine, NULL) == ERROR_SUCCESS &&
        RegCreateKeyExA(root, "User", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &user, NULL) == ERROR_SUCCESS,
        "capability tests use private registry roots, not installed Windows settings");
    items = ShellAvailableStartItems(0x80000004UL, 0, machine, user);
    Check(!(items & START_ITEM_NETWORK) && !(items & START_ITEM_LOGOFF) &&
        (items | START_ITEM_NETWORK | START_ITEM_LOGOFF) == ~0U,
        "standalone Win95 hides only unavailable network and logoff commands");
    Check(!(ShellAvailableStartItems(0x80000004UL, 2, machine, user) & START_ITEM_NETWORK),
        "only bit zero of SM_NETWORK indicates installed networking");
    FeatureValue(machine, "Network\\Logon", "UserProfiles", REG_DWORD, &one, sizeof(one));
    items = ShellAvailableStartItems(0x80000004UL, 0, machine, user);
    Check((items & START_ITEM_LOGOFF) && !(items & START_ITEM_NETWORK), "local user profiles allow logoff without a network component");
    FeatureValue(machine, "Network\\Logon", "UserProfiles", REG_SZ, "1", 2);
    Check(!(ShellAvailableStartItems(0x80000004UL, 0, machine, user) & START_ITEM_LOGOFF),
        "malformed profile values do not invent multi-user support");
    FeatureValue(machine, "Network\\Logon", "PrimaryProvider", REG_SZ, "Microsoft Network", sizeof("Microsoft Network"));
    items = ShellAvailableStartItems(0x80000004UL, 1, machine, user);
    Check((items & START_ITEM_LOGOFF) && (items & START_ITEM_NETWORK),
        "installed network logon enables both commands without querying connections");
    Check(!(ShellAvailableStartItems(0x80000004UL, 0, machine, user) & START_ITEM_LOGOFF),
        "stale provider name without installed networking does not enable logoff");
    FeatureValue(user, policy, "NoLogoff", REG_DWORD, &one, sizeof(one));
    Check(!(ShellAvailableStartItems(4, 1, machine, user) & START_ITEM_LOGOFF), "user NoLogoff policy also applies to NT sessions");
    FeatureValue(user, policy, "NoLogoff", REG_DWORD, &zero, sizeof(zero));
    FeatureValue(machine, policy, "NoLogoff", REG_DWORD, &one, sizeof(one));
    FeatureValue(user, policy, "NoNetHood", REG_DWORD, &one, sizeof(one));
    items = ShellAvailableStartItems(4, 1, machine, user);
    Check(!(items & START_ITEM_LOGOFF) && !(items & START_ITEM_NETWORK), "machine and user restrictions filter commands independently");
    FeatureValue(machine, policy, "NoLogoff", REG_DWORD, &zero, sizeof(zero));
    FeatureValue(user, policy, "NoNetHood", REG_DWORD, &zero, sizeof(zero));
    Check(ShellAvailableStartItems(4, 1, machine, user) == ~0U, "removing restrictions restores available items without changing user preferences");
    Check(ShellAvailableStartItems(4, 0, machine, user) & START_ITEM_LOGOFF, "NT sessions do not depend on Win95 profile registry keys");
    for (i = 0; i < (int)(sizeof(cleanup) / sizeof(cleanup[0])); ++i) {
        RegDeleteKeyA(machine, cleanup[i]); RegDeleteKeyA(user, cleanup[i]);
    }
    RegCloseKey(machine); RegCloseKey(user);
    Check(RegDeleteKeyA(root, "Machine") == ERROR_SUCCESS && RegDeleteKeyA(root, "User") == ERROR_SUCCESS,
        "disposable capability roots are empty after cleanup");
    RegCloseKey(root);
    Check(RegDeleteKeyA(HKEY_CURRENT_USER, path) == ERROR_SUCCESS, "capability fixtures leave no registry state behind");
}
