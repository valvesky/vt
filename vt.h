#ifndef _VT_H_
#define _VT_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* SDL libraries */
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_events.h>

#include <locale.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>

typedef struct {
  int pid;
  int fd;
  // int stdin_fd;
  // int stdout_fd;
  // int stderr_fd;
  bool active;
} Shell;

Shell shell_init();
void shell_destroy(Shell *shell);
void shell_read(Shell shell);

#endif
