#include "../src/device_input.h"
#include <cstdlib>
#include <iostream>
void check(bool ok, const char* label) { if (!ok) { std::cerr << label << '\n'; std::exit(1); } }
int main() {
    DeviceInput input;
    check(!input.observe(5000, 0, {0, 16, 16}), "connection establishes baseline only");
    check(!input.observe(5000, 0, {0, 16, 16}), "periodic unchanged packets ignored");
    check(input.observe(5000, 0, {1, 16, 16}), "button press detected");
    check(!input.observe(5000, 0, {1, 16, 16}), "held button not repeated");
    check(input.observe(5000, 0, {0, 16, 16}), "button release detected");
    check(!input.observe(5001, 0, {0, 16, 16}), "second controller has independent baseline");
    check(!input.observe(5000, 1, {0, 16}), "report IDs have independent baselines");
    check(input.observe(5000, 0, {0, 22, 16}), "stick movement detected");
    check(DeviceInput::axis(0, -32768, 32767, true) == DeviceInput::axis(1000, -32768, 32767, true), "center jitter filtered");
    check(DeviceInput::axis(0, -32768, 32767, true) != DeviceInput::axis(16000, -32768, 32767, true), "deliberate stick movement retained");
    input.forget(5000);
    check(!input.observe(5000, 0, {1, 16, 16}), "reconnection does not trigger");
    input.reset();
    check(!input.observe(5001, 0, {1, 16, 16}), "sleep reset establishes fresh baseline");
    input.pollControllers(); // Exercise actual APIs even when no controller is connected.
    RAWMOUSE mouse{}; mouse.lLastX=1;
    check(!DeviceInput::mouse(mouse),"one-count mouse jitter cannot resume work");
    mouse.lLastX=8; check(DeviceInput::mouse(mouse),"deliberate mouse movement counts");
    mouse.lLastX=0; mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;
    check(!DeviceInput::mouse(mouse),"mouse release cannot resume work");
    mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
    check(DeviceInput::mouse(mouse),"new mouse press counts");
    check(!input.controller(9000,{0,0,0,16,16,16,16}),"controller initial baseline ignored");
    check(!input.controller(9000,{0,0,0,17,16,16,16}),"single-bucket controller jitter ignored");
    check(input.controller(9000,{1,0,0,17,16,16,16}),"new controller button press counts");
    check(!input.controller(9000,{1,0,0,18,16,16,16}),"jitter while holding button cannot count as new press");
    check(!input.controller(9000,{0,0,0,16,16,16,16}),"controller release to neutral ignored");
    check(input.controller(9000,{0,0,0,24,16,16,16}),"deliberate controller deflection retained");
    check(!input.presses(9001,2,{}),"empty HID baseline established");
    check(input.presses(9001,2,{4}),"HID press recognized");
    check(!input.presses(9001,2,{}),"HID release cannot resume");
    RAWMOUSE slow{}; slow.lLastX=1;
    for(unsigned i=0;i<7;++i) check(!input.mouseActivity(slow,100+i*10),"small deliberate movement accumulates without early trigger");
    check(input.mouseActivity(slow,170),"high polling rate small movements can still resume work");
    for(unsigned i=0;i<20;++i) { slow.lLastX=i%2?1:-1; check(!input.mouseActivity(slow,200+i*5),"alternating sensor jitter is ignored"); }
    check(input.key(42,65,true),"new keyboard press counts");
    check(!input.key(42,65,true),"held keyboard auto-repeat cannot count as fresh activity");
    check(!input.key(42,65,false),"key release ignored");
    check(input.key(42,65,true),"new press after release counts");
    std::cout << "All device input checks passed.\n";
}
