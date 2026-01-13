#include <stdio.h>
#include <link.h>
#include <string.h>
#include <memory.h>
#include <linux/elf.h>
#include <elf.h>
#include <sys/mman.h>
#include <errno.h>
#include <dlfcn.h>


#define PAGE_BASE(x) ((void*)((u_int64_t)(x) & (~0xFFFl)))


void generate_trampoline_simple(uint64_t target_addr, uint32_t *code_buffer) {
    // printf("target_addr:%p code_buffer:%p\n",target_addr,code_buffer);
    // movz x16, #(addr>>48), lsl #48
    code_buffer[0] = 0xD2800000 | (3 << 21) | (((target_addr >> 48) & 0xFFFF) << 5) | 16;
    
    // movk x16, #(addr>>32), lsl #32
    code_buffer[1] = 0xF2800000 | (2 << 21) | (((target_addr >> 32) & 0xFFFF) << 5) | 16;
    
    // movk x16, #(addr>>16), lsl #16
    code_buffer[2] = 0xF2800000 | (1 << 21) | (((target_addr >> 16) & 0xFFFF) << 5) | 16;
    
    // movk x16, #addr, lsl #0
    code_buffer[3] = 0xF2800000 | (0 << 21) | ((target_addr & 0xFFFF) << 5) | 16;
    // printf("%p %p %p %p\n",code_buffer[0],code_buffer[1],code_buffer[2],code_buffer[3]);
    // br x16
    code_buffer[4] = 0xD61F0200;
}

/*
pfnDestAddr:要hook的函数地址
pfnNewAddr:hook函数
pfnOldAddr：旧函数地址
*/
void tramplo();
void* InlineHook(void* pfnDestAddr,  void* pfnNewAddr, void* pfnOldAddr){
    // printf("%p %p %p\n",pfnDestAddr,pfnNewAddr,pfnOldAddr);
    //1.申请内存，保存跳板代码
    size_t nLoadSize = 0x100;
    uint8_t *ptramplo = (uint8_t*)mmap64(NULL, nLoadSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS , -1, 0);
    mprotect(ptramplo,1 ,PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void*)ptramplo,(void*)tramplo,0xd0);

    uint8_t* ptramplo2 = ptramplo + 0x14;
    uint8_t* phookaddr = ptramplo + 0xc8;
    uint8_t* poldcode = ptramplo + 0xa4;
    uint8_t* poldaddr = ptramplo + 0xc0;
    //uint8_t* pretAddr = ptramplo + 0xc0;//返回的地址


    //2.保存原指令到oldcode
    memcpy((void*)poldcode,(void*)pfnDestAddr,0x14);

    //3.覆盖原指令为tramplo,并写入tramplo2的地址
    mprotect(PAGE_BASE(pfnDestAddr),1 ,PROT_READ | PROT_WRITE | PROT_EXEC);
    // memcpy((void*)pfnDestAddr,(void*)ptramplo,0x14);
    // memcpy(pfnDestAddr +8 ,(void*)&ptramplo2,8);
    uint32_t code_buffer[4] = {0};
    generate_trampoline_simple((uint64_t)(ptramplo + 0x14),code_buffer);
    memcpy((void*)pfnDestAddr,&code_buffer[0],4);
    memcpy((void*)pfnDestAddr+4,&code_buffer[1],4);
    memcpy((void*)pfnDestAddr+8,&code_buffer[2],4);
    memcpy((void*)pfnDestAddr+12,&code_buffer[3],4);
    memcpy((void*)pfnDestAddr+16,&code_buffer[4],4);

    //4.修改oldaddr为原指令地址
    void* pOldAddr = (uint8_t*)pfnDestAddr + 0x14;
    memcpy((void*)poldaddr,(void*)&pOldAddr,8);

    //5.填写hook的地址
    memcpy((void*)phookaddr,(void*)&pfnNewAddr,8);

    *(uint64_t*)pfnOldAddr = (uint64_t)poldcode;
    return poldcode;
}

//---------------------------------------------------------------
typedef int (*oldPfn)(const char* _Nonnull __fmt, ...);
oldPfn old_printf = NULL;

int myPrintf(const char * __fmt, int n){
    old_printf("inlinehook come om: __fmt:%s n:%d \n",__fmt,n);
    return 0;
}

//------------------------------------------------------------------
typedef int (*old_Pfnopen)(const char *pathname,const char * mode);
old_Pfnopen old_open= NULL;
int new_open(const char *pathname,const char *  mode) {
    printf("open hook  file path:%s %s \n", pathname,mode);

    return 0;
}

//------------------------------------
typedef int (*old_pfnPuts)(const char* _Nonnull __s);
old_pfnPuts old_puts= NULL;
int new_puts(const char* _Nonnull __s){
    printf("puts hook  :%s \n", __s);
    return 0;
}

__attribute__((constructor))  
void installInlineHook(){

    printf("installInlineHook\n");
    InlineHook(&printf,myPrintf,&old_printf);
    InlineHook(&fopen,new_open,&old_open);
    InlineHook(&puts,new_puts,&old_puts);

}