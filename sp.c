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
#include <sys/wait.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} LineBuf;

typedef struct Child {
  pid_t pid;
  int child_ptoc;
  int child_ctop;
  LineBuf ctop_lb;
  struct Child *nxt;
} Child;

typedef struct KV{
  char *key;
  char *val;
  int val_len;
  struct KV *nxt;
} KV;

int cmd_fd = -1, reply_fd = -1, ptoc_fd = -1, ctop_fd = -1;
bool is_main = false;

KV *local_table = NULL;

Child *child_head = NULL;
Child *child_tail = NULL;

LineBuf cmd_lb = {NULL, 0, 0};
LineBuf ptoc_lb = {NULL, 0, 0};

static void set_cloexec(int fd){
  int flags = fcntl(fd, F_GETFD);
  if(flags != -1){
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
}

int lb_append(LineBuf *lb, const char *src, size_t n){
    if(lb->len + n + 1 > lb->cap){
        size_t new_cap = (lb->cap == 0) ? 1024 : lb->cap;
        while(new_cap < lb->len + n + 1){
            new_cap *= 2;
        }
        char *new_buf = realloc(lb->buf, new_cap);
        if(!new_buf) return -1;
        lb->buf = new_buf;
        lb->cap = new_cap;
    }
    memcpy(lb->buf + lb->len, src, n);
    lb->len += n;
    lb->buf[lb->len] = '\0';
    return 0;
}

static void lb_consume(LineBuf *lb, size_t consumed){
  size_t remain = lb->len - consumed;
  if(remain > 0){
    memmove(lb->buf, lb->buf + consumed, remain);
  }
  lb->len = remain;
  if(lb->buf){
    lb->buf[lb->len] = '\0';
  }
  if(lb->len == 0 && lb->cap > 4096){
    free(lb->buf);
    lb->buf = NULL;
    lb->cap = 0;
  }
}

void handle_parent_msg(char *line, size_t len){};
void handle_child_msg(Child *chd, char *line, size_t len){};

// Record: 11/11 rest: input and poll

int main(int argc, char *argv[]) {

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
    set_cloexec(cmd_fd);
    set_cloexec(reply_fd);
    set_cloexec(ptoc_fd);
    set_cloexec(ctop_fd);
    if(unlink(argv[1]) == -1){
      perror("unlink cmd_fifo failed");
      exit(1);
    }
    if(unlink(argv[2]) == -1){
      perror("unlink reply_fifo failed");
      exit(1);
    }
  }

  // end TODO

  while (1) {
    Command command = {.buf = "", .len = 0, .type = UNKNOWN};

    struct pollfd fds[40];
    Child *fd_map[40] = {NULL};
    nfds_t nfds = 0;

    // TODO: poll the STDIO/FIFO for commands.

    fds[0].fd = cmd_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    nfds = 1;

    // end TODO

    // TODO: poll parent, and child processes for custom commands.
    
    fds[1].fd = ptoc_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    nfds = 2;

    for(Child *cur = child_head; cur != NULL; cur = cur->nxt){
      fds[nfds].fd = cur->child_ctop;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      fd_map[nfds] = cur;
      nfds++;
    }

    // end TODO

    int ready = poll(fds, nfds, -1);
    if(ready < 0){
      if(errno == EINTR) continue;
      perror("poll error");
      exit(1);
    }

    // TODO: classify the command and execute it.

    char read_chunk[65536];

    if(fds[0].revents & (POLLIN | POLLHUP)){
      ssize_t n = read(cmd_fd, read_chunk, sizeof(read_chunk));
      if(n < 0){
        if(errno != EINTR && errno != EAGAIN){
          perror("read cmd_fd failed.");
          exit(1);
        }
      }
      else if(n == 0){
        Command kill_cmd = {.buf = "", .len = 0, .type = KILL};
        command_handler(&kill_cmd);
      }
      else{
        if(lb_append(&cmd_lb, read_chunk, (size_t)n) < 0){
          perror("realloc cmd failed.");
          exit(1);
        }

        char *nl;
        while((nl = memchr(cmd_lb.buf, '\n', cmd_lb.len)) != NULL){
          size_t line_len = (size_t)(nl - cmd_lb.buf);
          *nl = '\0';
          char *space = memchr(cmd_lb.buf, ' ', line_len);
          char *cmd_type_str = cmd_lb.buf;
          char *arg_ptr = "";
          size_t arg_len = 0;

          if(space != NULL){
            *space = '\0';
            arg_ptr = space + 1;
            arg_len = line_len - (size_t)(arg_ptr - cmd_lb.buf);
          }

          Command command = {.buf = arg_ptr, .len = arg_len, .type = UNKNOWN};
          if(strcmp(cmd_type_str, "create") == 0) command.type = CREATE;
          else if(strcmp(cmd_type_str, "store") == 0) command.type = STORE;
          else if(strcmp(cmd_type_str, "delete") == 0) command.type = DELETE;
          else if(strcmp(cmd_type_str, "fetch") == 0) command.type = FETCH;
          else if(strcmp(cmd_type_str, "kill") == 0) command.type = KILL;
          else if(strcmp(cmd_type_str, "update") == 0) command.type = UPDATE;

          command_handler(&command);

          lb_consume(&cmd_lb, line_len + 1);
        }
      }
    }

    // end TODO

    // TODO: handle custom commands.

    if(ptoc_fd != -1 && (fds[1].revents & (POLLIN | POLLHUP))){
      ssize_t n = read(ptoc_fd, read_chunk, sizeof(read_chunk));
      if(n < 0){
        if(errno != EINTR && errno != EAGAIN){
          perror("read ptoc_fd failed.");
          exit(1);
        }
      }
      else if(n == 0){
        Command kill_cmd = {.buf = "", .len = 0, .type = KILL};
        command_handler(&kill_cmd);
      }
      else if(n > 0){
        if(lb_append(&ptoc_lb, read_chunk, (size_t)n) < 0){
          perror("realloc ptoc_lb failed.");
          exit(1);
        }
        char *nl;
        while((nl = memchr(ptoc_lb.buf, '\n', ptoc_lb.len)) != NULL){
          size_t line_len = (size_t)(nl - ptoc_lb.buf);
          *nl = '\0';
          handle_parent_msg(ptoc_lb.buf, line_len);
          lb_consume(&ptoc_lb, line_len + 1);
        }
      }
    }

    for(nfds_t i = 2; i < nfds; i++){
      if(!(fds[i].revents & (POLLIN | POLLHUP))) continue;

      Child *chd = fd_map[i];
      ssize_t n = read(chd->child_ctop, read_chunk, sizeof(read_chunk));
      if(n < 0){
        if(errno != EINTR && errno != EAGAIN){
          perror("read ctop_fd failed.");
          exit(1);
        }
      }
      if(n == 0){
        waitpid(chd->pid, NULL, 0);
        close(chd->child_ptoc);
        close(chd->child_ctop);
        free(chd->ctop_lb.buf);
        Child *prev = NULL, *cur = child_head;
        while(cur != chd){
          prev = cur;
          cur = cur->nxt;
        }
        if(cur == chd){
          if(prev == NULL) child_head = chd->nxt;
          else prev->nxt = chd->nxt;
          if(child_tail == chd) child_tail = prev;
        }
        free(chd);
      }
      else if(n > 0){
        if(lb_append(&chd->ctop_lb, read_chunk, (size_t)n) < 0){
          perror("realloc ctop_lb failed.");
          exit(1);
        }
        char *nl;
        while((nl = memchr(chd->ctop_lb.buf, '\n', chd->ctop_lb.len)) != NULL){
          size_t line_len = (size_t)(nl - chd->ctop_lb.buf);
          *nl = '\0';
          handle_child_msg(chd, chd->ctop_lb.buf, line_len);
          lb_consume(&chd->ctop_lb, line_len + 1);
        }
      }
    }
    
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

  Child *crt_chd = calloc(1, sizeof(Child));
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
    close(tmp_ptoc[1]);
    close(tmp_ctop[0]);
    if(tmp_ctop[1] == 3){
      tmp_ctop[1] = dup(tmp_ctop[1]);
      close(3);
    }
    if(tmp_ptoc[0] != 3){
      dup2(tmp_ptoc[0], 3);
      close(tmp_ptoc[0]);
    }
    if(tmp_ctop[1] != 4){
      dup2(tmp_ctop[1], 4);
      close(tmp_ctop[1]);
    }
    char *space = memchr(buf, ' ', len);
    *space = '\0';
    char *args[] = {"./sp", buf, space+1, NULL};
    execv(args[0], args);
    perror("exec failed.");
    exit(1);
  }
  close(tmp_ptoc[0]);
  close(tmp_ctop[1]);
  set_cloexec(tmp_ptoc[1]);
  set_cloexec(tmp_ctop[0]);
  crt_chd->child_ptoc = tmp_ptoc[1];
  crt_chd->child_ctop = tmp_ctop[0];
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

  char *space = memchr(buf, ' ', len);
  *space = '\0';
  char *k = buf;
  int k_len = strlen(k);
  char *v = space+1;
  int new_vlen = strlen(v);
  char *new_v;
  KV *cur = local_table;
  KV *prev = cur;
  while(cur != NULL){
    if(strcmp(k, cur->key) == 0){
      new_v = realloc(cur->val, (new_vlen+1) * sizeof(char));
      memcpy(new_v, v, new_vlen);
      new_v[new_vlen] = '\0';
      cur->val = new_v;
      cur->val_len = new_vlen;
      break;
    }
    prev = cur;
    cur = cur->nxt;
  }
  if(cur == NULL){
    KV *new_pair = malloc(sizeof(KV));
    new_v = malloc((new_vlen + 1) * sizeof(char));
    memcpy(new_v, v, new_vlen);
    new_v[new_vlen] = '\0';
    new_pair->val = new_v;
    new_paie->val_len = new_vlen;
    char *new_k = malloc(sizeof(char) * (k_len + 1));
    memcpy(new_k, k, k_len);
    new_k[k_len] = '\0';
    new_pair->key = new_k;
    new_pair->nxt = NULL;
    if(prev == NULL){
      local_table = new_pair;
    }
    else{
      prev->nxt = new_pair;
    }
  }
  return 0;
}

int delete_handler(char *buf, size_t len) {
  // TODO: Implement the DELETE command handler.

  KV *cur = local_table;
  KV *prev = cur;
  while(cur != NULL){
    if(strcmp(buf, cur->key) == 0) break;
    prev = cur;
    cur = cur->nxt;
  }
  if(cur != NULL){
    if(prev == cur){
      local_table = cur->nxt;
    }
    else{
      prev->nxt = cur->nxt;
    }
    free(cur->key);
    free(cur->val);
    free(cur);
  }
  return 0;
}

int fetch_handler(char *buf, size_t len) {
  // TODO: Implement the FETCH command handler.
}

int kill_handler(char *buf, size_t len) {
  // TODO: Implement the KILL command handler.

  Child *cur_c = child_head;
  Child *prev_c = cur_c;
  while(cur_c != NULL){
    prev_c = cur_c;
    cur_c = cur_c->nxt;
    free(prev_c->ctop_lb);
    close(prev_c->child_ctop);
    close(prev_c->child_ptoc);
    waitpid(prev_c->pid, NULL, 0);
    free(prev_c);
  }
  close(ptoc_fd);
  close(ctop_fd);
  close(cmd_fd);
  close(reply_fd);
  free(ptoc_lb);
  free(cmd_lb);
  KV *cur_kv = local_table;
  KV *prev_kv = cur_kv;
  while(cur_kv != NULL){
    prev_kv = cur_kv;
    cur_kv = cur_kv->nxt;
    free(prev_kv->key);
    free(prev_kv->val);
    free(prev_kv);
  }
  exit(0);
}

int update_handler(char *buf, size_t len) {
  // TODO: Implement the UPDATE command handler.
}
