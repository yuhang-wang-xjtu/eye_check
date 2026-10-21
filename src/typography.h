#pragma once
// Shared by the C++ renderer and the Win32 dialog resource compiler.
#define UI_BODY_POINTS 9
#define UI_FONT_FACE "Microsoft YaHei UI"
#ifndef RC_INVOKED
namespace Typography {
inline constexpr int Body = UI_BODY_POINTS * 96 / 72;
inline constexpr int Caption = 11;
inline constexpr int Title = 20;
inline constexpr int Clock = 56;
inline constexpr const wchar_t* Face = L"Microsoft YaHei UI";
}
#endif
