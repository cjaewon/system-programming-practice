/*
미니 셸 구현

bash와 유사한 문법을 지원하지만, 호환되지 않는 명령어가 있음.
아래 참고

추가적으로 입력 명령어에 대한 길이 제한이 있음. 코드 참고 ex) char[15][50]
경계 검사가 없음을 유의. index error, 오버플로우 위험 있음.
fork, dup2 시스템콜 에러 처리 안 함.

지원하는 문법:
- 셸 연산자와 인자들은 모두 스페이스바로 정확히 구분되어야함.
  - echo"hi" X => echo "hi" O
  - cat a.txt>b.txt X => cat a.txt > b.txt O
- pipeline : cat hello.txt | grep "Hello"
- redirection : echo "Hello, World" > opt.txt 
  (pipeline과 redirection이 겹칠 경우 pipeline을 우선함.)
- 지원하는 내장 명령어 : cd

지원하지 않는 문법:
- &&, 작은 따옴표, (), 탭, \ 
- pipline with cd
- 주석
- 지원하는 문법에 표시되지 않은 대부분 문법들

기타:
- lexer는 간단한 상태 머신을 구현함.
- parse_and_excute는 특정한 자료구조로 명령어를 다시 표현하기 보다는 배열을 읽어가면서 바로 상태를 업데이트하고 파이프라인 만나면 
  이 상태를 실행하는 방식으로 구현함.
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

typedef enum {
  STATE_START,
  STATE_GAP, 
  STATE_WORD,
  STATE_DQUOTE,
  STATE_FIN_DQUOTE,
} State;

char* unquote(char* s) {
  if (s[0] == '\"') {
    s[strlen(s) - 1] = '\0';
    return s + 1;
  }

  return s;
}

/**
 * 
 * simple command line lexer
 * split returns 0 when spliting success.
 * returns -1 when syntax error ocuured.
 * return -2 when split inner problem (argv memory limit, not implemented yet)
 */

int lexer(char* cmd, char argv[15][50], int* argc) {
  // current state respresents state after consuming cmd[0, pos) before proccessing cmd[pos].
  State state = STATE_START; 
  int pos = 0;

  int out = 0;
  int in = 0;

  while (pos < 150) {
    char c = cmd[pos];

    if (c == '\0') break;
    // if (out >= 15 || in >= 50) return -2; 상태 안에서 처리해야할듯.

    switch (state) {
      case STATE_START:
        if (c == ' ') state = STATE_START; // not gap, "  hello" => "hello"
        else if (c == '\"') {
          argv[out][in++] = c;
          state = STATE_DQUOTE;
        } else {
          argv[out][in++] = c;
          state = STATE_WORD;
        }

        break;
      case STATE_GAP:
        if (c != ' ') {
          argv[out][in] = '\0';

          out += 1;
          in = 0;

          if (c == '\"') state = STATE_DQUOTE;
          else state = STATE_WORD;

          argv[out][in++] = c;
        }

        break;
      case STATE_WORD:
        if (c == ' ') state = STATE_GAP;
        else if (c == '\"') {
          return -1; 
        }
        else argv[out][in++] = c;

        break;
      case STATE_DQUOTE:
        if (c != '\"') {
          argv[out][in++] = c;
        } else {
          argv[out][in++] = c;
          state = STATE_FIN_DQUOTE;
        }

        break;
      case STATE_FIN_DQUOTE:
        if (c == ' ') state = STATE_GAP;
        else return -1;

        break;
    }

    pos += 1;
  }

  if (state == STATE_DQUOTE) return -1;
  if (state != STATE_START) {
    argv[out][in] = '\0';
  }

  *argc = state == STATE_START ? 0 : out + 1;

  return 0;
}

int parse_and_excute(char argv[15][50], int argc) {
  pid_t pid_list[15];
  int pid_idx = 0;
  
  char* inp_redirection = NULL;
  char* opt_redirection = NULL;

  int prev_pipe_read_fd = -1;

  char* unit_cmd[16] = {NULL, };
  int unit_cmd_idx = 0;

  int i = 0;

  while (i < argc) {
    if (strcmp(argv[i], ">") == 0) {
      opt_redirection = unquote(argv[++i]);
    } else if (strcmp(argv[i], "<") == 0) {
      inp_redirection = unquote(argv[++i]);
    } else if (strcmp(argv[i], "|") == 0) {
      int pipefd[2];
      pipe(pipefd);

      pid_t pid = fork();

      if (pid != 0) { // parent
        pid_list[pid_idx++] = pid;

        memset(unit_cmd, 0, sizeof(unit_cmd));
        unit_cmd_idx = 0;
        
        if (prev_pipe_read_fd != -1) 
          close(prev_pipe_read_fd);
        close(pipefd[1]);

        prev_pipe_read_fd = pipefd[0];
        opt_redirection = NULL;
        inp_redirection = NULL;
      } else { // child
        if (prev_pipe_read_fd != -1) {
          dup2(prev_pipe_read_fd, STDIN_FILENO);
          close(prev_pipe_read_fd);
        } else if (inp_redirection != NULL) {
          int redirection_fd = open(inp_redirection, O_RDONLY);

          dup2(redirection_fd, STDIN_FILENO);
          close(redirection_fd);
        }

        dup2(pipefd[1], STDOUT_FILENO);

        close(pipefd[0]);
        close(pipefd[1]);

        execvp(unit_cmd[0], unit_cmd);

        perror(unit_cmd[0]);
        _exit(127);
      }
    } else {
      if (argv[i][0] == '\"') {
        unit_cmd[unit_cmd_idx++] = unquote(argv[i]);
      } else {
        unit_cmd[unit_cmd_idx++] = &argv[i][0];
      }
    }
  
    i++;
  }

  if (strcmp(unit_cmd[0], "cd") == 0) {
    if (argc < 2) {
      printf("error: argv lacks\n");

      return -1;
    }

    chdir(unit_cmd[1]);
    return 0;
  }

  pid_t pid = fork();

  if (pid != 0) {
    pid_list[pid_idx++] = pid;

    if (prev_pipe_read_fd != -1) close(prev_pipe_read_fd);
  } else {
    if (prev_pipe_read_fd != -1) {
      dup2(prev_pipe_read_fd, STDIN_FILENO);
      close(prev_pipe_read_fd);
    } else if (inp_redirection != NULL) {
      int redirection_fd = open(inp_redirection, O_RDONLY);

      dup2(redirection_fd, STDIN_FILENO);
      close(redirection_fd);
    }

    if (opt_redirection != NULL) {
      int redirection_fd = open(opt_redirection, O_WRONLY | O_CREAT | O_TRUNC, 0666);

      dup2(redirection_fd, STDOUT_FILENO);
      close(redirection_fd);
    }

    execvp(unit_cmd[0], unit_cmd);

    perror(unit_cmd[0]);
    _exit(127);
  }


  for (int i = 0; i < pid_idx; i++) {
    waitpid(pid_list[i], NULL, 0);
  }

  return 0;
}




int main() {
  while (true) {
    char cwd[150];
    char cmd[150];

    getcwd(cwd, sizeof(cwd));

    printf("sh:%s > ", cwd);

    if (fgets(cmd, sizeof(cmd), stdin) == NULL) break;

    cmd[strcspn(cmd, "\n")] = '\0';

    int argc;
    char argv[15][50];

    if (lexer(cmd, argv, &argc) < 0) {
      fprintf(stderr, "sh: syntax error\n");
      continue;
    }

    if (argc == 0) continue;;

    parse_and_excute(argv, argc);
  }
}