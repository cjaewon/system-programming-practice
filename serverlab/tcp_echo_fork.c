#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/wait.h>

#define PORT 3000

ssize_t write_all(int fd, const void *buf, size_t count) {
  size_t remain = count;
  const char *ptr = (const char *)buf;

  while (remain > 0) {
    ssize_t written_cnt = write(fd, ptr, remain);

    if (written_cnt < 0) {
      if (errno == EINTR) continue;
      else return -1;
    }

    remain -= written_cnt;
    ptr += written_cnt;
  }

  return (ssize_t)count;
}

int main() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (server_fd < 0) {
    perror("socket 생성 실패");
    return -1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in server_addr;

  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)& server_addr, sizeof(server_addr)) < 0) {
    perror("socket bind 실패");
    close(server_fd);

    return -1;
  }

  if (listen(server_fd, 5) < 0) {
    perror("socket listen 실패");
    close(server_fd);

    return -1;
  }

  printf("server is listening in :%d\n", PORT);

  while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // child process 회수

    while (true) {
      pid_t wait_pid = waitpid(-1, NULL, WNOHANG);

      if (wait_pid > 0) continue; 
      if (wait_pid == 0) break;

      if (errno == EINTR) continue;
      if (errno != ECHILD) perror("waitpid 실패");

      break;
    }

    int client_fd = accept(server_fd, (struct sockaddr*)& client_addr, &client_len);

    if (client_fd < 0) {
      perror("socket server accept 실패");
      continue;
    }

    pid_t pid = fork();

    if (pid > 0) { // parent
      close(client_fd);
    } else if (pid == 0) { // child 
      while (true) {
        close(server_fd);

        char buf[4096];
        ssize_t read_count = read(client_fd, buf, 4096);

        if (read_count > 0) {
          if (write_all(client_fd, buf, read_count) < 0) {
            perror("socket write all 실패");
          }
        } else if (read_count == 0) {
          break; // EOF
        } else {
          if (errno == EINTR) continue;

          perror("socket read 실패");
          break;
        }
      }

      close(client_fd);

      _exit(0);
    } else { // fork error
      close(client_fd);
      perror("fork 에러 발생");
    }

  }

  close(server_fd);

  return 0;
}