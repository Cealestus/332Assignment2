#include "types.h"
#include "user.h"



int
main(int argc, char *argv[]){
	int i;
	int pid;
	int *created;
	int *ended;
	for(i = 0; i < atoi(argv[1]); i++) {
		pid = fork();
		if(pid < 0) {
			printf(1, "Error");
			exit();
		} else if (pid == 0) {
			printf(1, "Child (%d): %d\n", i + 1, getpid());
			exit(); 
		} else  {
			waitstat(created, ended);
		}
	}
	return 0;






}
