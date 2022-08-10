#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.定义了 entry.S 中的 stack0 ，它要求 16bit 对齐。
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];
//下面一行定义了共享变量，即每个 CPU 的暂存区用于 machine-mode 定时器中断，它是和 timer 驱动之间传递数据用的。
// scratch area for timer interrupt, one per CPU.
uint64 mscratch0[NCPU * 32];
//声明了 timer 中断处理函数，在接下来的 timer 初始化函数中被用到。
// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.下面几行代码使 CPU 进入 supervisor mode 。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany设置了汇编指令 mret 后 PC 指针跳转的函数，也就是 main 函数。
  w_mepc((uint64)main);

  // disable paging for now.通过将0写入页表寄存器satp,暂时关闭了分页功能，即直接使用物理地址。
  w_satp(0);
  //接下来的几行代码，将所有中断异常处理设定在给 supervisor mode 下。
  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
//请求时钟中断，也就是 clock 的初始化，其具体实现在后面
  // ask for clock interrupts.
  timerinit();
//将 CPU 的 ID 值保存在寄存器 tp 中
  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);
  //最后切换到 supervisor mode 调用返回指令 mret ，并跳转到 main() 函数处执行。
  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  //clock 时钟驱动的初始化函数，首先读出 CPU 的 ID 
  // each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.设置中断时间间隔，这里设置的是 0.1 秒。
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;
  //利用刚才在文件开头声明的 timer_scratch 变量，把刚才的 CPU 的 ID 和设置的中断间隔设置到 scratch 寄存器中，以供 clock 驱动使用。
  // prepare information in scratch[] for timervec.
  // scratch[0..3] : space for timervec to save registers.
  // scratch[4] : address of CLINT MTIMECMP register.
  // scratch[5] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &mscratch0[32 * id];
  scratch[4] = CLINT_MTIMECMP(id);
  scratch[5] = interval;
  w_mscratch((uint64)scratch);
  //最后几行是设置中断处理函数，打开中断。
  // set the machine-mode trap handler.
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}
