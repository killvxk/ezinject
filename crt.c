#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include "ezinject_injcode.h"

extern void lib_preinit(struct injcode_user *user);
extern int lib_main(int argc, char *argv[]);

__attribute__((constructor)) void entry(void)
{
	pid_t pid = getpid();
	int shm_id = shmget(pid, MAPPINGSIZE, 0);
	if(shm_id < 0){
		perror("shmget");
		return;
	}
	void *mem = shmat(shm_id, NULL, SHM_RDONLY);
	if(mem == MAP_FAILED){
		perror("shmat");
		return;
	}

	int sem_id = semget(pid, 1, 0);
	if(sem_id < 0){
		perror("semget");
		return;
	}

	// copy the struct locally
	struct injcode_bearing br = *(struct injcode_bearing *)mem;
	
	// remove original shmat mapping, created by ezinject
	shmdt(br.mapped_mem);

	shmdt(mem);

	// signal ezinject to close IPC
	struct sembuf sem_op = {
		.sem_num = 0,
		.sem_op = -1,
		.sem_flg = 0
	};
	if(semop(sem_id, &sem_op, 1) < 0){
		perror("semop");
		return;
	}

	lib_preinit(&br.user);
	lib_main(br.argc, br.argv);
	
}