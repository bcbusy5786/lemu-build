/*
 * vmmsetup.exe - install or remove vmmouse.sys on Windows NT 3.1 to 4.0.
 *
 * A small setup window with Install, Remove and Close buttons. The driver
 * (vmmouse.sys) is carried inside this .exe as a resource and written out on
 * Install, so this one file is all you need to copy to a guest.
 *
 * It also works from the command line:
 *
 *   vmmsetup /install    install the driver   (add /q for no window)
 *   vmmsetup /remove     remove it            (add /q for no window)
 *   vmmsetup /status     show what it is doing (add /q to only set errorlevel)
 *
 * Restart Windows NT after an install or a remove.
 *
 * vmmouse must start after i8042prt (it attaches to i8042prt's device) and
 * before mouclass (which must find the filter). So it goes into i8042prt's
 * load group with the next free tag, and its tag goes right after i8042prt's
 * tag in Control\GroupOrderList.
 *
 * Builds with the Win32 SDK alone (no DDK). Links user32, gdi32, advapi32.
 */

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "vmmouse.h"
#include "vmmsetup.h"

#define SERVICES    "SYSTEM\\CurrentControlSet\\Services"
#define SVCKEY      SERVICES "\\" VMM_SERVICE
#define I8042KEY    SERVICES "\\i8042prt"
#define ORDERKEY    "SYSTEM\\CurrentControlSet\\Control\\GroupOrderList"
#define MAXTAGS     256

/* result of an action, for the window and for the command line */
#define R_OK        0           /* done */
#define R_NOADMIN   1           /* not an administrator */
#define R_FAIL      2           /* something went wrong */
#define R_NOOP      3           /* nothing to do (e.g. remove when absent) */

static HINSTANCE g_hInst;

/* ------------------------------------------------------------------ */
/* registry helpers (from install.c, unchanged logic)                 */
/* ------------------------------------------------------------------ */

static BOOL
getstr(HKEY k, char *name, char *buf, DWORD size)
{
    DWORD type, len = size;

    if (RegQueryValueEx(k, name, NULL, &type, (LPBYTE) buf, &len) != ERROR_SUCCESS)
        return FALSE;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return FALSE;
    buf[size - 1] = 0;
    return TRUE;
}

static BOOL
getdword(HKEY k, char *name, DWORD *v)
{
    DWORD type, len = sizeof(*v);

    return RegQueryValueEx(k, name, NULL, &type, (LPBYTE) v, &len) == ERROR_SUCCESS
        && type == REG_DWORD;
}

static BOOL
setdword(HKEY k, char *name, DWORD v)
{
    return RegSetValueEx(k, name, 0, REG_DWORD, (LPBYTE) &v, sizeof(v)) == ERROR_SUCCESS;
}

/* GroupOrderList\<group> is REG_BINARY: a count, then that many tags. */
static DWORD
readorder(HKEY order, char *group, DWORD *tags)
{
    DWORD buf[MAXTAGS + 1], type, len = sizeof(buf), n, i;

    if (RegQueryValueEx(order, group, NULL, &type, (LPBYTE) buf, &len) != ERROR_SUCCESS)
        return 0;
    if (type != REG_BINARY || len < 4)
        return 0;
    n = buf[0];
    if (n > (len - 4) / 4)
        n = (len - 4) / 4;
    for (i = 0; i < n; i++)
        tags[i] = buf[i + 1];
    return n;
}

static BOOL
writeorder(HKEY order, char *group, DWORD *tags, DWORD n)
{
    DWORD buf[MAXTAGS + 1], i;

    buf[0] = n;
    for (i = 0; i < n; i++)
        buf[i + 1] = tags[i];
    return RegSetValueEx(order, group, 0, REG_BINARY, (LPBYTE) buf, 4 * (n + 1))
        == ERROR_SUCCESS;
}

/* One more than any tag in use in this group, by a service or the order list. */
static DWORD
freetag(char *group)
{
    HKEY svcs, k, order;
    char name[256], g[256];
    DWORD i, t, max = 0, tags[MAXTAGS], n;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SERVICES, 0, KEY_READ, &svcs) == ERROR_SUCCESS) {
        for (i = 0; RegEnumKey(svcs, i, name, sizeof(name)) == ERROR_SUCCESS; i++) {
            if (lstrcmpi(name, VMM_SERVICE) == 0)
                continue;
            if (RegOpenKeyEx(svcs, name, 0, KEY_READ, &k) != ERROR_SUCCESS)
                continue;
            if (getstr(k, "Group", g, sizeof(g)) && lstrcmpi(g, group) == 0
                && getdword(k, "Tag", &t) && t > max)
                max = t;
            RegCloseKey(k);
        }
        RegCloseKey(svcs);
    }
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, ORDERKEY, 0, KEY_READ, &order) == ERROR_SUCCESS) {
        n = readorder(order, group, tags);
        for (i = 0; i < n; i++)
            if (tags[i] > max)
                max = tags[i];
        RegCloseKey(order);
    }
    return max + 1;
}

/* Put tag right after i8042prt's tag in the group's load order. */
static BOOL
addorder(char *group, DWORD i8042tag, DWORD tag)
{
    HKEY order;
    DWORD tags[MAXTAGS], out[MAXTAGS + 2], n, m = 0, i, disp;
    BOOL placed = FALSE, ok;

    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, ORDERKEY, 0, "", REG_OPTION_NON_VOLATILE,
                       KEY_ALL_ACCESS, NULL, &order, &disp) != ERROR_SUCCESS)
        return FALSE;
    n = readorder(order, group, tags);
    for (i = 0; i < n && m < MAXTAGS; i++) {
        if (tags[i] == tag)
            continue;
        out[m++] = tags[i];
        if (tags[i] == i8042tag && !placed) {
            out[m++] = tag;
            placed = TRUE;
        }
    }
    if (!placed) {
        out[m++] = i8042tag;
        out[m++] = tag;
    }
    ok = writeorder(order, group, out, m);
    RegCloseKey(order);
    return ok;
}

static void
droporder(char *group, DWORD tag)
{
    HKEY order;
    DWORD tags[MAXTAGS], out[MAXTAGS], n, m = 0, i;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, ORDERKEY, 0, KEY_ALL_ACCESS, &order) != ERROR_SUCCESS)
        return;
    n = readorder(order, group, tags);
    for (i = 0; i < n; i++)
        if (tags[i] != tag)
            out[m++] = tags[i];
    if (m != n)
        writeorder(order, group, out, m);
    RegCloseKey(order);
}

/* RegDeleteKey on NT will not delete a key that has subkeys. */
static LONG
delkey(HKEY parent, char *name)
{
    HKEY k;
    char sub[256];
    LONG err;

    err = RegOpenKeyEx(parent, name, 0, KEY_ALL_ACCESS, &k);
    if (err != ERROR_SUCCESS)
        return err;
    while (RegEnumKey(k, 0, sub, sizeof(sub)) == ERROR_SUCCESS)
        if (delkey(k, sub) != ERROR_SUCCESS)
            break;
    RegCloseKey(k);
    return RegDeleteKey(parent, name);
}

static void
driverpath(char *dst)
{
    GetSystemDirectory(dst, MAX_PATH - 32);
    strcat(dst, "\\drivers\\" VMM_DRIVER);
}

/* ------------------------------------------------------------------ */
/* is this user an administrator? (works on NT 3.1 through 4.0)       */
/* ------------------------------------------------------------------ */

static BOOL
is_admin(void)
{
    HANDLE tok;
    PTOKEN_GROUPS grp;
    DWORD len = 0, i;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins;
    BOOL found = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
        return FALSE;
    GetTokenInformation(tok, TokenGroups, NULL, 0, &len);
    if (len == 0) {
        CloseHandle(tok);
        return FALSE;
    }
    grp = (PTOKEN_GROUPS) GlobalAlloc(GPTR, len);
    if (grp == NULL) {
        CloseHandle(tok);
        return FALSE;
    }
    if (GetTokenInformation(tok, TokenGroups, grp, len, &len)
        && AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                    DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        for (i = 0; i < grp->GroupCount; i++)
            if (EqualSid(grp->Groups[i].Sid, admins)) {
                found = TRUE;
                break;
            }
        FreeSid(admins);
    }
    GlobalFree(grp);
    CloseHandle(tok);
    return found;
}

/* ------------------------------------------------------------------ */
/* write the embedded driver to a file                                */
/* ------------------------------------------------------------------ */

static BOOL
write_bytes(const char *path, LPVOID data, DWORD sz)
{
    HANDLE f;
    DWORD w;

    f = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                   FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!WriteFile(f, data, sz, &w, NULL) || w != sz) {
        CloseHandle(f);
        DeleteFile(path);
        return FALSE;
    }
    CloseHandle(f);
    return TRUE;
}

/* Copy the embedded driver to dst. If dst is in use, write dst.new and ask
 * the system to put it in place at the next restart. *delayed says which. */
static BOOL
extract_driver(const char *dst, BOOL *delayed)
{
    HRSRC h;
    HGLOBAL g;
    LPVOID p;
    DWORD sz;
    char tmp[MAX_PATH];

    *delayed = FALSE;
    h = FindResource(g_hInst, MAKEINTRESOURCE(IDR_DRIVER), "VMMDRIVER");
    if (h == NULL)
        return FALSE;
    sz = SizeofResource(g_hInst, h);
    g = LoadResource(g_hInst, h);
    if (g == NULL || sz == 0)
        return FALSE;
    p = LockResource(g);
    if (p == NULL)
        return FALSE;

    if (write_bytes(dst, p, sz))
        return TRUE;

    /* dst is in use (the old driver is loaded): replace it at restart */
    strcpy(tmp, dst);
    strcpy(tmp + strlen(tmp) - 3, "new");
    if (!write_bytes(tmp, p, sz))
        return FALSE;
    if (!MoveFileEx(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_DELAY_UNTIL_REBOOT))
        return FALSE;
    *delayed = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* install / remove / status                                          */
/* ------------------------------------------------------------------ */

static int
do_install(char *msg)
{
    char dst[MAX_PATH], group[256];
    HKEY svc, i8042;
    DWORD disp, tag = 0, i8042tag = 0, start;
    BOOL hastag, delayed;
    LONG err;

    /* i8042prt decides where we load */
    err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, I8042KEY, 0, KEY_READ, &i8042);
    if (err != ERROR_SUCCESS) {
        strcpy(msg, "i8042prt (the PS/2 keyboard and mouse driver) is not "
                    "installed, so there is no mouse port to attach to.");
        return R_FAIL;
    }
    if (!getstr(i8042, "Group", group, sizeof(group))) {
        RegCloseKey(i8042);
        strcpy(msg, "i8042prt has no load group.");
        return R_FAIL;
    }
    hastag = getdword(i8042, "Tag", &i8042tag);
    if (!getdword(i8042, "Start", &start) || start > 1)
        start = 1;                      /* load as a system driver by default */
    RegCloseKey(i8042);

    /* write the driver out of our resource */
    driverpath(dst);
    if (!extract_driver(dst, &delayed)) {
        wsprintf(msg, "Could not write %s (error %lu).", dst, GetLastError());
        return R_FAIL;
    }

    /* the service key */
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, SVCKEY, 0, "", REG_OPTION_NON_VOLATILE,
                       KEY_ALL_ACCESS, NULL, &svc, &disp) != ERROR_SUCCESS) {
        strcpy(msg, "Could not create the service key. "
                    "Log on as Administrator.");
        return R_NOADMIN;
    }
    setdword(svc, "Type", 1);           /* kernel driver */
    setdword(svc, "Start", start);
    setdword(svc, "ErrorControl", 0);   /* if it fails, NT starts anyway */
    if (RegSetValueEx(svc, "Group", 0, REG_SZ, (LPBYTE) group, strlen(group) + 1)
        != ERROR_SUCCESS) {
        RegCloseKey(svc);
        strcpy(msg, "Could not write the service key.");
        return R_FAIL;
    }
    if (hastag) {
        if (!getdword(svc, "Tag", &tag) || tag == 0 || tag == i8042tag)
            tag = freetag(group);
        setdword(svc, "Tag", tag);
    } else {
        RegDeleteValue(svc, "Tag");     /* group loads in name order, i8042prt first */
    }
    RegDeleteValue(svc, "State");
    RegCloseKey(svc);

    if (hastag)
        addorder(group, i8042tag, tag);

    if (delayed)
        strcpy(msg, "Installed. The old driver was in use, so the new one "
                    "will take its place when you restart.\n\n"
                    "Restart Windows NT to start the seamless mouse.");
    else
        strcpy(msg, "Installed.\n\nRestart Windows NT to start the "
                    "seamless mouse.");
    return R_OK;
}

static int
do_remove(char *msg)
{
    char dst[MAX_PATH], group[256];
    HKEY svc;
    DWORD tag;
    BOOL hasgroup, hastag, changed = FALSE;
    LONG err;

    err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SVCKEY, 0, KEY_READ, &svc);
    if (err == ERROR_SUCCESS) {
        hasgroup = getstr(svc, "Group", group, sizeof(group));
        hastag = getdword(svc, "Tag", &tag);
        RegCloseKey(svc);
        err = delkey(HKEY_LOCAL_MACHINE, SVCKEY);
        if (err == ERROR_ACCESS_DENIED) {
            strcpy(msg, "Could not remove the service. Log on as Administrator.");
            return R_NOADMIN;
        }
        if (err != ERROR_SUCCESS) {
            strcpy(msg, "Could not delete the service key.");
            return R_FAIL;
        }
        if (hasgroup && hastag)
            droporder(group, tag);
        changed = TRUE;
    } else if (err != ERROR_FILE_NOT_FOUND) {
        strcpy(msg, "Could not open the service key.");
        return R_FAIL;
    }

    driverpath(dst);
    if (GetFileAttributes(dst) != 0xFFFFFFFF) {
        changed = TRUE;
        if (!DeleteFile(dst))
            MoveFileEx(dst, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    if (!changed) {
        strcpy(msg, "VMMouse is not installed. Nothing to remove.");
        return R_NOOP;
    }
    strcpy(msg, "Removed.\n\nRestart Windows NT to finish removing it.");
    return R_OK;
}

/* a short line about the current state, for the window and for /status */
static void
do_status(char *msg)
{
    char dst[MAX_PATH], state[512];
    HKEY svc;
    BOOL present, installed;

    driverpath(dst);
    present = GetFileAttributes(dst) != 0xFFFFFFFF;
    installed = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SVCKEY, 0, KEY_READ, &svc)
                == ERROR_SUCCESS;

    if (!installed && !present) {
        strcpy(msg, "Not installed.");
        return;
    }
    if (installed) {
        if (getstr(svc, "State", state, sizeof(state)))
            wsprintf(msg, "%s", state);     /* the driver's own words */
        else
            strcpy(msg, "Installed. Restart Windows NT to start it.");
        RegCloseKey(svc);
    } else {
        strcpy(msg, "The driver file is present but the service is not "
                    "installed.");
    }
}

/* ------------------------------------------------------------------ */
/* restart                                                            */
/* ------------------------------------------------------------------ */

static void
reboot_now(void)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;

    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        if (LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(tok);
    }
    ExitWindowsEx(EWX_REBOOT, 0);
}

/* After a successful install or remove, offer to restart. */
static void
offer_restart(HWND owner)
{
    if (MessageBox(owner, "Restart Windows NT now?", "VMMouse Setup",
                   MB_YESNO | MB_ICONQUESTION) == IDYES)
        reboot_now();
}

/* ------------------------------------------------------------------ */
/* the dialog                                                         */
/* ------------------------------------------------------------------ */

/* Center the dialog on the screen. NT 3.1 has no DS_CENTER, so do it here;
 * this works on every NT version. */
static void
center_dialog(HWND hDlg)
{
    RECT rc;
    int w, h, sw, sh;

    GetWindowRect(hDlg, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hDlg, NULL, (sw - w) / 2, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void
refresh(HWND hDlg)
{
    char msg[512];
    BOOL admin = is_admin();
    BOOL installed = FALSE;
    HKEY svc;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SVCKEY, 0, KEY_READ, &svc) == ERROR_SUCCESS) {
        installed = TRUE;
        RegCloseKey(svc);
    }

    do_status(msg);
    if (!admin) {
        strcat(msg, "\n\nLog on as Administrator to install or remove.");
    }
    SetDlgItemText(hDlg, IDC_STATUS, msg);
    EnableWindow(GetDlgItem(hDlg, IDC_INSTALL), admin);
    EnableWindow(GetDlgItem(hDlg, IDC_REMOVE), admin && installed);
}

static void
on_action(HWND hDlg, int which)
{
    char msg[512];
    int r;

    r = (which == IDC_INSTALL) ? do_install(msg) : do_remove(msg);
    MessageBox(hDlg, msg, "VMMouse Setup",
               r == R_OK ? MB_ICONINFORMATION :
               r == R_NOOP ? MB_ICONINFORMATION : MB_ICONEXCLAMATION);
    refresh(hDlg);
    if (r == R_OK)
        offer_restart(hDlg);
}

static BOOL CALLBACK
DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        /* The ICON control shows the mouse, and the shell uses the .exe's
         * icon resource. NT 3.1 has no WM_SETICON, so nothing to do there. */
        center_dialog(hDlg);
        refresh(hDlg);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_INSTALL:
        case IDC_REMOVE:
            on_action(hDlg, LOWORD(wParam));
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* command line                                                       */
/* ------------------------------------------------------------------ */

/* does the command line hold /word or -word (case-insensitive)? */
static BOOL
has_opt(LPSTR cmd, char *word)
{
    char *p;

    for (p = cmd; *p; p++) {
        if ((*p == '/' || *p == '-') && lstrcmpi(p + 1, word) == 0)
            return TRUE;
        if ((*p == '/' || *p == '-')) {
            char *q = p + 1;
            int n = lstrlen(word);
            if (_strnicmp(q, word, n) == 0 && (q[n] == 0 || q[n] == ' '
                                               || q[n] == '/' || q[n] == '-'))
                return TRUE;
        }
    }
    return FALSE;
}

int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    char msg[512];
    BOOL quiet;
    int r;

    g_hInst = hInst;
    quiet = has_opt(lpCmd, "q");

    if (has_opt(lpCmd, "install") || has_opt(lpCmd, "remove")) {
        if (!is_admin()) {
            if (!quiet)
                MessageBox(NULL, "Log on as Administrator to install or remove.",
                           "VMMouse Setup", MB_ICONEXCLAMATION);
            return R_NOADMIN;
        }
        r = has_opt(lpCmd, "install") ? do_install(msg) : do_remove(msg);
        if (!quiet) {
            MessageBox(NULL, msg, "VMMouse Setup",
                       r == R_OK ? MB_ICONINFORMATION : MB_ICONEXCLAMATION);
            if (r == R_OK)
                offer_restart(NULL);
        }
        return r;
    }

    if (has_opt(lpCmd, "status")) {
        do_status(msg);
        if (!quiet)
            MessageBox(NULL, msg, "VMMouse Setup", MB_ICONINFORMATION);
        return R_OK;
    }

    /* no command: show the window */
    DialogBox(hInst, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);
    return R_OK;
}
