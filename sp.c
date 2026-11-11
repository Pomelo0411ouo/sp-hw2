#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
  pid_t pid;
  struct Child *nxt;
  int child_ptoc;
  int child_ctop;
} Child;

char buf[1024];
size_t buf_size = 1024;

Child *child_head = NULL;
Child *child_tail = NULL;

static void set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags != -1) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
}

int main(int argc, char *argv[]) {
  int cmd_fd, reply_fd, ptoc_fd, ctop_fd;
  bool is_main = false;
  if (argc == 1) {
    is_main = true;
  }

  // TODO: Determine whether the current process is SP main.
  //       After that, you may want to make the following code the same for both
  //       SP main and SP child processes. Therefore, you will set some
  //       variables here.

  if(is_main){
    cmd_fd = STDIN_FILENO;
    reply_fd = STDOUT_FILENO;
    ptoc_fd = -1;
    ctop_fd = -1;
  }
  else{
    ptoc_fd = 3;
    ctop_fd = 4;
    if(mkfifo(argv[1], 0666) < 0){
      perror("mkfifo failed.");
      exit(1);
    }
    if(mkfifo(argv[2], 0666) < 0){
      perror("mkfifo failed.");
      exit(1);
    }
    cmd_fd = open(argv[1], O_RDONLY);
    reply_fd = open(argv[2], O_WRONLY);
    if(cmd_fd < 0 || reply_fd < 0){
      perror("Open fifo failed.");
      exit(1);
    }
    int flags = fcntl(reply_fd, F_GETFD);
    set_cloexec(cmd_fd);
    set_cloexec(reply_fd);
    set_cloexec(ptoc_fd);
    set_cloexec(ctop_fd);
    if(unlink(argv[1]) == -1) {
      perror("unlink cmd_fifo error");
      exit(1);
    }
    if(unlink(argv[2]) == -1) {
      perror("unlink reply_fifo error");
      exit(1);
    }
  }

  struct pollfd jdg_cmd[2];
  jdg_cmd[0].fd = cmd_fd;
  jdg_cmd[0].events = 0 | POLLIN;
  // jdg_cmd[1].fd = reply_fd;
  // jdg_cmd[1].events = 0 | POLLOUT;
  struct pollfd pipe_cmd[70];
  nfds_t pipe_fds = 0;

  // end TODO

  while (1) {
    char command_buffer[1024] = "";
    Command command = {.buf = command_buffer, .len = 0, .type = UNKNOWN};
    // TODO: poll the STDIO/FIFO for commands.

    int cmds = poll(jdg_cmd, 1, 0);

    // end TODO

    // TODO: poll parent, and child processes for custom commands.
    
    int pipe_cmds = poll(pipe_cmd, pipe_fds, 0);

    // end TODO

    // TODO: classify the command and execute it.

    if(cmds > 0){
      char typ[10] = "";
      char c;
      int ptr = 0;
      while((c = getc(jdg_cmd[0].fd)) != ' ' && c != '\n' && c != EOF){
        typ[ptr++] = c;
      }
      typ[ptr] = '\0';
      if(c != '\n' && c!=EOF){
        fgets(command_buffer, buf_size, jdg_cmd[0].fd);
        command.len = strlen(command_buffer);
      }
      if(c == EOF){
        command.type = KILL;
      }
      else if(strcmp(typ, "create") == 0){
        command.type = CREATE;
      }
      else if(strcmp(typ, "store") == 0){
        command.type = STORE;
      }
      else if(strcmp(typ, "delete") == 0){
        command.type = DELETE;
      }
      else if(strcmp(typ, "fetch") == 0){
        command.type = FETCH;
      }
      else if(strcmp(typ, "update") == 0){
        command.type = UPDATE;
      }
      else if(strcmp(typ, "kill") == 0){
        command.type = KILL;
      }
      command_handler(&command);
    }

    // end TODO

    // TODO: handle custom commands.

    // if(pipe_cmds > 0){}
    // command_handler(&command);

    // end TODO
  }
}

int command_handler(Command *cmd) {
  switch (cmd->type) {
  case CREATE:
    create_handler(cmd->buf, cmd->len);
    break;
  case STORE:
    store_handler(cmd->buf, cmd->len);
    break;
  case DELETE:
    delete_handler(cmd->buf, cmd->len);
    break;
  case FETCH:
    fetch_handler(cmd->buf, cmd->len);
    break;
  case KILL:
    kill_handler(cmd->buf, cmd->len);
    break;
  case UPDATE:
    update_handler(cmd->buf, cmd->len);
    break;
  default:
    fprintf(stderr, "Unknown command type\n");
    return -1;
  }
  return 0;
}

int create_handler(char *buf, size_t len) {
  // TODO: Implement the CREATE command handler.

  struct Child *crt_chd = malloc(1*sizeof(struct Child));
  int tmp_ptoc[2];
  int tmp_ctop[2];
  if(pipe(tmp_ptoc) < 0 || pipe(tmp_ctop) < 0){
    perror("pipe failed");
    return -1;
  }
  pid_t pid = fork();
  if(pid < 0){
    perror("fork failed");
    return -1;
  }
  if(pid == 0){
    if(tmp_ctop[1] == 3){
      tmp_ctop[1] = dup(tmp_ctop[1]);
      close(3);
    }
    if(tmp_ptoc[0] != 3){
      dup2(tmp_ptoc[0], 3);
      close(tmp_ptoc[0]);
    }
    close(tmp_ptoc[1]);
    close(tmp_ctop[0]);
    if(tmp_ctop[1] != 4){
      dup2(tmp_ctop[1], 4);
      close(tmp_ctop[1]);
    }
    char cmd_fifo[1024] = "";
    char reply_fifo[1024] = "";
    int ptr = 0;
    while(buf[ptr] != ' '){
      cmd_fifo[ptr] = buf[ptr];
      ptr++;
    }
    cmd_fifo[ptr++] = '\0';
    int rep_ptr = 0;
    while(ptr<len && buf[ptr] != '\n'){
      reply_fifo[rep_ptr++] = buf[ptr++];
    }
    reply_fifo[rep_ptr] = '\0';
    size_t r_len = strlen(reply_fifo);
    if (r_len > 0 && reply_fifo[r_len - 1] == '\n') {
      reply_fifo[r_len - 1] = '\0';
    }
    char *args[] = {"./sp", cmd_fifo, reply_fifo, NULL};
    execv(args[0], args);
    perror("exec failed.");
    exit(1);
  }
  close(tmp_ptoc[0]);
  close(tmp_ctop[1]);
  set_cloexec(tmp_ptoc[1]);
  set_cloexec(tmp_ctop[0]);
  crt_chd->ptoc = tmp_ptoc[1];
  crt_chd->ctop = tmp_ctop[0];
  crt_chd->pid = pid;
  crt_chd->nxt = NULL;
  if(child_head == NULL){
    child_head = crt_chd;
    child_tail = crt_chd;
  }
  else{
    child_tail->nxt = crt_chd;
    child_tail = crt_chd;
  }
  return 0;
}

int store_handler(char *buf, size_t len) {
  // TODO: Implement the STORE command handler.
}

int delete_handler(char *buf, size_t len) {
  // TODO: Implement the DELETE command handler.
}

int fetch_handler(char *buf, size_t len) {
  // TODO: Implement the FETCH command handler.
}

int kill_handler(char *buf, size_t len) {
  // TODO: Implement the KILL command handler.
}

int update_handler(char *buf, size_t len) {
  // TODO: Implement the UPDATE command handler.
}
