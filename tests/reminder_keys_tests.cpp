#include "../src/reminder_keys.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    ReminderKeys keys;
    check(!keys.down('A'),"normal typing is unaffected without a dismissed popup");
    keys.dismiss(13); check(keys.active() && keys.down(13),"closing Enter and repeat are consumed");
    check(keys.up(13) && !keys.active(),"closing key remains fenced until release");
    keys.dismiss(17); keys.down(18); keys.down('L');
    check(keys.up(17) && keys.active(),"first modifier release cannot expose the remaining chord");
    check(keys.up(18) && keys.active(),"second modifier release still consumes hotkey letter");
    check(keys.up('L') && !keys.active(),"whole chord is released before accepting new work input");
    check(!keys.down('C'),"subsequent independent input is allowed");
    keys.dismiss(17); keys.reset(); check(!keys.active(),"lock, sleep or device removal cannot strand a held closing key");
    keys.dismiss(0);keys.dismiss(255+1);check(!keys.active(),"invalid keys do not leave a permanent fence");
    std::cout<<"Reminder key acknowledgement and chord checks passed.\n";
}
