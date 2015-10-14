#include "types.h"
#include "user.h"



int
main(int argc, char *argv[]){
	int i;
	int pid;
	int *turnaround = malloc(sizeof(int*));
	int *running = malloc(sizeof(int*));
	for(i = 0; i < atoi(argv[1]); i++) {
		pid = fork();
		if(pid < 0) {
			printf(1, "Error");
			exit();
		} else if (pid == 0) {
			printf(1, "Child (%d): %d\n", i + 1, getpid());
			exit(); 
		} else  {
			waitstat(turnaround, running);
			printf(1, "Turnaround: %d Running: %d", *turnaround, *running);
		}
	}
	return 0;






}
