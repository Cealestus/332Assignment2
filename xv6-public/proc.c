#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include <stdio.h>


struct {
  struct spinlock lock;
  struct proc proc[NPROC];

  // create three queues of high medium and low priority
  struct queue high;
  struct queue med;
  struct queue low;

} ptable;

static struct proc *initproc;
// amount of times the processes have run
int runTimes = 0;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->priority=0;
  p->procmtimes =0;
  p->pid = nextpid++;
  release(&ptable.lock);


  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
   
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
	
  // initialize created, ended, and running	
  acquire(&tickslock);
  p->created = ticks;
  p->ended = 0;
  p->running =0;
  release(&tickslock);


  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S


  // initialize the three queues
  ptable.high.head = NULL;
  ptable.high.tail = NULL;
  ptable.med.head = NULL;
  ptable.med.tail = NULL;
  ptable.low.head = NULL;
  ptable.low.tail = NULL;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  // set priority to 0(high priority) for the new process
  p->priority = 0;
  p->procmtimes =0;
  // put the process in the highest priority queue
  queuePush(&ptable.high, p);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));
 
  pid = np->pid;

  // lock to force the compiler to emit the np->state write last.
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  np->priority=0;
  
  queuePush(&ptable.high, np);
  release(&ptable.lock);
  
	
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }


  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;


  // set ended to the ticks
  acquire(&tickslock);
  proc->ended = ticks;
  release(&tickslock);

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

int
waitstat(int* turnaround, int* runtime)
{
	  struct proc *p;
	  int havekids, pid;

	  acquire(&ptable.lock);
	  for(;;){
		// Scan through table looking for zombie children.
		havekids = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->parent != proc)
				continue;
			havekids = 1;
			if(p->state == ZOMBIE){
				pid = p->pid;            
				kfree(p->kstack);
				p->kstack = 0 ;      
				freevm(p->pgdir);			
				p->state = UNUSED;		   
				p->pid = 0;			    
				p->parent = 0;		    
				p->name[0] = 0;		    
				p->killed = 0;		    
				*turnaround = p->ended - p->created;		    
				*runtime = p->running;
				release(&ptable.lock);			    
				return pid;
			    
			}
			    
		}
		// No point waiting if we don't have any children.
		if(!havekids || proc->killed){
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		sleep(proc, &ptable.lock);  //DOC: wait-sleep
	  }
}



// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
   
            
      runTimes= runTimes+1;
      if (runTimes == moveup){	
      	moveToHighQ(&ptable.high, &ptable.med, &ptable.low);
	runTimes=0;
      }

      if(p->state != RUNNABLE)
        continue;
	
	
		
	if(queueIsEmpty(&ptable.high) == 0){
		//cprintf("pid: %d high queue\n", p->pid);
		p = ptable.high.head;
		p->running++;
		proc =p;
		switchuvm(p);
		p->state= RUNNING;
		swtch(&cpu->scheduler, proc->context);
		switchkvm();
		p->priority=1;
		dequeue(&ptable.high);
		queuePush(&ptable.med, p);
	}
	else if(queueIsEmpty(&ptable.high) == 1 && queueIsEmpty(&ptable.med) == 0){
		//cprintf("pid: %d med queue\n", p->pid);
		p = ptable.med.head;
		p->running++;
		proc =p;
		switchuvm(p);
		p->state= RUNNING;
		swtch(&cpu->scheduler, proc->context);
		switchkvm();
		p->procmtimes++;

		if(p->procmtimes == mtimes){
			p->priority=2;
			dequeue(&ptable.med);
			queuePush(&ptable.low, p);
		}
	}
	//else if(queueIsEmpty(&ptable.high) == 1 && queueIsEmpty(&ptable.med) == 1 && queueIsEmpty(&ptable.low) == 0) {
	else{
		//cprintf("pid: %d low queue\n", p->pid);
		p = ptable.low.head;
		p->running++;
		proc =p;
		switchuvm(p);
		p->state= RUNNING;
		swtch(&cpu->scheduler, proc->context);
		switchkvm();
		p->procmtimes++;
		
	}
      

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
/*      p->running++;
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();
*/
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;

    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  proc->priority=0;
  //cprintf("yield\n");
  queuePush(&ptable.high, proc);
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot 
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
 // removeFromQueue(proc, &ptable.high, &ptable.med, &ptable.low);
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      p->priority = 0;
      queuePush(&ptable.high, p);
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // remove the process from the queue
      // removeFromQueue(p, &ptable.high, &ptable.med, &ptable.low);
	
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
	p->priority = 0;
	queuePush(&ptable.high, p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);

  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
	      cprintf(" %p", pc[i]);
    }
      cprintf("\n");
  }
}




// queuePush to put the process in a queue at the tail
void
queuePush(struct queue *q,struct proc *p)
{
	if(q->tail == NULL)
	{
		q->head = p;
		q->tail = p;
	}
	else if (q->tail == q->head)
	{		
		q->tail->next = p;
		p->previous= q->tail;
		q->tail = p;
		q->head->next = q->tail;
	}
	else {
		q->tail->next = p;
		p->previous= q->tail;
		q->tail = p;
		q->tail->next = NULL;
	}
}


// checking is the queue is empty
// return 1 if it is
// return 0 if it is not empty
int
queueIsEmpty(struct queue *q){
	if(q->tail == NULL){
		return 1;
	}
	else {
		return 0;
	}
}


// remove the head of the queue from the queue itself
// and make all the responding changes to the pointers
void
dequeue(struct queue *q){

	if(queueIsEmpty(q) ){
		return;
	}
	else if( q->head == q->tail){
		q->head = NULL;
		q->tail = NULL;
	}
	else if(q->head->next == q->tail){
		q->head->next= NULL;
		q->head = q->tail;
		q->tail->previous = NULL;
	}
	else 
	{
		q->head = q->head->next;
		q->head->previous->next = NULL;
		q->head->previous = NULL;
	}
}


// loop through the med queue and move all the processes to the high queue
// and loop through the low queue and move all the processes to the high queue
void
moveToHighQ(struct queue *q1, struct queue *q2, struct queue *q3){

	struct proc *p;
	while(queueIsEmpty(q2)==0){
		p = q2->head;
		p->priority= 0;
		p->procmtimes = 0;
		queuePush(q1, p);
		dequeue(q2);
	}
	while(queueIsEmpty(q3)==0){
		p = q3->head;
		p->priority= 0;
		p->procmtimes = 0;
		queuePush(q1, p);
		dequeue(q3);
	}
}



// search through the queue and remove the process from it
void
removeFromQueue(struct proc *p, struct queue *q1, struct queue *q2, struct queue *q3){

	struct proc *delP;
	if(p->priority == 0 && queueIsEmpty(q1)== 0){
		if(p->pid == q1->head->pid) {
			dequeue(q1);
		}
		else if ( q1->head->next == q1->tail) {
			if(q1->tail->pid == p->pid)
			{
				q1->head->next= NULL;
				q1->tail = q1->head;
				q1->tail->previous = NULL;
			}	
		}
		else {
			delP = q1->head->next;
			while(p->pid != delP->pid && delP->next != NULL){
				delP = delP->next;
			}
			delP->next->previous = delP->previous;
			delP->previous->next = delP->next;
			delP->priority = 0;
			delP->procmtimes = 0;
			
		}
	}
	else if(p->priority == 1 && queueIsEmpty(q2)== 0){
		if(p->pid == q2->head->pid )  {
			dequeue(q2);
		}
		else if ( q2->head->next == q2->tail){
			if(q2->tail->pid ==p->pid) {
				q2->head->next = NULL;
				q2->tail = q2->head;
				q2->tail->previous = NULL;
			}
		}
		else {
			delP = q2->head->next;
			while( p->pid != delP->pid && delP->next != NULL){
				delP= delP->next;
			}
			delP->next->previous = delP->previous;
			delP->previous->next = delP->next;
			delP->priority = 0;
			delP->procmtimes = 0;
		}
	}
	else if(p->priority == 2 && queueIsEmpty(q3)== 0){
		if(p->pid == q3->head->pid ){
			dequeue(q3);

		}
		else if (q3->head->next == q3->tail) {
			if(q3->tail->pid == p->pid) {
				q3->head->next = NULL;
				q3->tail = q3->head;
				q3->tail->previous = NULL;
			}
		}
		else {
			delP = q3->head->next;
			while(p->pid != delP->pid && delP->next != NULL){
				delP= delP->next;
			}
			delP->next->previous = delP->previous;
			delP->previous->next = delP->next;
			delP->priority = 0;
			delP->procmtimes = 0;
		}
	}
}


