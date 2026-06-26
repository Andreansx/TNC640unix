/* gdactive.c -- LD_PRELOAD: force gtk_external_display_active() -> 0 (libgtkbind 0x16510).
 * That fn returns g_ascii_strcasecmp(gdk_get_display(), ":0.0") != 0 -> TRUE for any display
 * whose name isn't exactly ":0.0" (our Xvfb :0 reports ":0"); when TRUE, GUPPYRUNTIMEGTK_::
 * CreateWindowData SKIPS the screen/usage->Wnd-type dispatch entirely, so the OEM window is
 * created via the external/fallthrough path = NON-bind-capable (no +0x14/+0x1c) -> jh.softkey.
 * Register "Binding...failed". On a real NC the display IS the internal ":0.0"; forcing this to
 * 0 makes the bind path behave like the internal NC display so the usage dispatch runs and an
 * OEMmachine/OemPanel usage maps to a bind-capable WndFocusPane.
 * NOTE: must keep a libc reference (write()) so the .so carries DT_NEEDED libc.so.6 — a
 * no-NEEDED .so is rejected by FEX's ELF preloader ("cannot open shared object file").
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o gdactive.so emulator/gdactive.c */
#include <unistd.h>
__attribute__((constructor)) static void gdactive_init(void){
    const char m[] = "[gdactive] gtk_external_display_active forced -> 0\n";
    write(2, m, sizeof(m)-1);
}
int gtk_external_display_active(void){ return 0; }
