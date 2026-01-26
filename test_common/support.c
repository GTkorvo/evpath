#include "config.h"
#include "support.h"
#include "simple_rec.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* winsock2.h and ws2tcpip.h are included via support.h for Windows */

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

/* Global variables */
int quiet = 1;
int regression = 1;
char *transport = NULL;
char *control = NULL;
char *argv0 = NULL;
int no_fork = 0;

static char *ssh_args[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
static char remote_directory[1024] = "";

void
set_ssh_args(char *destination_host, char *ssh_port)
{
    ssh_args[0] = strdup(SSH_PATH);
    ssh_args[1] = destination_host;
    ssh_args[2] = NULL;
    if (ssh_port != NULL) {
	ssh_args[2] = "-p";
	ssh_args[3] = ssh_port;
	ssh_args[4] = NULL;
    }
}

void
set_remote_directory(const char *dir)
{
    strcpy(remote_directory, dir);
}

void
usage(void)
{
    printf("Usage:  %s <options> \n", argv0);
    printf("  Options:\n");
    printf("\t-q  quiet\n");
    printf("\t-v  verbose\n");
    printf("\t-n  No regression test.  I.E. just run the master and print \n\t\twhat command would have been run for client.\n");
    printf("\t-ssh <hostname>:<ssh_port>:<remote directory>  parameters to use for remote client via ssh.\n");
    printf("\t-ssh <hostname>:<remote directory>  parameters to use for remote client via ssh.\n");

    exit(1);
}

#ifdef _MSC_VER
int inet_aton(const char* cp, struct in_addr* addr)
{
    addr->s_addr = inet_addr(cp);
    return (addr->s_addr == INADDR_NONE) ? 0 : 1;
}
#endif

pid_t
run_subprocess(char **args)
{
    char **run_args = args;
#ifdef HAVE_WINDOWS_H
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    char comm_line[8191];

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );
    char module[MAX_PATH];
    GetModuleFileName(NULL, &module[0], MAX_PATH);
    int i = 1;
    strcpy(comm_line, module);
    strcat(comm_line, " ");
    while (args[i] != NULL) {
      strcat(comm_line, args[i]);
      strcat(comm_line, " ");
      i++;

    }
    if (!CreateProcess(module,
		       comm_line,
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory
        &si,            // Pointer to STARTUPINFO structure
		       &pi )
    )
    {
        printf( "CreateProcess failed (%lu).\n", GetLastError() );
	printf("Args were argv[0] = %s\n", args[0]);
	printf("Args were argv[1] = %s, argv[2] = %s\n", args[1], args[2]);
        return 0;
    }
    return (intptr_t) pi.hProcess;
#else
    pid_t child;
    if (quiet <=0) {printf("Forking subprocess\n");}
    if (ssh_args[0] != NULL) {
        int i=0, j=0;
        int count = 0;
	while(args[count] != NULL) count++;
	count+= 6; /* enough */
	run_args = malloc(count * sizeof(run_args[0]));
	while(ssh_args[i]) {
	    run_args[i] = ssh_args[i];
	    i++;
	}
	if (remote_directory[0] != 0) {
	  if (strrchr(argv0, '/')) argv0 = strrchr(argv0, '/') + 1;
	  run_args[i] = malloc(strlen(remote_directory) +
			       strlen(argv0) + 4);
	  strcpy(run_args[i], remote_directory);
	  if (remote_directory[strlen(remote_directory)-1] != '/')
	    strcat(run_args[i], "/");
	  strcat(run_args[i], argv0);
	} else {
	  run_args[i] = argv0;
	}
	i++;
	while(args[j+1]) {
	    run_args[i] = args[j+1];
	    i++; j++;
	}
	run_args[i] = NULL;
    } else {
        run_args[0] = argv0;
    }
    if (quiet <= 0) {
        int i=0;
	printf("Subproc arguments are: ");
	while(run_args[i]) {
	    printf("%s ", run_args[i++]);
	}
	printf("\n");
    }
    if (no_fork) {
	child = -1;
    } else {
        child = fork();
	if (child == 0) {
	    /* I'm the child */
	    execv(run_args[0], run_args);
	}
    }
    return child;
#endif
}

/*
 * Cross-platform wait for subprocess.
 * Returns: pid on success, 0 if non-blocking and child not exited, -1 on error.
 * exit_state is encoded like waitpid - use WIFEXITED/WEXITSTATUS/etc to analyze.
 */
pid_t
wait_for_subprocess(pid_t proc, int *exit_state, int block)
{
#ifdef HAVE_WINDOWS_H
    DWORD timeout = block ? INFINITE : 0;
    DWORD wait_result = WaitForSingleObject((HANDLE)proc, timeout);

    if (wait_result == WAIT_OBJECT_0) {
	DWORD child_exit_code;
	GetExitCodeProcess((HANDLE)proc, &child_exit_code);
	CloseHandle((HANDLE)proc);
	/* Encode exit status like Linux: exit_code in bits 8-15, 0 in bits 0-7 */
	*exit_state = (int)(child_exit_code << 8);
	return proc;
    } else if (wait_result == WAIT_TIMEOUT) {
	/* Non-blocking and child still running */
	return 0;
    } else {
	/* WAIT_FAILED or other error */
	return -1;
    }
#else
    return waitpid(proc, exit_state, block ? 0 : WNOHANG);
#endif
}

int
verify_simple_record(simple_rec_ptr event)
{
    long sum = 0;
    sum += event->integer_field % 100;
    sum += event->short_field % 100;
    sum += event->long_field % 100;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    return (sum == event->scan_sum);
}
