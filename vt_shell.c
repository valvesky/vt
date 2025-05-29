#include "vt.h"
#include <pty.h>
#include <sys/ioctl.h>

Shell
shell_init() {
  Shell shell = {0};

  int master, slave;
  if (openpty(&master, &slave, NULL, NULL, NULL) < 0)
    crash("couldn't open tty");
    
  shell.pid = fork();
  if (shell.pid < 0) {
    crash("fork failed");
  }

  if (shell.pid == 0) {
    close(master);
    setsid(); /* create a new process group */

    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);

    if (ioctl(slave, TIOCSCTTY, NULL) < 0)
      crash("ioctl failed! ");
    if (slave > STDERR_FILENO) 
      close(slave);

    setenv("TERM", "xterm-256color", 1);
    execlp("bash", "bash", "--login", NULL);

    /* this version of exit closes all file descriptors */ 
    _Exit(EXIT_SUCCESS);
  }

  shell.fd = master;
  shell.active = true;
  return shell;
}

void
shell_destroy(Shell *shell) {
  if (!shell->active) return;

  /* Send EOF to pipe */
  if (shell->fd > 0)
    close(shell->fd); 

  /* Wait for shell to exit */
  if (shell->pid > 0)
    waitpid(shell->pid, NULL, 0);

  printf("[Shell %-d] Exited successfully\n", shell->pid);
  shell->active = false;
}
