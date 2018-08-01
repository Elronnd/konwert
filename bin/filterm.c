#include <stdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define VERSION "1.8"

char		*prog_name;
const char	*shell;
int		master;
int		slave;
struct termios	tt;
struct winsize	win;
char		line[] = "/dev/ptyXX";

void usage(int status) {
	fprintf(status ? stderr : stdout, "\
Usage: %s INPUT OUTPUT [COMMAND [ARGS]]\n\
Execute the specified COMMAND (default is the shell), filtering terminal\n\
input and/or output.\n\
\n\
INPUT and OUTPUT are names of konwert's filters - they are passed as\n\
the first argument to the konwert program. `%s - OUTPUT' filters\n\
only output, and `%s INPUT -' only input.\n\
\n\
The command `-' executes the shell as a login shell.\n\
\n\
The input filter will have set the environment variable FILTERM=in,\n\
and the output one - FILTERM=out. This way some filters can slightly\n\
alter their behaviour when working for filterm.\n\
\n\
In addition, the following standard options are recognized:\n\
  --help      display this help and exit\n\
  --version   output version information and exit\n", prog_name, prog_name, prog_name);

	exit(status);
}

void version(void) {
	printf("filterm, version %s\nCopyright 1998 Marcin Kowalczyk <qrczak@knm.org.pl>\n", VERSION);
	exit (0);
}

void getmaster(void) {
	char *pty = &line[strlen("/dev/ptyp")];

	for (const char *bank = "pqrs"; *bank; bank++) {
		line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		struct stat stb;
		if (stat(line, &stb) < 0) {
			break;
		}

		for (const char *cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			if ((master = open (line, O_RDWR)) >= 0) {
				char *tp = &line[strlen("/dev/")];
				*tp = 't';

				int ok = !access(line, R_OK|W_OK);
				*tp = 'p';

				if (ok) {
					tcgetattr(0, &tt);
					ioctl(0, TIOCGWINSZ, &win);
					return;
				}
				close(master);
			}
		}
	}
	printf("Out of pty's\n");
	exit(1);
}

// if anyone knows polish and wants to translate this, I'd be grateful
// perhaps terminal setup?
void terminalsurowy(void) {
	struct termios rtt = tt;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	tcsetattr(0, TCSAFLUSH, &rtt);
}

void restore_terminal(void) {
	tcsetattr (0, TCSAFLUSH, &tt);
}

void cat(int in, int out) {
	char buf[BUFSIZ];
	int cc;
	while ((cc = read(in, buf, sizeof (buf))) > 0) {
		write (out, buf, cc);
	}
}

void konwert(char *filter) {
	execlp("konwert", "konwert", filter, 0);
	perror("konwert");
	exit(1);
}

void getslave(void) {
	line[strlen("/dev/")] = 't';
	slave = open(line, O_RDWR);
	if (slave < 0) {
		perror(line);
		exit(1);
	}
	tcsetattr(slave, TCSAFLUSH, &tt);
	ioctl(slave, TIOCSWINSZ, &win);
	setsid();
	ioctl(slave, TIOCSCTTY, 0);
}

void komenda(int argc, char **argv) {
	getslave();
	dup2(slave, 0);
	dup2(slave, 1);
	dup2(slave, 2);
	close(slave);

	if (!argc) {
		const char *slash = strrchr(shell, '/');
		if (!slash) {
			slash = shell - 1;
		}

		execl(shell, slash + 1, 0);
	} else if (argc == 1 && !strcmp(argv[0], "-")) {
		const char *slash = strrchr(shell, '/');
		if (!slash) slash = shell - 1;
		char *argv0 = calloc(1, 1 + strlen(slash + 1) + 1);
		argv0[0] = '-';
		strcpy(argv[0] + 1, slash + 1);
		execl(shell, argv0, 0);
	} else {
		execvp(argv[0], argv);
	}

	perror(argc ? argv[0] : shell);
	exit(1);
}

int glowny, input[2], output[2], pidout;

void end(int _) {
	(void) _; // ignore warning
	if (input[1]) {
		close(input[1]);
	}

	if (glowny) {
		restore_terminal();
	}

	if (pidout) {
		kill(pidout, SIGTERM);
	}

	if (glowny) {
		while (wait(NULL) != -1) {}
	}

	exit(0);
}

int main(int argc, char **argv) {
	prog_name = argv[0];
	shell = getenv ("SHELL");
	if (!shell) shell = "/bin/sh";

	if (argc == 2) {
		if (!strcmp (argv[1], "--help")) usage(0);
		if (!strcmp (argv[1], "--version")) version();
	}
	if (argc < 3) {
		usage(0);
	}

	getmaster();
	signal(SIGCHLD, end);

	if (strcmp (argv[2], "-")) {
		if (pipe(output) == -1) {
			perror ("pipe");
			exit (1);
		} switch (fork()) {
			case -1:
				perror("fork");
				exit(1);
			case 0:
				close(master);
				close(output[1]);
				dup2(output[0], 0);
				close(output[0]);
				putenv("FILTERM=out");
				konwert(argv[2]);
		}
		close (output[0]);
	} else {
		output[1] = 1;
	}

	switch (pidout = fork()) {
		case -1:
			perror ("fork");
			exit (1);
		case 0:
			close (0);
			if (strcmp (argv[2], "-")) close (1);
			close (2);
			cat (master, output[1]);
			exit (0);
	}

	if (strcmp (argv[2], "-")) {
		close(output[1]);
	}

	switch (fork()) {
		case -1:
			perror("fork");
			exit(1);
		case 0:
			pidout = 0;
			close(master);
			komenda(argc - 3, argv + 3);
	}

	if (strcmp(argv[1], "-")) {
		if (pipe(input) == -1) {
			perror ("pipe");
			exit (1);
		}

		switch (fork()) {
			case -1:
				perror("fork");
				exit(1);
			case 0:
				pidout = 0;
				close(input[1]);
				dup2(input[0], 0);
				close(input[0]);
				dup2(master, 1);
				close(master);
				putenv("FILTERM=in");
				konwert(argv[1]);
		}
		close(master);
		close(input[0]);
	} else {
		input[1] = master;
	}

	glowny = 1;
	terminalsurowy();
	cat(0, input[1]);
	end(0);
}
