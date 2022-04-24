
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAXLINE    80
#define MAXARGS     80
#define MAXPROCESS  5
#define MAXPROCESSID    1<<5

#define EMPTY 0
#define FG 1
#define BG 2
#define DONE 3

int next_proc_id = 1;

struct process {
  pid_t pid;
  int proc_id;
  int condition;
  char cmdline[MAXLINE];
};
struct process all_proc[MAXPROCESS];


void eval(char *cmdline);
int builtinCommand(char **argv);
void fgCommand(char **argv);
void bgCommand(char** argv);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
int parseline(const char *cmdline, char **argv);
void create_proc(struct process* all_proc);
int max_proc_id(struct process* all_proc);
void add_proc(struct process* all_proc, pid_t pid, int condition, char *cmdline);
pid_t has_fg(struct process* all_proc);
void display(struct process* all_proc);
void executeRedirection(int carrot1, int carrot2, int carrot1index, int carrot2index, char **argv);


int main(int argc, char** argv)
{
    create_proc(all_proc);

    char cmdline[MAXLINE];

    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGCHLD, sigchld_handler);

    while (1) 
    {
        printf("prompt> ");
        fgets(cmdline, MAXLINE, stdin);
        if (feof(stdin)) {
            exit(0);
        }
        eval(cmdline);
    }

    exit(0);
}

void eval(char *cmdline)
{
  pid_t pid;
  char *argv[MAXARGS];
  int bg = parseline(cmdline,argv);

  if(argv[0] == 0)
    return;

  int carrot1 = 0; // <
  int carrot2 = 0; // >
  int carrot1index = -1;
  int carrot2index = -1;
  int i;
  
  if(!builtinCommand(argv)) 
  {
    pid_t pid;
    pid = fork();

    if(pid == 0) 
    {
      setpgid(0,0);
      for (i = 0; i < MAXARGS; i++)
      {
        if (argv[i] == NULL)
        {
          break;
        }
        else if (strcmp(argv[i], "<") == 0)
        {
          carrot1 = 1;
          carrot1index = i;
        }
        else if (strcmp(argv[i], ">") == 0)
        {
          carrot2 = 1;
          carrot2index = i;
        }
      }
      executeRedirection(carrot1, carrot2, carrot1index, carrot2index, argv);
    
      if (execv(argv[0], argv) < 0 && execvp(argv[0], argv) < 0)
      {
	        printf("%s: Command not found.\n", argv[0]);
	        exit(0);
      }
    }

    if(!bg) 
    {
      add_proc(all_proc, pid, FG, cmdline);
      int stat;
      pid_t currentId = waitpid(pid, &stat, WUNTRACED);

      for (i = 0; i < MAXPROCESS; i++) 
      {
          if (all_proc[i].pid == currentId && all_proc[i].condition != 3)
          {
              all_proc[i].pid = 0;
              all_proc[i].proc_id = 0;
              all_proc[i].condition = EMPTY;
              strcpy(all_proc[i].cmdline, "\0");
              next_proc_id = max_proc_id(all_proc) + 1;
              return;
          }
      }
    }
    else 
    {
      add_proc(all_proc, pid, BG, cmdline);
    }
  }
  return;
  
}

void executeRedirection(int carrot1, int carrot2, int carrot1index, int carrot2index, char **argv) //carrot1 = <, carrot2 = >
{
  mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;
  if (carrot1 == 1 && carrot2 == 1)
  {
    int inFileId = open(argv[carrot1index + 1], O_RDONLY, mode);
    int outFileID = open(argv[carrot2index + 1], O_CREAT | O_WRONLY | O_TRUNC, mode);
    dup2(inFileId, STDIN_FILENO);
    dup2(outFileID, STDOUT_FILENO);
    argv[carrot2index] = NULL;
    argv[carrot2index + 1] = NULL;
    argv[carrot1index] = NULL;
    argv[carrot1index + 1] = NULL;
  }
  else if (carrot1 == 1 && carrot2 == 0)
  {
    int inFileId = open(argv[carrot1index + 1], O_RDONLY, mode);
    dup2(inFileId, STDIN_FILENO);
    argv[carrot1index] = NULL;
    argv[carrot1index + 1] = NULL;
  }
  else if (carrot1 == 0 && carrot2 == 1)
  {
    int outFileID = open(argv[carrot2index + 1], O_CREAT | O_WRONLY | O_TRUNC, mode);
    dup2(outFileID, STDOUT_FILENO);
    argv[carrot2index] = NULL;
    argv[carrot2index + 1] = NULL;
  }
  
}

int parseline(const char *cmdline, char **argv)
{
  static char array[MAXLINE];
  char *buf = array;
  char *delim;
  int argc;
  int bg;
  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';
  while (*buf && (*buf == ' '))
    buf++;

  argc = 0;

  while ((delim = strchr(buf, ' '))) 
  {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) 
      buf++;
  }
  argv[argc] = NULL;

  if (argc == 0)
    return 1;

  if ((bg = (*argv[argc-1] == '&')) != 0) 
  {
    argv[--argc] = NULL;
  }
  return bg;
}

int builtinCommand(char **argv)
{
  pid_t pid;

  if(strcmp(argv[0],"quit")==0) 
  {
	  exit(0);
  }

  else if(strcmp(argv[0],"jobs")==0) 
  {
    display(all_proc);
    return 1;
  }

  else if(strcmp(argv[0],"fg")==0) 
  {
    fgCommand(argv);
    return 1;
  }

  else if(strcmp(argv[0],"bg")==0) 
  {
    bgCommand(argv);
    return 1;
  }
  else if(strcmp(argv[0],"kill")==0)
  {
    char *cop = argv[1];
    char temp = cop[0];
    if (temp == '%')
    {
      char num = cop[1];
      int id = num - '0';
      int i;
      for (i = 0; i < MAXPROCESS; i++)
      {
        if (all_proc[i].proc_id == id)
        {
          kill(all_proc[i].pid, SIGINT);
          all_proc[i].pid = 0;
          all_proc[i].proc_id = 0;
          all_proc[i].condition = EMPTY;
          strcpy(all_proc[i].cmdline, "\0");
          next_proc_id = max_proc_id(all_proc) + 1;
          return 1;
        }
      }
    }

    else
    {
      int id = atoi(argv[1]);
      int i;
      for (i = 0; i < MAXPROCESS; i++)
      {
        if (all_proc[i].pid == id)
        {
          kill(all_proc[i].pid, SIGINT);
          all_proc[i].pid = 0;
          all_proc[i].proc_id = 0;
          all_proc[i].condition = EMPTY;
          strcpy(all_proc[i].cmdline, "\0");
          next_proc_id = max_proc_id(all_proc) + 1;
          return 1;
        }
      }
    }
    return 1;
  }
  return 0;
}

void fgCommand(char **argv)
{
  char *cop = argv[1];
  char temp = cop[0];
  if (temp == '%')
  {
    char num = cop[1];
    int id = num - '0';
    int i;
    for (i = 0; i < MAXPROCESS; i++)
    {
      if (all_proc[i].proc_id == id)
      {
        all_proc[i].condition = FG;
        kill(all_proc[i].pid, SIGCONT);
        int stat;
        waitpid(all_proc[i].pid, &stat, WUNTRACED);
        return;
      }
    }
  }

  else
  {
    int id = atoi(argv[1]);
    int i;
    for (i = 0; i < MAXPROCESS; i++)
    {
      if (all_proc[i].pid == id)
      {
        all_proc[i].condition = FG;
        kill(all_proc[i].pid, SIGCONT);
        int stat;
        waitpid(all_proc[i].pid, &stat, WUNTRACED);
        return;
      }
    }
  }
}

void bgCommand(char** argv) 
{
  char *cop = argv[1];
  char temp = cop[0];
  if (temp == '%')
  {
    char num = cop[1];
    int id = num - '0';
    int i;
    for (i = 0; i < MAXPROCESS; i++)
    {
      if (all_proc[i].proc_id == id && all_proc[i].condition == DONE)
      {
        all_proc[i].condition = BG;
        kill(all_proc[i].pid, SIGCONT);
        return;
      }
    }
  }

  else
  {
    int id = atoi(argv[1]);
    int i;
    for (i = 0; i < MAXPROCESS; i++)
    {
      if (all_proc[i].pid == id && all_proc[i].condition == DONE)
      {
        all_proc[i].condition = BG;
        kill(all_proc[i].pid, SIGCONT);
        return;
      }
    }
  }
}

void sigchld_handler(int sig)
{
    pid_t child_pid;
    int status;
    
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        if (WIFSTOPPED(status)) 
        {
          int i;
          for (i = 0; i < MAXPROCESS; i++)
          {
            if (all_proc[i].pid == child_pid)
            {
              all_proc[i].condition = DONE;
              return;
            }
          }
        }

        else if (WIFSIGNALED(status) || WIFEXITED(status)) 
        {
            int i;
            for (i = 0; i < MAXPROCESS; i++) 
            {
                if (all_proc[i].pid == child_pid)
                {
		                all_proc[i].pid = 0;
		                all_proc[i].proc_id = 0;
		                all_proc[i].condition = EMPTY;
		                strcpy(all_proc[i].cmdline, "\0");
                    next_proc_id = max_proc_id(all_proc) + 1;
                    return;
                }
            }
        }
        else
        {
            fprintf(stdout, "%s\n", "waitpid error");
        }
    }
    return;
}

void sigint_handler(int sig)
{
  pid_t pid;

  pid = has_fg(all_proc);

  if(pid > 0) 
  {
    kill(pid, SIGINT);
    int i;
    for (i = 0; i < MAXPROCESS; i++) 
    {
      if (all_proc[i].pid == pid)
      {
          all_proc[i].pid = 0;
          all_proc[i].proc_id = 0;
          all_proc[i].condition = EMPTY;
          strcpy(all_proc[i].cmdline, "\0");
          next_proc_id = max_proc_id(all_proc) + 1;
          return;
      }
    }
  }
}

void sigtstp_handler(int sig)
{
  pid_t pid;

  pid = has_fg(all_proc);
  if(pid > 0) 
  {
    kill(pid, SIGTSTP);
    int i;
    for (i = 0; i < MAXPROCESS; i++)
    {
      if (all_proc[i].pid == pid)
      {
        all_proc[i].condition = DONE;
        return;
      }
    }
  }
}

void create_proc(struct process* all_proc) 
{
    int i;
    for (i = 0; i < MAXPROCESS; i++) 
    {
        all_proc[i].pid = 0;
        all_proc[i].proc_id = 0;
        all_proc[i].condition = EMPTY;
        strcpy(all_proc[i].cmdline, "\0");
	  }
}

int max_proc_id(struct process* all_proc)
{
  int i, max = 0;

  for (i = 0; i < MAXPROCESS; i++)
  {
    if (all_proc[i].proc_id > max)
    {
      max = all_proc[i].proc_id;
    }
  }
  return max;
}

void add_proc(struct process* all_proc, pid_t pid, int condition, char* cmdline)
{
    int i;
    for (i = 0; i < MAXPROCESS; i++) 
    {
        if (all_proc[i].pid == 0) 
        {
            all_proc[i].pid = pid;
            all_proc[i].condition = condition;
            all_proc[i].proc_id = next_proc_id++;
            if (next_proc_id > MAXPROCESS)
            {
                next_proc_id = 1;
            }
            strcpy(all_proc[i].cmdline, cmdline);
            return;
        }
    }
}

pid_t has_fg(struct process* all_proc) 
{
  int i;

  for (i = 0; i < MAXPROCESS; i++)
  {
    if (all_proc[i].condition == FG)
    {
      return all_proc[i].pid;
    }
  }
  return 0;
}

void display(struct process *all_proc)
{
  int i;
  for (i = 0; i< MAXPROCESS; i++)
  {
    if (all_proc[i].condition != EMPTY) 
    {
      printf("[%d] (%d) ", all_proc[i].proc_id, all_proc[i].pid);

      if(all_proc[i].condition == 2)
      {
	        printf("Running ");
      }
      else if (all_proc[i].condition == 3)
      {
	        printf("Stopped ");
      }
      else
      {
	        printf("error");
      }
      printf("%s", all_proc[i].cmdline);
    }
  }
}