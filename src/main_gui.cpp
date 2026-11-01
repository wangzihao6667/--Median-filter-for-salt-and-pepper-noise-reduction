#include "gui.h"

#include <windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    return run_gui(instance, show_command);
}
