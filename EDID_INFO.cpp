#include "EDID_INFO.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>

using namespace std;


//------------------------------------------------
// EDID 辅助函数
//------------------------------------------------
static string DescText(const BYTE* d)
{
    string s;

    for (int j = 0; j < 13; j++)
    {
        BYTE c = d[5 + j];

        if (c == 0x0A)
            break;

        s += (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
    }

    while (!s.empty() && s.back() == ' ')
        s.pop_back();

    return s;
}


static string Fmt(double value, int precision)
{
    ostringstream oss;

    oss << fixed << setprecision(precision) << value;

    return oss.str();
}


static bool ChecksumOK(const BYTE* block)
{
    int sum = 0;

    for (int i = 0; i < 128; i++)
        sum += block[i];

    return (sum & 0xFF) == 0;
}


//------------------------------------------------
// Established Timings (0x23-0x25)
//------------------------------------------------
static void PrintEstablishedTimings(const vector<BYTE>& e)
{
    static const char* modes[] = {
        "720x400@70",  "720x400@88",  "640x480@60",  "640x480@67",
        "640x480@72",  "640x480@75",  "800x600@56",  "800x600@60",
        "800x600@72",  "800x600@75",  "832x624@75",  "1024x768i@87",
        "1024x768@60", "1024x768@72", "1024x768@75", "1280x1024@75",
        "1152x870@75"
    };

    DWORD bits =
        ((DWORD)e[35] << 16) |
        ((DWORD)e[36] << 8) |
        e[37];

    cout << "Established  :";

    bool any = false;

    for (int i = 0; i < 17; i++)
    {
        if (bits & (1u << (23 - i)))
        {
            cout << " " << modes[i];
            any = true;
        }
    }

    if (bits & 0x007F)
    {
        cout << " +Manufacturer modes";
        any = true;
    }

    if (!any)
        cout << " None";

    cout << endl;
}


//------------------------------------------------
// Standard Timings (0x26-0x35)
//------------------------------------------------
static string StandardTimingText(BYTE b0, BYTE b1)
{
    if (b0 == 0x00 || (b0 == 0x01 && b1 == 0x01))
        return "";

    static const int arWidth[]  = { 16, 4, 5, 16 };
    static const int arHeight[] = { 10, 3, 4, 9 };
    static const char* arName[] = { "16:10", "4:3", "5:4", "16:9" };

    int width   = (b0 + 31) * 8;
    int ratio   = (b1 >> 6) & 0x03;
    int height  = width * arHeight[ratio] / arWidth[ratio];
    int refresh = (b1 & 0x3F) + 60;

    ostringstream oss;

    oss << width << "x" << height << "@" << refresh
        << "Hz(" << arName[ratio] << ")";

    return oss.str();
}


static void PrintStandardTimings(const BYTE* data, int count)
{
    cout << "Standard     :";

    bool any = false;

    for (int i = 0; i < count; i++)
    {
        string t = StandardTimingText(data[i * 2], data[i * 2 + 1]);

        if (!t.empty())
        {
            cout << " " << t;
            any = true;
        }
    }

    if (!any)
        cout << " None";

    cout << endl;
}


//------------------------------------------------
// Detailed Timing Descriptor
//------------------------------------------------
static void PrintDetailedTiming(const BYTE* d, const char* label)
{
    int clock10k = d[0] | (d[1] << 8);

    int hActive = d[2] | ((d[4] & 0xF0) << 4);
    int hBlank  = d[3] | ((d[4] & 0x0F) << 8);
    int vActive = d[5] | ((d[7] & 0xF0) << 4);
    int vBlank  = d[6] | ((d[7] & 0x0F) << 8);
    int hTotal  = hActive + hBlank;
    int vTotal  = vActive + vBlank;

    int widthMm  = d[12] | ((d[14] & 0xF0) << 4);
    int heightMm = d[13] | ((d[14] & 0x0F) << 8);

    BYTE misc = d[17];
    bool interlaced = (misc & 0x80) != 0;

    // 交错模式: DTD 中垂直行数是每场的行数, 显示时按完整帧换算
    int vActiveDisplay = interlaced ? vActive * 2 : vActive;
    int vTotalDisplay  = interlaced ? vTotal * 2 : vTotal;
    int vBlankDisplay  = interlaced ? vBlank * 2 : vBlank;

    static const char* syncTypes[] = {
        "Analog composite", "Bipolar analog composite",
        "Digital composite", "Digital separate"
    };

    double refresh = 0.0;

    if (hTotal > 0 && vTotal > 0)
        refresh = (double)clock10k * 10000.0 / ((double)hTotal * vTotal);

    cout << label << ": "
         << hActive << "x" << vActiveDisplay << (interlaced ? "i" : "p")
         << " @ " << Fmt(refresh, 2) << "Hz"
         << ", clock " << Fmt(clock10k / 100.0, 2) << "MHz"
         << ", total " << hTotal << "x" << vTotalDisplay
         << ", blank H " << hBlank << " V " << vBlankDisplay;

    if (widthMm && heightMm)
        cout << ", " << widthMm << "x" << heightMm << "mm";

    cout << ", " << syncTypes[(misc >> 3) & 0x03];

    if (((misc >> 3) & 0x03) == 3)
        cout << " (H" << ((misc & 0x02) ? "+" : "-")
             << " V" << ((misc & 0x04) ? "+" : "-") << ")";

    cout << endl;
}


//------------------------------------------------
// 首选(主要)分辨率
//------------------------------------------------
string GetPreferredResolution(const vector<BYTE>& edid)
{
    if (edid.size() < 128)
        return "Unknown";

    for (int i = 54; i <= 108; i += 18)
    {
        const BYTE* d = &edid[i];

        // 跳过 Display Descriptor, 只取 Detailed Timing Descriptor
        if (d[0] == 0x00 && d[1] == 0x00)
            continue;

        int clock10k = d[0] | (d[1] << 8);

        if (clock10k == 0)
            continue;

        int hActive = d[2] | ((d[4] & 0xF0) << 4);
        int vActive = d[5] | ((d[7] & 0xF0) << 4);

        bool interlaced = (d[17] & 0x80) != 0;

        int vDisplay = interlaced ? vActive * 2 : vActive;

        ostringstream oss;

        oss << hActive << "x" << vDisplay;

        return oss.str();
    }

    return "Unknown";
}


//------------------------------------------------
// Monitor Range Limits Descriptor (0xFD)
//------------------------------------------------
static void PrintRangeLimits(const BYTE* d, const char* label)
{
    int flags = d[4];
    int maxV = d[6] + ((flags & 0x03) * 255);
    int maxH = d[8] + (((flags >> 2) & 0x03) * 255);

    const char* timing = "Unknown";

    switch (d[10])
    {
    case 0x00: timing = "Default GTF";   break;
    case 0x01: timing = "No timing";     break;
    case 0x02: timing = "Secondary GTF"; break;
    case 0x04: timing = "CVT";           break;
    }

    cout << label << ": Range Limits "
         << (int)d[5] << "-" << maxV << "Hz(V), "
         << (int)d[7] << "-" << maxH << "kHz(H), "
         << (int)d[9] * 10 << "MHz max pixel clock, "
         << timing << endl;
}


//------------------------------------------------
// Display Descriptor
//------------------------------------------------
static void PrintDisplayDescriptor(const BYTE* d, const char* label)
{
    switch (d[3])
    {
    case 0xFC:
        cout << label << ": Monitor Name \"" << DescText(d) << "\"" << endl;
        break;

    case 0xFF:
        cout << label << ": Serial Number \"" << DescText(d) << "\"" << endl;
        break;

    case 0xFE:
        cout << label << ": Text \"" << DescText(d) << "\"" << endl;
        break;

    case 0xFD:
        PrintRangeLimits(d, label);
        break;

    case 0xFA:
    {
        cout << label << ": Additional Standard Timings:";

        bool any = false;

        for (int i = 0; i < 6; i++)
        {
            string t = StandardTimingText(d[5 + i * 2], d[6 + i * 2]);

            if (!t.empty())
            {
                cout << " " << t;
                any = true;
            }
        }

        if (!any)
            cout << " None";

        cout << endl;
        break;
    }

    case 0xFB: cout << label << ": Additional White Point" << endl;               break;
    case 0xF9: cout << label << ": Display Color Management" << endl;            break;
    case 0xF8: cout << label << ": CVT 3-Byte Timing Codes" << endl;              break;
    case 0xF7: cout << label << ": Additional Standard Timings (type 3)" << endl;  break;
    case 0x10: cout << label << ": Dummy" << endl;                                break;

    default:
        cout << label << ": "
             << (d[3] <= 0x0F ? "Manufacturer Reserved" : "Unknown tag")
             << " 0x"
             << hex << setw(2) << setfill('0') << (int)d[3]
             << dec << setfill(' ') << endl;
        break;
    }
}


//------------------------------------------------
// Extension Blocks
//------------------------------------------------
static const char* ExtensionName(BYTE tag)
{
    switch (tag)
    {
    case 0x00: return "Reserved";
    case 0x01: return "Timing Extension (obsolete)";
    case 0x02: return "CEA-861";
    case 0x10: return "VTB-EXT";
    case 0x40: return "DI-EXT";
    case 0x50: return "LS-EXT";
    case 0x60: return "MI-EXT";
    case 0x70: return "DisplayID";
    case 0xF0: return "Block Map";
    case 0xFF: return "Manufacturer-Specific";
    default:   return "Unknown";
    }
}


static const char* CeaVicName(int vic)
{
    switch (vic)
    {
    case 1:   return "640x480p@60";
    case 2:   return "720x480p@60(4:3)";
    case 3:   return "720x480p@60(16:9)";
    case 4:   return "1280x720p@60";
    case 5:   return "1920x1080i@60";
    case 6:   return "720(1440)x480i@60";
    case 7:   return "720(1440)x480i@60(16:9)";
    case 14:  return "1440x480p@60";
    case 15:  return "1440x576p@50";
    case 16:  return "1920x1080p@60";
    case 17:  return "720x576p@50";
    case 18:  return "720x576p@50(16:9)";
    case 19:  return "1280x720p@50";
    case 20:  return "1920x1080i@50";
    case 21:  return "720(1440)x576i@50";
    case 22:  return "720(1440)x576i@50(16:9)";
    case 31:  return "1920x1080p@50";
    case 32:  return "1920x1080p@24";
    case 33:  return "1920x1080p@25";
    case 34:  return "1920x1080p@30";
    case 63:  return "1920x1080p@120";
    case 64:  return "3840x2160p@60";
    case 65:  return "3840x2160p@50";
    case 93:  return "3840x2160p@120";
    case 97:  return "3840x2160p@144";
    case 107: return "3840x2160p@240";
    default:  return nullptr;
    }
}


static const char* AudioFormatName(int format)
{
    static const char* names[] = {
        "Reserved", "LPCM", "AC-3", "MPEG-1", "MP3", "MPEG-2",
        "AAC-LC", "DTS", "ATRAC", "One Bit Audio", "E-AC-3",
        "DTS-HD", "MAT", "DST", "WMA Pro", "Extended"
    };

    return names[format & 0x0F];
}


static void PrintCeaDTDs(const BYTE* block)
{
    int dtdOffset = block[2];

    if (dtdOffset < 4)
        return;

    for (int pos = dtdOffset; pos + 18 <= 127; pos += 18)
    {
        if (block[pos] == 0x00 && block[pos + 1] == 0x00)
            break;

        PrintDetailedTiming(block + pos, "    DTD");
    }
}


static void PrintCea861(const BYTE* block)
{
    BYTE flags = block[3];
    int dtdOffset = block[2];

    cout << "    CEA-861 rev " << (int)block[1]
         << ", native DTDs " << (flags & 0x0F)
         << ", underscan " << ((flags & 0x80) ? "yes" : "no")
         << ", basic audio " << ((flags & 0x40) ? "yes" : "no")
         << ", YCbCr 4:4:4 " << ((flags & 0x20) ? "yes" : "no")
         << ", YCbCr 4:2:2 " << ((flags & 0x10) ? "yes" : "no")
         << endl;

    int dataEnd = (dtdOffset >= 4 && dtdOffset <= 127) ? dtdOffset : 127;
    int pos = 4;

    while (pos < dataEnd)
    {
        BYTE header = block[pos];
        int tag = header >> 5;
        int len = header & 0x1F;

        if (pos + 1 + len > dataEnd)
            break;

        const BYTE* p = block + pos + 1;

        switch (tag)
        {
        case 1: // Video Data Block
            cout << "    Video        :";

            for (int i = 0; i < len; i++)
            {
                int vic = p[i] & 0x7F;
                const char* vicName = CeaVicName(vic);

                cout << " ";

                if (vicName)
                    cout << vicName;
                else
                    cout << "VIC" << vic;

                if (p[i] & 0x80)
                    cout << "(native)";
            }

            cout << endl;
            break;

        case 2: // Audio Data Block
            for (int i = 0; i + 2 < len; i += 3)
            {
                static const char* rates[] = {
                    "192", "176.4", "96", "88.2", "48", "44.1", "32"
                };

                static const int depths[] = { 24, 20, 18, 16 };

                int format = (p[i] >> 3) & 0x0F;

                cout << "    Audio        : "
                     << AudioFormatName(format)
                     << ", " << ((p[i] & 0x07) + 1) << "ch, rates:";

                bool any = false;

                for (int k = 0; k < 7; k++)
                {
                    if (p[i + 1] & (1 << (6 - k)))
                    {
                        cout << " " << rates[k];
                        any = true;
                    }
                }

                if (!any)
                    cout << " none";

                cout << "kHz";

                if (format == 1) // LPCM
                {
                    cout << ", depths:";
                    any = false;

                    for (int k = 0; k < 4; k++)
                    {
                        if (p[i + 2] & (1 << (2 - k)))
                        {
                            cout << " " << depths[k];
                            any = true;
                        }
                    }

                    if (!any)
                        cout << " none";

                    cout << "bit";
                }

                cout << endl;
            }
            break;

        case 3: // Speaker Allocation Data Block
            if (len >= 3)
            {
                int mask = p[0] | (p[1] << 8) | (p[2] << 16);

                cout << "    Speakers     : 0x"
                     << hex << setw(6) << setfill('0') << mask
                     << dec << setfill(' ') << endl;
            }
            break;

        case 4: // Vendor-Specific Data Block
            if (len >= 3)
            {
                int oui = p[0] | (p[1] << 8) | (p[2] << 16);

                cout << "    VSDB         : OUI 0x"
                     << hex << setw(6) << setfill('0') << oui
                     << dec << setfill(' ');

                if (oui == 0x000C03 && len >= 5) // HDMI
                {
                    cout << " HDMI, PA "
                         << ((p[3] >> 4) & 0x0F) << "."
                         << (p[3] & 0x0F) << "."
                         << ((p[4] >> 4) & 0x0F) << "."
                         << (p[4] & 0x0F);

                    if (len >= 7 && p[6] > 0)
                        cout << ", max TMDS " << (int)p[6] * 5 << "MHz";
                }

                cout << endl;
            }
            break;

        case 5: // Video Capability Data Block
            cout << "    Video caps   : 0x"
                 << hex << setw(2) << setfill('0')
                 << (len > 0 ? (int)p[0] : 0)
                 << dec << setfill(' ') << endl;
            break;

        case 7: // Extended tag
            if (len >= 1)
            {
                cout << "    Extended     : ";

                switch (p[0])
                {
                case 0:
                    cout << "Video Capability";
                    break;

                case 1:
                    cout << "Vendor-Specific Video";
                    break;

                case 5:
                    cout << "Colorimetry";
                    break;

                case 6:
                    cout << "HDR Static Metadata";

                    if (len >= 2)
                    {
                        cout << " EOTF:";

                        if (p[1] & 0x01) cout << " SDR";
                        if (p[1] & 0x02) cout << " HDR";
                        if (p[1] & 0x04) cout << " ST2084";
                        if (p[1] & 0x08) cout << " HLG";
                    }
                    break;

                case 7:
                    cout << "HDR Dynamic Metadata";
                    break;

                case 13:
                    cout << "Video Format Preference";
                    break;

                case 14:
                    cout << "YCbCr 4:2:0 Video";
                    break;

                case 15:
                    cout << "YCbCr 4:2:0 Capability Map";
                    break;

                default:
                    cout << "Tag " << (int)p[0];
                    break;
                }

                cout << endl;
            }
            break;

        default:
            cout << "    DataBlock    : tag " << tag
                 << ", " << len << " bytes" << endl;
            break;
        }

        pos += 1 + len;
    }

    // DTDs in extension block
    PrintCeaDTDs(block);
}


//------------------------------------------------
// DisplayID Extension Block (0x70)
//------------------------------------------------
static void PrintDisplayID(const BYTE* block)
{
    cout << "    DisplayID version 0x"
         << hex << setw(2) << setfill('0') << (int)block[1]
         << dec << setfill(' ');

    // Realtek 自定义布局: 偏移 8 起为 20 字节一条的 timing
    // 不同批次表头 byte 2 会不同 (0x79 / 0x67 等), 不能写死
    int sectionLen = block[7];

    bool custom =
        block[1] == 0x12 &&
        sectionLen > 0 &&
        sectionLen % 20 == 0 &&
        8 + sectionLen <= 127;

    // 简单校验第一条 timing, 避免把标准 DisplayID 误判成自定义布局
    if (custom)
    {
        const BYTE* t = block + 8;

        int clock10k = t[0] | (t[1] << 8) | (t[2] << 16);
        int hActive = (t[4] | (t[5] << 8)) + 1;
        int vActive = (t[12] | (t[13] << 8)) + 1;

        if (clock10k == 0 ||
            hActive < 320 || hActive > 16384 ||
            vActive < 200 || vActive > 16384)
        {
            custom = false;
        }
    }

    if (!custom)
    {
        cout << endl;
        return;
    }

    int count = sectionLen / 20;

    cout << ", " << count << " timing(s)" << endl;

    for (int i = 0; i < count; i++)
    {
        const BYTE* t = block + 8 + i * 20;

        int clock10k = t[0] | (t[1] << 8) | (t[2] << 16);

        int hActive = (t[4] | (t[5] << 8)) + 1;
        int hBlank  = (t[6] | (t[7] << 8)) + 1;
        int vActive = (t[12] | (t[13] << 8)) + 1;
        int vBlank  = (t[14] | (t[15] << 8)) + 1;

        int hTotal = hActive + hBlank;
        int vTotal = vActive + vBlank;

        double refresh = 0.0;

        if (hTotal > 0 && vTotal > 0)
            refresh = (double)clock10k * 10000.0 / ((double)hTotal * vTotal);

        cout << "    Timing " << (i + 1) << "     : "
             << hActive << "x" << vActive
             << " @ " << Fmt(refresh, 2) << "Hz"
             << ", clock " << Fmt(clock10k / 100.0, 2) << "MHz"
             << ", total " << hTotal << "x" << vTotal
             << ", blank H " << hBlank << " V " << vBlank;

        if (t[3] & 0x80)
            cout << " (native)";

        cout << endl;
    }
}


//------------------------------------------------
// EDID解析
//------------------------------------------------
void PrintEDIDInfo(const vector<BYTE>& edid)
{
    if (edid.size() < 128)
        return;


    // Manufacturer
    WORD id =
        ((WORD)edid[8] << 8) |
        edid[9];


    char manufacturer[4];

    manufacturer[0] =
        ((id >> 10) & 0x1F)
        + 'A' - 1;

    manufacturer[1] =
        ((id >> 5) & 0x1F)
        + 'A' - 1;

    manufacturer[2] =
        (id & 0x1F)
        + 'A' - 1;

    manufacturer[3] = 0;



    // Product ID
    WORD productID =
        edid[10] |
        ((WORD)edid[11] << 8);



    // Serial
    DWORD serial =
        edid[12] |
        ((DWORD)edid[13] << 8) |
        ((DWORD)edid[14] << 16) |
        ((DWORD)edid[15] << 24);



    // Date
    int week = edid[16];
    int year = edid[17] + 1990;



    // Monitor Name / Serial Number Descriptor
    string name = "Unknown";
    string sn = "Unknown";

    for (int i = 54; i <= 108; i += 18)
    {
        if (edid[i] != 0x00 ||
            edid[i + 1] != 0x00 ||
            edid[i + 2] != 0x00)
        {
            continue;
        }

        string text = DescText(&edid[i]);

        if (text.empty())
            continue;

        if (edid[i + 3] == 0xFC)
            name = text;
        else if (edid[i + 3] == 0xFF)
            sn = text;
    }

    cout
        << "\n----------- EDID INFO -----------\n";


    cout
        << "Manufacturer : "
        << manufacturer
        << endl;


    cout
        << "Product ID   : 0x"
        << hex
        << uppercase
        << setw(4)
        << setfill('0')
        << productID
        << dec
        << setfill(' ')
        << endl;


    cout
        << "Serial       : 0x"
        << hex
        << uppercase
        << setw(8)
        << setfill('0')
        << serial
        << dec
        << setfill(' ')
        << endl;

    cout
        << "Date         : Week "
        << week
        << " Year "
        << year
        << endl;


    cout
        << "Name         : "
        << name
        << endl;

    cout
        << "SN           : "
        << sn
        << endl;


    cout
        << "---------------------------------\n";


    // Timings
    cout << "\n----------- TIMINGS -----------\n";

    PrintEstablishedTimings(edid);
    PrintStandardTimings(&edid[38], 8);


    // Descriptors
    cout << "\n----------- DESCRIPTORS -----------\n";

    for (int k = 0; k < 4; k++)
    {
        const BYTE* d = &edid[54 + k * 18];
        string label = "Descriptor " + to_string(k + 1);

        if (d[0] == 0x00 && d[1] == 0x00)
            PrintDisplayDescriptor(d, label.c_str());
        else
            PrintDetailedTiming(d, label.c_str());
    }


    // Extension blocks
    int available = (int)edid.size() / 128;
    int declared = edid[126];
    int extCount = declared;
    int hfCount = -1;

    // HDMI 2.1 HF-EEODB:
    // CEA 扩展块 0x84 处的 E2 78 数据块会携带真实扩展块数量 (0x86)。
    // 这时 base block 的 edid[126] 可能仍为 1。
    if (available >= 2 &&
        edid[128] == 0x02 &&
        (edid[132] & 0xE0) == 0xE0 &&
        edid[133] == 0x78)
    {
        hfCount = edid[134];
        extCount = hfCount;
    }

    if (extCount > available - 1)
        extCount = available - 1;

    if (extCount > 0)
    {
        cout << "\n----------- EXTENSIONS -----------\n";

        if (extCount != declared)
        {
            cout << "Extension count: declared " << declared;

            if (hfCount >= 0)
                cout << ", HF-EEODB " << hfCount;
            else
                cout << ", available " << (available - 1);

            cout << endl;
        }

        for (int e = 1; e <= extCount; e++)
        {
            const BYTE* block = edid.data() + e * 128;

            if (e == 2)
            {
                // 第 2 个扩展块完整输出
                cout << "Extension " << e << " : "
                     << ExtensionName(block[0])
                     << " (tag 0x"
                     << hex << setw(2) << setfill('0') << (int)block[0]
                     << dec << setfill(' ')
                     << ", checksum "
                     << (ChecksumOK(block) ? "OK" : "BAD")
                     << ")" << endl;

                if (block[0] == 0x02)
                    PrintCea861(block);
                else if (block[0] == 0x70)
                    PrintDisplayID(block);
            }
            else if (block[0] == 0x02)
            {
                // 其他扩展块只输出 DTD
                PrintCeaDTDs(block);
            }
        }
    }
}

