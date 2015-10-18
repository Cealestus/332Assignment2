#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	int pid;
	int turnaround, running;
	int j;

	for(j = 1; j < 3; j++){
		pid = fork();
		if(pid < 0){
			printf(1, "Error\n");
			exit();
		}else if(pid == 0){
			//Do something
			printf(1, "pid: %d\n", getpid());
			exit();
		}else{
			waitstat(&turnaround, &running);
			printf(1, "parent: Turnaround: %d Running: %d\n", turnaround, running);
		}
	}
	exit();
}	
