
// i3-bw
// This software is licensed under the GPL.
// Author: lee@yun.yagibdah.de, 2012-09-28
//
// This program outputs some system information to be displayed in the
// status bar of the i3 window manager.  I wrote it because I wanted
// the up- and downstream bandwidth usage displayed in the bar and the
// i3-status program only displays the link speed.
//
// compile with something like:
//   gcc -march=native -Wall -O2 i3-bw.c -o i3-bw
//
// First run the program from the commandline to check if you get any
// errors:
//
// ./i3-bw 1 /sys/class/net/eth1/statistics/rx_bytes /sys/class/net/eth1/statistics/tx_bytes
//
// Replace "eth1" with the inferface you want the bandwidth usage displayed
// for. It it works, make an entry in your i3 configuration like:
//
// bar {
//   status_command path/to/i3-bw 5 /sys/class/net/eth1/statistics/rx_bytes /sys/class/net/eth1/statistics/tx_bytes
// }
//
// That updates the info in the bar every 5 seconds, giving you the average
// bandwidth usage over the last 5 seconds.
//


#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/sysinfo.h>


#define BUFFSIZE 128
#define KIB (1024)


typedef struct {
  unsigned long rx;
  unsigned long tx;
  struct timeval tv;
} rxtx;

typedef struct {
  FILE *rx;
  FILE *tx;
  char *buffer;
} filestuff;

typedef struct {
  double rxbw;
  double txbw;
  time_t timestamp;
} bandwidth;

typedef struct {
  int days;
  int hours;
  int minutes;
  int seconds;
} updays;


int getstat(filestuff *f, rxtx *s) {
  int ret = 0;
  int c;
  unsigned long offset;

  if(gettimeofday( &(s->tv), NULL) ) {
    perror("gettimeofday");
    ret = 3;
  }
  else {
    bzero(f->buffer, BUFFSIZE);
    offset = 0;
    while( (  (   c = fgetc(f->rx) ) != EOF) && offset < BUFFSIZE) {
      *(f->buffer + offset) = c;
      offset++;
    }
    if(offset < BUFFSIZE) {
      s->rx = atol(f->buffer);

      bzero(f->buffer, BUFFSIZE);
      offset = 0;
      while( (  (   c = fgetc(f->tx) ) != EOF) && offset < BUFFSIZE) {
	*(f->buffer + offset) = c;
	offset++;
      }
      if(offset < BUFFSIZE) {
	s->tx = atol(f->buffer);
      }
      else {
	ret = 2;
      }
    }
    else {
      ret = 1;
    }
  }

  return ret;
}


void sec2days(long seconds, updays *ud) {
  ud->days = seconds / 86400;
  seconds -= 86400 * ud->days;
  ud->hours = seconds / 3600;
  seconds -= 3600 * ud->hours;
  ud->minutes = seconds / 60;
  ud->seconds = seconds - 60 * ud->minutes;
}


void output(struct sysinfo *i, bandwidth *b, double loads[] ) {
  updays up;
  char thistime[26];
  int n;

  sec2days(i->uptime, &up);
  ctime_r( &(b->timestamp), thistime);
  n = 25;
  while(thistime[n] != '\n') {
    --n;
  }
  thistime[n] = '\0';

  printf(",[{\"color\":\"#2f4f4f\",\"name\":\"uptime\",\"full_text\":\"up: %d:%02d:%02d:%02d\"}",
	 up.days, up.hours, up.minutes, up.seconds);
  printf(",{\"color\":\"#2f4f4f\",\"name\":\"load\",\"full_text\":\"%5.2f %5.2f %5.2f\"}",
	 loads[0], loads[1], loads[2] );
  printf(",{\"color\":\"#2f4f4f\",\"name\":\"procs\",\"full_text\":\"procs: %d\"}",
	 i->procs);
  printf(",{\"color\":\"#00ff00\",\"name\":\"bandwidth\",\"full_text\":\"rx: %8.4f tx: %8.4f KiB/sec\"}",
	 b->rxbw / (double)KIB, b->txbw / (double)KIB);
  printf(",{\"color\":\"#2f4f4f\",\"name\":\"time\",\"full_text\":\"%s\"}]", thistime);

  fflush(stdout);
}


void delta(rxtx *old, rxtx *new, bandwidth *bw) {
  unsigned long drx, dtx;
  double dtime;

  drx = new->rx - old->rx;
  dtx = new->tx - old->tx;
  dtime = (double)(new->tv.tv_sec - old->tv.tv_sec);
  dtime += (double)( (double)(new->tv.tv_usec - old->tv.tv_usec) / (double)1000000);

  bw->rxbw = (double)drx / dtime;
  bw->txbw = (double)dtx / dtime;
  bw->timestamp = new->tv.tv_sec;
}


int main(int argc, char *argv[] ) {
  int ret = 0;
  int tick;
  rxtx new;
  rxtx old;
  filestuff t;
  bandwidth bw;
  struct sysinfo syi;
  double loads[3];

  if(argc != 4) {
    puts("usage: i3-bw seconds /path/to/statsfile-rx /path/to/statsfile-tx");
    ret = 1;
  }
  else {
    tick = atoi(argv[1] );
    if(tick <= 0) {
      tick = 5;
    }

    t.rx = fopen(argv[2], "r");
    if(t.rx == NULL) {
      perror(argv[2] );
      ret = 2;
    }
    else {
      t.tx = fopen(argv[3], "r");
      if(t.tx == NULL) {
	perror(argv[3] );
	ret = 3;
      }
      else {
	t.buffer = malloc(BUFFSIZE);
	if(t.buffer == NULL) {
	  puts("error allocating memory");
	  ret = 7;
	}
	else {
	  if( !getstat( &t, &old) ) {
	    // the version info must be printed only once
	    // add the author to get the format right, see output() above
	    puts("{\"version\":1}\n[[{\"name\":\"author\",\"full_text\":\"i3-bw author: lee@yun.yagibdah.de\"}]");
	    fflush(stdout);
	    while(1) {
	      rewind(t.rx);
	      rewind(t.tx);
	      sleep(tick);
	      if( !getstat( &t, &new) ) {
		delta( &old, &new, &bw);
		memcpy( &old, &new, sizeof(old) );
		if(getloadavg(loads, 3) == 3) {
		  if( !sysinfo( &syi) ) {
		    output( &syi, &bw, loads);
		  }
		  else {
		    perror("sysinfo");
		    ret = 8;
		    break;
		  }
		}
		else {
		  puts("error: cannot get load average");
		  ret = 9;
		  break;
		}
	      }
	      else {
		ret = 5;
		break;
	      }
	    }
	  }
	  else {
	    ret = 6;
	  }
	}
      }
    }
  }

  if(t.rx != NULL) {
    fclose(t.rx);
  }
  if(t.tx != NULL) {
    fclose(t.tx);
  }
  if(t.buffer != NULL) {
    free(t.buffer);
  }
  return ret;
}
