#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

#include "EDID_INFO.h"

#pragma comment(lib, "Advapi32.lib")

using namespace std;


//------------------------------------------------
// 打印EDID HEX
//------------------------------------------------
void PrintHex(const vector<BYTE>& edid)
{
    for (size_t i = 0; i < edid.size(); i++)
    {
        cout
            << hex
            << setw(2)
            << setfill('0')
            << (int)edid[i]
            << " ";

        if ((i + 1) % 16 == 0)
            cout << endl;
    }

    cout << dec << endl;
}


void SaveEDIDTxt(
    const vector<BYTE>& edid,
    const string& path)
{
    ofstream file(path);

    if (!file.is_open())
    {
        cout << "Create " << path << " failed\n";
        return;
    }


    for (size_t i = 0; i < edid.size(); i++)
    {
        file
            << "0x"
            << uppercase
            << hex
            << setw(2)
            << setfill('0')
            << (int)edid[i];


        if (i != edid.size() - 1)
        {
            file << ",";
        }


        // 每16个数据换行
        if ((i + 1) % 16 == 0)
        {
            file << "\n";
        }
        else
        {
            file << " ";
        }
    }


    file.close();


    cout << "Save " << path << " OK\n";
}

//------------------------------------------------
// 读取EDID
//------------------------------------------------
bool ReadEDID(
    HKEY hKey,
    vector<BYTE>& edid)
{
    DWORD type;
    DWORD size = 0;


    // 获取实际长度
    if (RegQueryValueExA(
        hKey,
        "EDID",
        NULL,
        &type,
        NULL,
        &size)
        != ERROR_SUCCESS)
    {
        return false;
    }


    vector<BYTE> buffer(size);


    if (RegQueryValueExA(
        hKey,
        "EDID",
        NULL,
        &type,
        buffer.data(),
        &size)
        != ERROR_SUCCESS)
    {
        return false;
    }


    edid = buffer;


    return true;
}



//------------------------------------------------
// 输出 EDID
//------------------------------------------------
void ShowEDID(
    const vector<BYTE>& edid,
    const string& monitorInfo,
    const string& txtPath)
{
    cout
        << "\n============================\n";

    if (!monitorInfo.empty())
        cout << monitorInfo;

    cout
        << "EDID Size : "
        << edid.size()
        << " Bytes\n";


    //解析
    PrintEDIDInfo(edid);


    //保存TXT
    SaveEDIDTxt(edid, txtPath);


    //打印HEX
    cout
        << "\n----------- EDID HEX -----------\n";

    PrintHex(edid);
}



//------------------------------------------------
// 显示器信息
//------------------------------------------------
struct ActiveMonitor
{
    string deviceName;     // \\.\DISPLAY1\Monitor0
    string instanceId;     // DISPLAY\GBT2761\4&...
    string interfaceType;  // HDMI / DisplayPort / ...
    vector<BYTE> edid;
};


static string WideToAnsi(const wchar_t* w)
{
    int len = WideCharToMultiByte(
        CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);

    if (len <= 1)
        return "";

    string s(len - 1, '\0');

    WideCharToMultiByte(
        CP_ACP, 0, w, -1, &s[0], len, NULL, NULL);

    return s;
}


static char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');

    return c;
}


static bool EqualNoCase(const string& a, const string& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++)
    {
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    }

    return true;
}


static const char* OutputTechnologyName(
    DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech)
{
    switch (tech)
    {
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15:
        return "VGA";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO:
        return "S-Video";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO:
        return "Composite";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO:
        return "Component";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:
        return "DVI";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:
        return "HDMI";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:
        return "LVDS";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:
        return "DisplayPort";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
        return "eDP";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
        return "Internal";

    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:
        return "Miracast";

    default:
        return "Unknown";
    }
}


//------------------------------------------------
// 通过 QueryDisplayConfig 查询当前接口类型
//------------------------------------------------
static string GetInterfaceType(const string& monitorPath)
{
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;

    if (GetDisplayConfigBufferSizes(
        QDC_ONLY_ACTIVE_PATHS,
        &pathCount,
        &modeCount)
        != ERROR_SUCCESS)
    {
        return "";
    }

    vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (QueryDisplayConfig(
        QDC_ONLY_ACTIVE_PATHS,
        &pathCount,
        paths.data(),
        &modeCount,
        modes.data(),
        NULL)
        != ERROR_SUCCESS)
    {
        return "";
    }


    for (UINT32 i = 0; i < pathCount; i++)
    {
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};

        target.header.type =
            DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId =
            paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&target.header)
            != ERROR_SUCCESS)
        {
            continue;
        }

        string targetPath =
            WideToAnsi(target.monitorDevicePath);

        if (EqualNoCase(targetPath, monitorPath))
            return OutputTechnologyName(target.outputTechnology);
    }


    return "";
}


//------------------------------------------------
// 枚举所有当前活动显示器
//------------------------------------------------
static void EnumerateActiveMonitors(
    vector<ActiveMonitor>& monitors)
{
    DISPLAY_DEVICEA adapter;
    adapter.cb = sizeof(adapter);

    for (DWORD adapterIndex = 0;
         EnumDisplayDevicesA(
             NULL,
             adapterIndex,
             &adapter,
             0);
         adapterIndex++)
    {
        adapter.cb = sizeof(adapter);

        if ((adapter.StateFlags &
             DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
        {
            continue;
        }


        DISPLAY_DEVICEA monitor;
        monitor.cb = sizeof(monitor);

        for (DWORD monitorIndex = 0;
             EnumDisplayDevicesA(
                 adapter.DeviceName,
                 monitorIndex,
                 &monitor,
                 EDD_GET_DEVICE_INTERFACE_NAME);
             monitorIndex++)
        {
            monitor.cb = sizeof(monitor);

            if ((monitor.StateFlags &
                 DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            {
                continue;
            }

            if (monitor.StateFlags &
                DISPLAY_DEVICE_MIRRORING_DRIVER)
            {
                continue;
            }


            // \\?\DISPLAY#GBT2761#4&2678e3df&0&UID4145#{...}
            string interfacePath = monitor.DeviceID;
            string id = interfacePath;

            if (id.rfind("\\\\?\\", 0) == 0)
                id.erase(0, 4);

            size_t guid = id.find("#{");

            if (guid != string::npos)
                id.erase(guid);

            for (char& c : id)
            {
                if (c == '#')
                    c = '\\';
            }


            string regPath =
                "SYSTEM\\CurrentControlSet\\Enum\\" +
                id +
                "\\Device Parameters";


            HKEY hKey;

            if (RegOpenKeyExA(
                HKEY_LOCAL_MACHINE,
                regPath.c_str(),
                0,
                KEY_READ,
                &hKey)
                != ERROR_SUCCESS)
            {
                continue;
            }


            vector<BYTE> edid;
            bool ok = ReadEDID(hKey, edid);

            RegCloseKey(hKey);

            if (!ok || edid.empty())
                continue;


            ActiveMonitor info;

            info.deviceName = monitor.DeviceName;
            info.instanceId = id;
            info.interfaceType = GetInterfaceType(interfacePath);
            info.edid = edid;

            monitors.push_back(info);
        }
    }
}



//------------------------------------------------
// 扫描显示器
//------------------------------------------------
void ScanDisplay()
{
    vector<ActiveMonitor> monitors;

    EnumerateActiveMonitors(monitors);


    // 笔记本内建屏优先显示
    stable_sort(
        monitors.begin(),
        monitors.end(),
        [](const ActiveMonitor& a, const ActiveMonitor& b)
        {
            auto isInternal = [](const string& type)
            {
                return type == "Internal" || type == "eDP";
            };

            return isInternal(a.interfaceType) &&
                   !isInternal(b.interfaceType);
        });


    // 输出所有活动显示器
    if (!monitors.empty())
    {
        for (size_t i = 0; i < monitors.size(); i++)
        {
            const ActiveMonitor& m = monitors[i];

            string monitorInfo;

            monitorInfo += "Monitor      : ";
            monitorInfo += m.deviceName;
            monitorInfo += "\n";

            monitorInfo += "Monitor ID   : ";
            monitorInfo += m.instanceId;
            monitorInfo += "\n";

            monitorInfo += "Interface    : ";

            if (m.interfaceType.empty())
                monitorInfo += "Unknown";
            else
                monitorInfo += m.interfaceType;

            monitorInfo += "\n";

            monitorInfo += "Resolution   : ";
            monitorInfo += GetPreferredResolution(m.edid);
            monitorInfo += "\n";


            string txtPath = (monitors.size() > 1)
                ? "EDID_" + to_string(i + 1) + ".txt"
                : "EDID.txt";

            ShowEDID(m.edid, monitorInfo, txtPath);
        }

        return;
    }


    cout
        << "No active monitor EDID found, "
        << "fallback to registry scan...\n";


    HKEY hDisplay;


    if (RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Enum\\DISPLAY",
        0,
        KEY_READ,
        &hDisplay)
        != ERROR_SUCCESS)
    {
        cout << "Open DISPLAY failed\n";
        return;
    }


    vector<BYTE> maxEDID;


    DWORD vendorIndex = 0;
    char vendor[256];


    while (true)
    {
        DWORD vendorLen = sizeof(vendor);


        if (RegEnumKeyExA(
            hDisplay,
            vendorIndex,
            vendor,
            &vendorLen,
            NULL,
            NULL,
            NULL,
            NULL)
            != ERROR_SUCCESS)
        {
            break;
        }


        HKEY hVendor;


        if (RegOpenKeyExA(
            hDisplay,
            vendor,
            0,
            KEY_READ,
            &hVendor)
            == ERROR_SUCCESS)
        {
            DWORD devIndex = 0;
            char device[256];


            while (true)
            {
                DWORD deviceLen = sizeof(device);


                if (RegEnumKeyExA(
                    hVendor,
                    devIndex,
                    device,
                    &deviceLen,
                    NULL,
                    NULL,
                    NULL,
                    NULL)
                    != ERROR_SUCCESS)
                {
                    break;
                }


                string path =
                    "SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\";

                path += vendor;
                path += "\\";
                path += device;
                path += "\\Device Parameters";


                HKEY hEDID;


                if (RegOpenKeyExA(
                    HKEY_LOCAL_MACHINE,
                    path.c_str(),
                    0,
                    KEY_READ,
                    &hEDID)
                    == ERROR_SUCCESS)
                {
                    vector<BYTE> data;


                    if (ReadEDID(
                        hEDID,
                        data))
                    {
                        // 保留最大EDID
                        if (data.size() > maxEDID.size())
                        {
                            maxEDID = data;
                        }
                    }


                    RegCloseKey(hEDID);
                }


                devIndex++;
            }


            RegCloseKey(hVendor);
        }


        vendorIndex++;
    }


    RegCloseKey(hDisplay);


    if (!maxEDID.empty())
        ShowEDID(maxEDID, "", "EDID.txt");
}



//------------------------------------------------
// Main
//------------------------------------------------
int main()
{

    cout
        << "Scan Monitor EDID...\n";


    ScanDisplay();


    cout
        << "\nPress any key to exit...";


    getchar();


    return 0;
}