



int
main(){

	for(i = 0; i < atoi(argv[1]); i++) {
		pid = fork();
		if(pid < 0) {
			printf("Error");
			exit(1);
		} else if (pid == 0) {
			printf("Child (%d): %d\n", i + 1, getpid());
			exit(0); 
		} else  {
			wait(NULL);
		}
	}







}
