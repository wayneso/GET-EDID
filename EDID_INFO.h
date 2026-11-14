#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 打印 EDID 的完整解析信息
// （基础信息 / Timings / Descriptors / Extension Blocks）
void PrintEDIDInfo(const std::vector<BYTE>& edid);

// 获取 EDID 的首选(主要)分辨率, 例如 "2560x1440p @ 59.95Hz"
std::string GetPreferredResolution(const std::vector<BYTE>& edid);
