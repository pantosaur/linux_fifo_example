/*
Copyright (c) 2020 Pierre A. Sauriol

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>

#include "common.h"

#define usage()\
  dprintf(STDERR_FILENO, "usage: ");\
  dprintf(STDERR_FILENO, argv[0]);\
  dprintf(STDERR_FILENO, " fifo_i fifo_o\n");\
  exit(EXIT_FAILURE);

#define BUFLEN 8192
#define TIMEOUT 1000

int delete_pipes(char** pnames, int npipes){
  struct stat sb;
  for(int i=0; i<npipes; i++){
    if(stat(pnames[i], &sb) != -1 && (sb.st_mode & S_IFMT) == S_IFIFO){
      if(remove(pnames[i])) perror("remove");  
    }
  }
}

int create_pipes(char** pnames, int npipes){
  struct stat sb;
  for(int i=0; i<npipes; i++){
    if(stat(pnames[i], &sb) == -1){
      switch(errno){
	case ENOENT:
	  if (mkfifo(pnames[i], 0666)){
	    perror("mkfifo");
	    delete_pipes(pnames, i);
	    return 0;
	  } 
	  break;
	default:
	  perror("stat");
	  delete_pipes(pnames, i);
	  return 0;
      }
    }
    else if((sb.st_mode & S_IFMT) != S_IFIFO){
      dprintf(STDERR_FILENO, "could not create pipe %s\n", pnames[i]);
      delete_pipes(pnames, i);
      return 0;
    }
  }
  return 1;
}

int main(int argc, char* argv[]){
  char wfifo[10], rfifo[10];
  char buf[BUFLEN];
  char stdinbuf[BUFLEN];
  struct signalfd_siginfo info;
  int rfd,wfd;
  int in_c=0;

  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);

  opterr = 0;
  int opt;
  if ((opt = getopt(argc, argv, "")) != -1 || argc != 3){
    usage();
  }
  if (!create_pipes(argv+1, 1))
    exit(EXIT_FAILURE);

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGPIPE);
  sigprocmask(SIG_BLOCK, &sigset, NULL);


  struct pollfd* fds = malloc(4*sizeof(struct pollfd));
  fds[0].events=POLLIN;
  fds[1].events=POLLOUT;
  fds[2].events=POLLIN;
  fds[3].events=POLLIN;
  
  fds[0].fd = open(argv[1], O_RDONLY | O_NONBLOCK);
  fds[1].fd = open(argv[2], O_WRONLY | O_NONBLOCK);

  if((fds[2].fd = signalfd(-1, &sigset, 0)) == -1)
    handle_error("signalfd");

  fds[3].fd = 0;


  int n;
  while(1){
    if((n = poll(fds, 4, TIMEOUT)) == -1)
      handle_error("poll");

    if (fds[0].revents & POLLIN){
      int b=0;
      b=read(fds[0].fd, buf, BUFLEN-1);
      buf[b] = '\0';
      printf("%s", buf);
    }
    if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)){
      int b=0;
      b=read(fds[0].fd, buf, BUFLEN);
      buf[b] = '\0';
      printf("%s", buf);
      close(fds[0].fd);
      fds[0].fd = -1;
    }


    if (fds[1].revents & POLLOUT){
      int b;
      if ((b=write(fds[1].fd, stdinbuf, in_c)) == -1 && errno != EPIPE)
	handle_error("write");
      if((in_c-=b) == 0)
	fds[1].events &= ~POLLOUT;
    }
    if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)){
      close(fds[1].fd);
      fds[1].fd = -1;
    }

    if (fds[2].revents & POLLIN){
      read(fds[2].fd, &info, sizeof(info));
      switch(info.ssi_signo){
	case SIGINT:
	  printf("received SIGINT, exiting...\n");
	  for(int i=0; i<3; i++){
	    if(fds[i].fd >= 3 && close(fds[i].fd) == -1)
	      perror("close");
	  }
	  free(fds);
	  delete_pipes(argv+1, 1);
	  return 0;
	case SIGPIPE:
	  dprintf(STDERR_FILENO, "read end of pipe closed\n");
	  break;
	default:
	  //should not happen
	  printf("ERROR: %s\n", strsignal(info.ssi_signo));
	  exit(1);
      }
    }
    if (fds[2].revents & (POLLHUP | POLLERR | POLLNVAL))
      break;

    if (fds[3].revents & POLLIN){
      //TODO: do something if buffer is full
      if((in_c+=read(fds[3].fd, stdinbuf+in_c, BUFLEN - in_c)) == -1)
	handle_error("read");
      if(fds[1].fd < 0)
	in_c = 0;
      else
	fds[1].events |= POLLOUT;
    }
    if (fds[3].revents & (POLLHUP | POLLERR | POLLNVAL))
      break;

    if (fds[0].fd < 0){
      fds[0].fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    }
    if (fds[1].fd < 0){
      fds[1].fd = open(argv[2], O_WRONLY | O_NONBLOCK);
    }
  } 
  for(int i=0; i<3; i++){
    if(fds[i].fd >= 3 && close(fds[i].fd) == -1)
      perror("close");
  }
  free(fds);
  delete_pipes(argv+1, 2);
  return 0;
}
