#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	int pid;
	int turnaround, running;
	int j;

	pid = fork();
	for(j = 1; j < 11; j++){
		if(pid < 0){
			printf(1, "Error\n");
			exit();
		}else if(pid == 0){
			//Do something
			exit();
		}else{
			waitstat(&turnaround, &running);
			printf(1, "pid: %d Turnaround: %d Running: %d\n", getpid(), turnaround, running);
		}
	}
	exit();
}	
