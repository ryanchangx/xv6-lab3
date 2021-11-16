#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  int sc;
  struct proc *curproc;
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT:
    curproc = myproc();
    sc = curproc->stackcount;
    cprintf("stackcount=%x\n", sc);
    pde_t *pgdir = curproc->pgdir;
    uint rc = rcr2();
    uint m = PGROUNDUP(rc) - sc * PGSIZE, bottomofstack = (KERNBASE - 2* PGSIZE) - sc*PGSIZE, temp;
    cprintf("\trc->m: %x %x\n", rc, m);
    if((m == bottomofstack && m > curproc->sz) && sc <= 100){
      if((temp = allocuvm(pgdir, bottomofstack - PGSIZE, bottomofstack)) != 0){
        clearpteu(pgdir, (char*)(bottomofstack - PGSIZE));
        curproc->stackcount += 1;
        // curproc->tf->esp = bottomofstack;
        // curproc->pgdir = pgdir;
        cprintf("\tm=%x bstack=%x rc=%x\n", m, bottomofstack, rc);
      }
    }
    else{
      cprintf("\tm=%x bstack=%x rc=%x sz=%x pgsize=%x\n", m, bottomofstack, rc, curproc->sz, PGSIZE);
      goto bad;
      exit();
    }
    break;
  //PAGEBREAK: 13
  default:
    bad:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}


// Lab 3 (part 2) - Recursing 130 levels
// stackcount=1
//         rc->m: 7fffdff0 7fffd000
//         m=7fffd000 bstack=7fffd000 rc=7fffdff0
// stackcount=2
//         rc->m: 7fffdff0 7fffb000
//         m=7fffb000 bstack=7fffb000 rc=7fffdff0
// stackcount=3
//         rc->m: 7fffdff0 7fff9000
//         m=7fff9000 bstack=7fff9000 rc=7fffdff0
// stackcount=4
//         rc->m: 7fffdff0 7fff7000
//         m=7fff7000 bstack=7fff7000 rc=7fffdff0
// stackcount=5
//         rc->m: 7fffdff0 7fff5000
//         m=7fff5000 bstack=7fff5000 rc=7fffdff0
// stackcount=6
//         rc->m: 7fffdff0 7fff3000
//         m=7fff3000 bstack=7fff3000 rc=7fffdff0


// Lab 3 (part 2) - Recursing 200 levels
// stackcount=1
//         rc->m: 7fffdff0 7fffd000
//         m=7fffd000 bstack=7fffd000 rc=7fffdff0
// stackcount=2
//         rc->m: 7fffd000 7fffc000
//         m=7fffc000 bstack=7fffc000 rc=7fffd000
// stackcount=3
//         rc->m: 7fffc000 7fffb000
//         m=7fffb000 bstack=7fffb000 rc=7fffc000
// stackcount=4
//         rc->m: 7fffb000 7fffa000
//         m=7fffa000 bstack=7fffa000 rc=7fffb000
// stackcount=5
//         rc->m: 7fffa000 7fff9000
//         m=7fff9000 bstack=7fff9000 rc=7fffa000
// stackcount=6
//         rc->m: 7fff9000 7fff8000
//         m=7fff8000 bstack=7fff8000 rc=7fff9000
// stackcount=7
//         rc->m: 7fff8000 7fff7000
//         m=7fff7000 bstack=7fff7000 rc=7fff8000
// stackcount=8
//         rc->m: 7fff7000 7fff6000
//         m=7fff6000 bstack=7fff6000 rc=7fff7000
// stackcount=9
//         rc->m: 7fff6000 7fff5000
//         m=7fff5000 bstack=7fff5000 rc=7fff6000
// stackcount=a
//         rc->m: 7fff5000 7fff4000
//         m=7fff4000 bstack=7fff4000 rc=7fff5000
// stackcount=b
//         rc->m: 7fff4000 7fff3000
//         m=7fff3000 bstack=7fff3000 rc=7fff4000 sz=1000 pgsize=1000
// pid 3 lab3: trap 14 err 7 on cpu 0 eip 0xe9 addr 0x7fff4000--kill proc
