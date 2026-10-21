#pragma once
#include <windows.h>
#include <xinput.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <array>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <set>

// Compare meaningful controls, not packet numbers or periodic status reports.
class DeviceInput {
    std::map<std::pair<std::uintptr_t, unsigned>, std::vector<LONG>> reports;
    std::map<std::uintptr_t,std::set<USHORT>> heldKeys;
    long long mouseDx=0,mouseDy=0;
    std::uint64_t mouseAt=0;
public:
    void resetMotion() { mouseDx=mouseDy=0; mouseAt=0; }
    bool key(std::uintptr_t device,USHORT code,bool down) {
        if(!down) { heldKeys[device].erase(code); return false; }
        return heldKeys[device].insert(code).second;
    }
    bool mouseActivity(const RAWMOUSE& m,std::uint64_t now) {
        if(mouse(m)) { resetMotion(); return true; }
        if(m.usFlags&MOUSE_MOVE_ABSOLUTE) return false;
        if(!mouseAt || now-mouseAt>150) { mouseDx=mouseDy=0; mouseAt=now; }
        mouseDx+=m.lLastX; mouseDy+=m.lLastY;
        if(std::abs(mouseDx)+std::abs(mouseDy)>=8) { resetMotion(); return true; }
        return false;
    }
    const char* source="hid";
    bool presses(std::uintptr_t device,unsigned key,const std::vector<LONG>& values) {
        auto found=reports.find({device,key});
        if(found==reports.end()) { reports[{device,key}]=values; return false; }
        bool down=false;
        for(auto value:values) if(std::find(found->second.begin(),found->second.end(),value)==found->second.end()) down=true;
        found->second=values; return down;
    }
    bool motion(std::uintptr_t device,unsigned key,LONG value,LONG center,bool active,bool hat) {
        auto found=reports.find({device,key});
        if(found==reports.end()) { reports[{device,key}]={value}; return false; }
        const LONG old=found->second[0];
        const bool changed=active && (hat?value!=old:std::abs(value-center)>=std::abs(old-center)+3);
        if(changed || !active || value==center) found->second[0]=value;
        return changed;
    }
    // Cumulative controller noise must not become activity. Only new presses
    // or a deliberate increase away from neutral counts, never releases.
    bool controller(std::uintptr_t device,const std::vector<LONG>& values) {
        auto key=std::make_pair(device,0U);
        auto found=reports.find(key);
        if(found==reports.end()) { reports.emplace(key,values); return false; }
        auto& old=found->second;
        if(old.size()!=values.size()) { old=values; return false; }
        bool active=(values[0]&~old[0])!=0;
        for(size_t i=1;i<values.size();++i) {
            const LONG center=i>=3?16:0;
            if(std::abs(values[i]-center)>=std::abs(old[i]-center)+3) active=true;
        }
        if(active || values[0]!=old[0]) old=values;
        // Returning to neutral establishes a new baseline without resuming.
        bool neutral=values[0]==0;
        for(size_t i=1;i<values.size();++i) neutral=neutral && values[i]==(i>=3?16:0);
        if(neutral) old=values;
        return active;
    }
    static bool mouse(const RAWMOUSE& m) {
        const USHORT presses=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_DOWN|RI_MOUSE_MIDDLE_BUTTON_DOWN|RI_MOUSE_BUTTON_4_DOWN|RI_MOUSE_BUTTON_5_DOWN;
        if(m.usButtonFlags&presses) return true;
        if((m.usButtonFlags&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL)) && static_cast<SHORT>(m.usButtonData)!=0) return true;
        // Ignore single-count sensor jitter and absolute-position status packets.
        return !(m.usFlags&MOUSE_MOVE_ABSOLUTE) && (std::abs(static_cast<long long>(m.lLastX))+std::abs(static_cast<long long>(m.lLastY))>=4);
    }
    bool rawAvailable = false;
    bool observe(std::uintptr_t device, unsigned report, const std::vector<LONG>& values) {
        if (values.empty()) return false;
        auto key = std::make_pair(device, report);
        auto found = reports.find(key);
        if (found == reports.end()) { reports.emplace(key, values); return false; }
        const bool changed = found->second != values;
        found->second = values; return changed;
    }
    void forget(std::uintptr_t device) {
        heldKeys.erase(device);
        for (auto it = reports.begin(); it != reports.end();) {
            if (it->first.first == device) it = reports.erase(it); else ++it;
        }
    }
    void reset() { reports.clear(); heldKeys.clear(); resetMotion(); }
    static LONG axis(LONG value, LONG minimum, LONG maximum, bool centered) {
        const auto range = static_cast<std::int64_t>(maximum) - minimum;
        if (range <= 0) return value;
        const auto offset = static_cast<std::int64_t>(value) - minimum;
        if (centered && std::abs(offset * 2 - range) <= range / 6) return 16;
        return static_cast<LONG>(std::clamp<std::int64_t>(offset * 32 / range, 0, 32));
    }
    bool pollControllers() {
        bool changed = false;
        for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
            XINPUT_STATE state{};
            const auto device = static_cast<std::uintptr_t>(index + 1);
            if (XInputGetState(index, &state) != ERROR_SUCCESS) { forget(device); continue; }
            const auto& g = state.Gamepad;
            std::vector<LONG> values{g.wButtons,
                g.bLeftTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0 : g.bLeftTrigger / 8,
                g.bRightTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0 : g.bRightTrigger / 8,
                axis(g.sThumbLX, -32768, 32767, true), axis(g.sThumbLY, -32768, 32767, true),
                axis(g.sThumbRX, -32768, 32767, true), axis(g.sThumbRY, -32768, 32767, true)};
            changed = controller(device, values) || changed;
        }
        // Avoid legacy WinMM / DirectInput polling for controller compatibility.
        // Non-XInput controllers are observed through background Raw Input.
        return changed;
    }
    void attach(HWND target) {
        // Generic joystick, gamepad, multi-axis controller, pen/tablet and media controls.
        const RAWINPUTDEVICE devices[] = {
            {0x01, 0x02, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x01, 0x06, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x01, 0x04, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x01, 0x05, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x01, 0x08, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x0D, 0x02, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {0x0C, 0x01, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target}
        };
        rawAvailable = RegisterRawInputDevices(devices, ARRAYSIZE(devices), sizeof(RAWINPUTDEVICE)) != FALSE;
    }
    void detach() {
        RAWINPUTDEVICE devices[] = {{1, 2, RIDEV_REMOVE, nullptr}, {1, 6, RIDEV_REMOVE, nullptr}, {1, 4, RIDEV_REMOVE, nullptr}, {1, 5, RIDEV_REMOVE, nullptr},
            {1, 8, RIDEV_REMOVE, nullptr}, {0x0D, 2, RIDEV_REMOVE, nullptr}, {0x0C, 1, RIDEV_REMOVE, nullptr}};
        RegisterRawInputDevices(devices, ARRAYSIZE(devices), sizeof(RAWINPUTDEVICE));
        reset();
    }
    bool raw(HRAWINPUT handle) {
        UINT size = 0;
        if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size > 1024 * 1024) return false;
        std::vector<DWORD> buffer((size + sizeof(DWORD) - 1) / sizeof(DWORD));
        if (GetRawInputData(handle, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return false;
        if (size < sizeof(RAWINPUTHEADER)) return false;
        const auto* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
        if(input->header.dwType==RIM_TYPEMOUSE && size>=offsetof(RAWINPUT,data)+sizeof(RAWMOUSE)) {
            source="raw-mouse"; return mouseActivity(input->data.mouse,GetTickCount64());
        }
        if(input->header.dwType==RIM_TYPEKEYBOARD && size>=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)) {
            source="raw-keyboard";
            return input->data.keyboard.VKey!=255 && key(reinterpret_cast<std::uintptr_t>(input->header.hDevice),input->data.keyboard.VKey,!(input->data.keyboard.Flags&RI_KEY_BREAK));
        }
        source="raw-hid";
        if (size < offsetof(RAWINPUT, data) + offsetof(RAWHID, bRawData)) return false;
        if (input->header.dwType != RIM_TYPEHID) return false;
        const auto& hid = input->data.hid;
        if (!hid.dwSizeHid || static_cast<std::uint64_t>(hid.dwSizeHid) * hid.dwCount > size - offsetof(RAWINPUT, data) - offsetof(RAWHID, bRawData)) return false;
        UINT prepSize = 0;
        if (GetRawInputDeviceInfoW(input->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &prepSize) == static_cast<UINT>(-1) || !prepSize || prepSize > 1024 * 1024) return false;
        std::vector<DWORD> prepBuffer((prepSize + 3) / 4);
        if (GetRawInputDeviceInfoW(input->header.hDevice, RIDI_PREPARSEDDATA, prepBuffer.data(), &prepSize) == static_cast<UINT>(-1)) return false;
        auto prep = reinterpret_cast<PHIDP_PREPARSED_DATA>(prepBuffer.data());
        HIDP_CAPS caps{};
        if (HidP_GetCaps(prep, &caps) != HIDP_STATUS_SUCCESS) return false;
        USHORT buttonCount = caps.NumberInputButtonCaps, valueCount = caps.NumberInputValueCaps;
        std::vector<HIDP_BUTTON_CAPS> buttons(buttonCount);
        std::vector<HIDP_VALUE_CAPS> values(valueCount);
        if (buttonCount && HidP_GetButtonCaps(HidP_Input, buttons.data(), &buttonCount, prep) != HIDP_STATUS_SUCCESS) return false;
        if (valueCount && HidP_GetValueCaps(HidP_Input, values.data(), &valueCount, prep) != HIDP_STATUS_SUCCESS) return false;
        bool changed = false;
        for (DWORD reportIndex = 0; reportIndex < hid.dwCount; ++reportIndex) {
            char* report = reinterpret_cast<char*>(const_cast<BYTE*>(hid.bRawData)) + reportIndex * hid.dwSizeHid;
            const auto device=reinterpret_cast<std::uintptr_t>(input->header.hDevice);
            unsigned buttonIndex=0;
            for (const auto& b : buttons) {
                const ULONG capacity = HidP_MaxUsageListLength(HidP_Input, b.UsagePage, prep);
                if (!capacity || capacity > 4096) continue;
                std::vector<USAGE> usages(capacity); ULONG count = capacity;
                if (HidP_GetUsages(HidP_Input, b.UsagePage, b.LinkCollection, usages.data(), &count, prep, report, hid.dwSizeHid) != HIDP_STATUS_SUCCESS) continue;
                if(b.UsagePage!=1 && b.UsagePage!=0x0C && b.UsagePage!=9 && b.UsagePage!=0x0D) continue;
                std::vector<LONG> pressed;
                std::sort(usages.begin(), usages.begin() + count);
                for (ULONG i = 0; i < count; ++i) pressed.push_back(usages[i]);
                changed=presses(device,0x10000000U+b.ReportID*65536U+buttonIndex++,pressed)||changed;
            }
            for (const auto& v : values) {
                // Ignore vendor telemetry. Decode desktop axes/hat and pen position/pressure.
                const USAGE first = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
                const USAGE last = v.IsRange ? v.Range.UsageMax : v.NotRange.Usage;
                if (last < first || last - first > 128) continue;
                for (unsigned usage = first; usage <= last; ++usage) {
                    if (!((v.UsagePage == 1 && usage >= 0x30 && usage <= 0x39) || (v.UsagePage == 0x0D && usage == 0x30))) continue;
                    ULONG rawValue = 0;
                    if (HidP_GetUsageValue(HidP_Input, v.UsagePage, v.LinkCollection, static_cast<USAGE>(usage), &rawValue, prep, report, hid.dwSizeHid) != HIDP_STATUS_SUCCESS) continue;

                    LONG value = static_cast<LONG>(rawValue);
                    if (v.LogicalMin < 0 && v.BitSize > 0 && v.BitSize < 32 && (rawValue & (1UL << (v.BitSize - 1))))
                        value = static_cast<LONG>(rawValue | (~0UL << v.BitSize));

                    const bool centered = caps.UsagePage == 1 && (caps.Usage == 4 || caps.Usage == 5 || caps.Usage == 8);
                    const LONG normalizedValue=usage == 0x39 ? value : axis(value, v.LogicalMin, v.LogicalMax, centered);
                    const bool active=usage==0x39 ? value>=v.LogicalMin && value<=v.LogicalMax : normalizedValue!=(centered?16:0);
                    const unsigned key=0x20000000U+v.ReportID*65536U+v.LinkCollection*256U+usage;
                    changed=motion(device,key,normalizedValue,centered?16:0,active,usage==0x39)||changed;
                }
            }
        }
        return changed;
    }
};
