#include <errno.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in.h>
#include <regex.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

static const int listenport = 4949;
static const char* configfile = "/etc/munin/munin-node.conf";
static const char* pidfile = "/var/run/munin-hardcore.pid";

static sigjmp_buf client_timeout;

void sigalrmhandler(int signal)
{
  siglongjmp(client_timeout, 1);
}

static regex_t ignore_file[8];
static int ignore_file_count;

static regex_t allow_ip[8];
static int allow_ip_count;

static void parse_config()
{
  FILE* config = fopen(configfile, "r");

  if(!config)
  {
    fprintf(stderr, "Failed to open %s: %s\n", configfile, strerror(errno));

    exit(EXIT_FAILURE);
  }

  char line[256];
  char* key;
  char* value;

  while(fgets(line, sizeof(line), config))
  {
    char* c;

    line[sizeof(line) - 1] = 0;

    if((c = strchr(line, '#')) != 0)
      *c = 0;

    c = line;

    while(isspace(*c))
      ++c;

    if(!*c)
      continue;

    key = c;

    value = strchr(key, ' ');

    if(!value)
      continue;

    *value++ = 0;

    c = strchr(value, '\n');

    if(c)
      *c = 0;

    if(!strcmp(key, "ignore_file"))
    {
      if(ignore_file_count == sizeof(ignore_file) / sizeof(ignore_file[0]))
      {
        fprintf(stderr, "Too many %s!\n", key);

        exit(EXIT_FAILURE);
      }

      if(0 == regcomp(&ignore_file[ignore_file_count], value, 0))
        ++ignore_file_count;
    }
    else if(!strcmp(key, "allow"))
    {
      if(allow_ip_count == sizeof(allow_ip) / sizeof(allow_ip[0]))
      {
        fprintf(stderr, "Too many %s!\n", key);

        exit(EXIT_FAILURE);
      }

      if(0 == regcomp(&allow_ip[allow_ip_count], value, 0))
        ++allow_ip_count;
    }
  }

  fclose(config);
}

int main(int argc, char** argv)
{
  char* fqdn;
  char* c;
  int i;
  int listenfd;
  int result;
  struct sockaddr_in address;

  if(argc != 2)
  {
    fprintf(stderr, "Use %s <FQDN>!\n", argv[0]);

    return EXIT_FAILURE;
  }

  nice(19);

  fqdn = argv[1];

  parse_config();

  {
    FILE* pf = fopen(pidfile, "w");
    if(pf)
    {
      fprintf(pf, "%d\n", getpid());
      fclose(pf);
    }
  }

  signal(SIGCHLD, SIG_DFL);
  signal(SIGPIPE, SIG_IGN);

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(listenport);

  if(-1 == (listenfd = socket(PF_INET, SOCK_STREAM, 0)))
  {
    perror(PACKAGE_NAME ": socket(PF_INET, SOCK_STREAM, 0)");

    return EXIT_FAILURE;
  }

  {
    int one = 1;

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  }

  if(-1 == bind(listenfd, (struct sockaddr*) &address, sizeof(address)))
  {
    perror(PACKAGE_NAME ": bind");

    return EXIT_FAILURE;
  }

  if(-1 == listen(listenfd, 16))
  {
    perror(PACKAGE_NAME ": listen");

    return EXIT_FAILURE;
  }

  daemon(0, 0);

  for(;;)
  {
    struct sockaddr_in addr;
    socklen_t addrlen;
    char buffer[128];

    int clientfd = accept(listenfd, (struct sockaddr*) &addr, &addrlen);

    if(clientfd == -1)
      continue;

    c = inet_ntoa(addr.sin_addr);

    for(i = 0; i < allow_ip_count; ++i)
    {
      if(REG_NOMATCH != regexec(&allow_ip[i], c, 0, 0, 0))
        break;
    }

    if(i == allow_ip_count)
    {
      close(clientfd);

      continue;
    }

    if(sigsetjmp(client_timeout, 1))
    {
      close(clientfd);

      continue;
    }

    signal(SIGALRM, sigalrmhandler);

    FILE* fs = fdopen(clientfd, "w+");

    fprintf(fs, "# " PACKAGE_NAME " node at %s\n", fqdn);

    for(;;)
    {
      fflush(fs);

      alarm(10);
      result = read(clientfd, buffer, sizeof(buffer) - 1);
      alarm(0);

      if(result <= 0)
        break;

      buffer[result - 1] = 0;

      // list, nodes, config, fetch, version or quit

      if(!strncmp("list ", buffer, 5))
      {
        if(strcmp(buffer + 5, fqdn))
        {
          fprintf(fs, "\n");

          continue;
        }

        DIR* dir = opendir("/etc/munin/plugins");

        if(dir)
        {
          struct dirent* ent;

          while((ent = readdir(dir)) != 0)
          {
            for(i = 0; i < ignore_file_count; ++i)
            {
              if(REG_NOMATCH != regexec(&ignore_file[i], ent->d_name, 0, 0, 0))
                break;
            }

            if(i != ignore_file_count)
              continue;

            if(ent->d_name[0] == '.')
              continue;

            fprintf(fs, "%s ", ent->d_name);
          }

          fprintf(fs, "\n");

          closedir(dir);
        }
      }
      else if(!strcmp("nodes", buffer))
      {
        fprintf(fs, "%s\n.\n", fqdn);
      }
      else if(!strncmp("config ", buffer, 7) || !strncmp("fetch ", buffer, 6))
      {
        if(strchr(buffer, '/'))
          break;

        pid_t child = fork();

        if(child < 0)
        {} /* XXX: log this */

        if(child == 0)
        {
          char* args[3];

          dup2(clientfd, STDOUT_FILENO);

          c = strchr(buffer, ' ') + 1;

          asprintf(&args[0], "/etc/munin/plugins/%s", c);

          if(buffer[0] == 'c')
          {
            args[1] = "config";
            args[2] = 0;
          }
          else
            args[1] = 0;

          execve(args[0], args, environ);

          return EXIT_FAILURE;
        }

        int status;

        waitpid(child, &status, 0);

        fprintf(fs, ".\n");
      }
      else if(!strcmp("version", buffer))
      {
        fprintf(fs, PACKAGE_NAME " node on %s version: %s\n", fqdn, PACKAGE_VERSION);
      }
      else if(!strcmp("quit", buffer))
      {
        break;
      }
      else
      {
        fprintf(fs, "# Unknown command. Try list, nodes, config, fetch, version or quit\n");
      }
    }

    fclose(fs);

    close(clientfd);
  }

  return EXIT_SUCCESS;
}
