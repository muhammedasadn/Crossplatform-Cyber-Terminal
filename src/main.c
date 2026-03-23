#include <stdio.h>
#include "window.h"

int main(void) {
    Window win;  /* Our window bundle — just a struct on the stack */

    /* Initialize: 800 wide, 500 tall */
    if (window_init(&win, 800, 500) != 0) {
        printf("Failed to create window.\n");
        return 1;
    }

    /* THE MAIN LOOP
     * This runs continuously until win.running becomes 0.
     * Every iteration = one frame drawn to the screen. */
    while (win.running) {
        window_handle_events(&win);  /* Check for input/quit */
        window_render_begin(&win);   /* Clear screen         */
        /* (later: draw text here)                            */
        window_render_end(&win);     /* Show the frame       */
    }

    window_destroy(&win);  /* Clean up before exit */
    return 0;
}