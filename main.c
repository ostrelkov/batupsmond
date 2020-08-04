/*
 * Short-Description: battery UPS monitor daemon
 * Description:       The batupsmond service daemon is able to monitor
 *                    battery status. If external power supply is absent
 *                    and battery is discharging then daemon execute /sbin/pwrdown
 *                    script to power off
 *
 * pwrdown script:
#!/bin/sh
sleep $1
poweroff
 *
 * Author: Oleg Strelkov <o.strelkov@gmail.com>
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>
/*
*/
#include <linux/ioctl.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

#include <linux/input.h>
#include <syslog.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>

//#define DEBUG_PRINT

#define SOFTWARE_VER        "0.4"

#define CHARGE_STATUS       "/sys/class/power_supply/battery/status"
#define BATTERY_CAPACITY    "/sys/class/power_supply/battery/capacity"
#define BATTERY_PRESENT     "/sys/class/power_supply/battery/present"

#define die(str, args...) do { \
        perror(str); \
        exit(EXIT_FAILURE); \
    } while(0)

#if defined(DEBUG_PRINT)
  #define print_dbg(args...)	printf(args)
#else
  #define print_dbg(args...) 	do { } while (0)
#endif

int daemonize = 1;
const char *poweroff_cmd[] = {"/sbin/pwrdown" , NULL};
int false_alarm_sec = -1;

unsigned char read_capacity(void)
{
	int fd;
	char cap[4];

	fd = open(BATTERY_CAPACITY, O_RDONLY);
    if(fd < 0) {
        die("Input file opening error");
    }
	read (fd, &cap, sizeof(cap));
	close (fd);
	cap[3] = '\0';
	return atoi(cap);
}

int charging(void)
{
	int fd;
	char first_ch;

	fd = open(CHARGE_STATUS, O_RDONLY);
    if(fd < 0) {
        die("Input file opening error");
    }
	read (fd, &first_ch, sizeof(first_ch));
	close (fd);
	/* 'C'harging/'F'ull or 'D'ischarging */
	return (first_ch == 'D') ? 0 : 1;
}

void usage (char *name) {
	fprintf(stderr, "%s ver.%s\n", name, SOFTWARE_VER);
	fprintf(stderr, "Usage: %s [-n] [-f sec]\n", name);
	fprintf(stderr,
		"-n - nodaemon\n"
		"-h - help\n"
		"-f sec - false alarm duration in seconds\n"
         "\n");
}

void parse_command_line (int argc, char **argv) {
	extern char *optarg;
	struct option long_options[] = {
		{"nodaemon", 0, NULL, 'n'},
		{"help", 0, NULL, 'h'},
		{"false-alarm-duration", 1, NULL, 'f'},
		{NULL, 0, NULL, 0}
	};
	int c = 0;

	while (c != -1) {
		c=getopt_long(argc, argv, "nhf:", long_options, NULL);
		switch (c) {
			case 'n':
				daemonize = 0;
				break;
			case 'h':
				usage(argv[0]);
				exit(0);
				break;
			case 'f':
                {
                    char *end;

                    false_alarm_sec = (int)strtol(optarg, &end, 10);
                    if (false_alarm_sec < 0) {
                        fprintf(stderr, "Error: false alarm duration should be > 0\n");
                        usage(argv[0]);
                        exit(3);
                    }
                }
				break;
		}
	}

	if (optind < argc) {
		usage(argv[0]);
		exit(1);
	}

    if (false_alarm_sec == -1) {
        fprintf(stderr, "Error: duration is not defined\n");
        usage(argv[0]);
        exit(2);
    }

}

void cleanup (int signum)
{
	if (daemonize) {
	    // do nothing
        syslog(LOG_NOTICE, "daemon is stopped\n");
	} else {
        syslog(LOG_NOTICE, "is breaked, bye\n");
	}
	closelog();
	exit(0);
}

void loadcontrol (int signum) {

    syslog(LOG_NOTICE, "SIGHUP signal is received\n");
    // what can we do?
	signal(SIGHUP, loadcontrol);
}

static int exec_prog(const char **argv)
{
    pid_t   my_pid;
    int     status, timeout;

    print_dbg("Before fork, parent pid=%d\n", getpid());
    my_pid = fork();
    if (my_pid == -1) {
        perror("fork");
    } else if (my_pid == 0) {
        print_dbg("It is the child, pid=%d\n", getpid());
        if (-1 == execve(argv[0], (char **)argv , NULL)) {
            perror("child process execve failed [%m]");
            return -1;
        }
    } else {
        print_dbg("It is the parent\n");
        timeout = 40; // 20 sec
        while (0 == waitpid(my_pid , &status , WNOHANG)) {
            if (--timeout < 0) {
                perror("timeout");
                return -1;
            }
            usleep(500000);
        }
/*
        printf("%s WEXITSTATUS %d WIFEXITED %d [status %d]\n",
                argv[0], WEXITSTATUS(status), WIFEXITED(status), status);
*/
        if (1 != WIFEXITED(status) || 0 != WEXITSTATUS(status)) {
            perror("%s failed, halt system");
            return -1;
        }
        print_dbg("Done waiting: timeout=%d\n", timeout);
    }
    return 0;
}

int main(int argc, char **argv) {

    int fd_in;
    char ch;
    unsigned char capacity, prev_cap, is_charging, prev_chrg;
    int timeout = false_alarm_sec;
    int powering_off = 0;

	parse_command_line(argc, argv);
	openlog("battery_UPS", LOG_PID | (daemonize ? 0 : LOG_PERROR), LOG_DAEMON);

	/* Set up a signal handler for SIGTERM to clean up things. */
	signal(SIGTERM, cleanup);
	/* And a handler for SIGHUP, to reaload control file. */
	signal(SIGHUP, loadcontrol);

	if (daemonize) {
		if (daemon(0,0) == -1) {
			perror("daemon");
            closelog();
			exit(1);
		}
        syslog(LOG_NOTICE, "started in daemon mode\n");
	} else {
        syslog(LOG_NOTICE, "started in non-daemon mode\n");
	}

    fd_in = open(BATTERY_PRESENT, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0) {
        perror("open");
        closelog();
        exit(1);
    }
	read (fd_in, &ch, sizeof(ch));
	close (fd_in);
	if (ch == '1') {
        syslog(LOG_NOTICE, "battery is present\n");
	} else {
        syslog(LOG_NOTICE, "battery is absent, bye\n");
        closelog();
        exit(1);
	}

    while (1) {

        prev_chrg = is_charging;
        is_charging = charging();
        if (is_charging) {
            sleep(1);
            prev_cap = capacity;
            capacity = read_capacity();
            print_dbg("Charging: capacity now: %d\n", capacity);
            timeout =  false_alarm_sec;
            powering_off = 0;
            if ((prev_cap != capacity) || (prev_chrg != is_charging))  {
                syslog(LOG_NOTICE, "charging: capacity = %d\n", capacity);
            }
        } else {
            if (!powering_off) {
                syslog(LOG_NOTICE, "discharging: capacity = %d, countdown = %d\n", capacity, timeout);
                timeout--;
                if (timeout <= 0) {
                    syslog(LOG_NOTICE, "external power fail, powering off\n");
                    if (exec_prog(poweroff_cmd)) {
                        syslog(LOG_NOTICE, "error of daemon execution\n");
                    } else {
                        powering_off = 1;
                    }
                }
                sleep(1);
            }
        }

    }

    return 0;
}
