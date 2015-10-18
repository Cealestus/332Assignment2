#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	int pid;
	int turnaround, running;
	int j;
	int sum,k,i,C;
	for(j = 1; j < 21; j++){
		pid = fork();
		if(pid < 0){
			printf(1, "Error\n");
			exit();
		}else if(pid == 0){
			//Do something
			//printf(1, "pid: %d\n", getpid());
			for (k = 1; k < 11; k ++){
				C = (1/k);
				for(i = 1; i <= (21 - j)*200; i++){
					sum += (1/((i/1) + C));
				}
				printf(1, "Result of %d'th sum of the %d'th child is = %d\n", k, j + 1, sum);
				//printf(1,"");
			}
			
			exit();
		}else{
			waitstat(&turnaround, &running);
			printf(1, "parent: Turnaround: %d Running: %d\n", turnaround, running);
		}
	}
	exit();
}	
