#ifndef BONGO_CAT_LINUX_INPUT_WAIT_H
#define BONGO_CAT_LINUX_INPUT_WAIT_H

/* Private eventfd for listener shutdown; join the worker before closing it. */
int bongo_cat_linux_input_stop_create(void);
void bongo_cat_linux_input_stop_signal(int stop_fd);
/* Wait for input or shutdown: 1 = input, 0 = stop, -1 = failure. */
int bongo_cat_linux_input_wait(int input_fd, int stop_fd);

#endif
