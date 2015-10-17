#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	int pid;
	int turnaround;
	int running;
	int i, j, k, N, C, sum, sum2;
	int i2;
	N = 4000;
	sum = 0;
	C = 0;
	sum2 = 0;

	for(j = 1; j < 21; j++){
		pid = fork();
		if(pid <0){
			printf(1, "Error\n");
			exit();
		} else if(pid == 0){
			for (k = 1; k < 11; k ++){
				C = (1/k);
				for(i = 1; i <= (21 - j)*N; i++){
					sum += (1/((i/1) + C));
				}
				printf(1, "Result of %d'th sum of the %d'th child is = %d\n", k, j + 1, sum);
			}
			exit();
		}else{
			//wait();
			waitstat(&turnaround, &running);
			printf(1, "Turnaround: %d Running: %d\n", turnaround, running);
			for(i2 = 1; i < 5*N; i++){
				sum2 += (1/((i2/1) + (1/j)));
			}
			printf(1, "Result of the %d'th sum of the parent is = %d \n", j, sum2);
		}
	}
	exit();
}
