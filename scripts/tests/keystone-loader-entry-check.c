#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
static unsigned char signal_stack[65536];
static uintptr_t base=0x10f400000UL;
static size_t length;
static void caught(int sig, siginfo_t *info, void *context) {
 ucontext_t *u=context;
 uintptr_t sp=u->uc_mcontext.__gregs[REG_SP];
 uintptr_t pc=u->uc_mcontext.__gregs[REG_PC];
 char message[256];
 int n=snprintf(message,sizeof(message),"signal=%d pc=0x%lx sp=0x%lx fault=%p; stack_in_EPM=%d\n",sig,(unsigned long)pc,(unsigned long)sp,info->si_addr,sp>=base && sp<base+length);
 write(1,message,(size_t)n);
 _exit(sig==SIGTRAP && sp>=base && sp<base+length && !(sp & 15) ? 0 : 1);
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 int fd=open(argv[1],O_RDONLY);struct stat st;
 if(fd<0 || fstat(fd,&st))return 2;
 length=((size_t)st.st_size+4095)&~4095UL;
 if(length<0x20000)length=0x20000; /* EPM includes zero-filled page padding. */
 unsigned char *memory=mmap((void*)base,length,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 if(memory==MAP_FAILED){perror("mmap");return 2;}
 if(read(fd,memory,(size_t)st.st_size)!=st.st_size)return 2;
 close(fd);
 /* Stop at the first call to load_runtime. Execute only the actual entry's
  * stack setup and argument stores, never privileged loader instructions. */
 int patched=0;
 for(size_t offset=0;offset<128;) {
  uint16_t half=*(uint16_t*)(memory+offset);
  if((half&3)!=3){offset+=2;continue;}
  uint32_t insn=*(uint32_t*)(memory+offset);
  if(((insn&0x7f)==0x6f || (insn&0x7f)==0x67) && ((insn>>7)&31)==1){
   *(uint32_t*)(memory+offset)=0x00100073;patched=1;break;
  }
  offset+=4;
 }
 if(!patched)return 2;
 __builtin___clear_cache((char*)memory,(char*)memory+length);
 stack_t alt={.ss_sp=signal_stack,.ss_size=sizeof(signal_stack)};
 if(sigaltstack(&alt,0))return 2;
 struct sigaction sa={.sa_sigaction=caught,.sa_flags=SA_SIGINFO|SA_ONSTACK};
 sigemptyset(&sa.sa_mask);sigaction(SIGSEGV,&sa,0);sigaction(SIGTRAP,&sa,0);sigaction(SIGILL,&sa,0);
 ((void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))memory)(0,base,length,0,0,0,0,0);
 return 2;
}
