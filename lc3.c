#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

#define MEM_SIZE (1<<16)
u_int16_t memory[MEM_SIZE];


enum {
    R_R0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};

enum {
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

enum {
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

enum {
    FL_POS  = 1 << 0,
    FL_ZRO  = 1 << 1,
    FL_NEG  = 1 << 2
};

enum {
    KBSR = 0xFE00,  /* keyboard status */
    KBDR = 0xFE02   /* keyboard data */
};

u_int16_t reg[R_COUNT];


struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}


void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}


uint16_t check_key() {
    //The fd_set data type represents file descriptor sets for the select function. It is actually a bit array.
    fd_set readfds; 

    // This macro initializes the file descriptor set set to be the empty set.
    FD_ZERO(&readfds);
    
    //This macro adds filedes to the file descriptor set set.
    //The filedes parameter must not have side effects since it is evaluated more than once.
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

u_int16_t sign_extended(u_int16_t x, int bit_count) {
    if((x >> (bit_count - 1))&1){
        x |= (0xffff << bit_count);
    } 
    return x;
}  

void update_flag(u_int16_t r) {
    
    if(reg[r] == 0){
        reg[R_COND] = FL_ZRO;
    }else if(reg[r] >> 15) {
        reg[R_COND] = FL_NEG;
    }else {
        reg[R_COND] = FL_POS;
    }
}


u_int16_t swap16(u_int16_t x){
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE *file) {
    u_int16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin  = swap16(origin);
    u_int16_t max_size = MEM_SIZE - origin;
    u_int16_t *p = memory + origin;
    size_t read = fread(p, sizeof(u_int16_t), max_size, file);
    while(read-- > 0){
        (*p) = swap16(*p);
        ++p;
    }

}

int read_image(const char *image_path) {
    FILE *file = fopen(image_path, "rb");
    if(!file){ 
        return 0;
    }
    read_image_file(file);
    fclose(file);
    return 1;
}

uint16_t mem_read(uint16_t address)
{
    if (address == KBSR)
    {
        if (check_key())
        {
            memory[KBSR] = (1 << 15);
            memory[KBDR] = getchar();
        }
        else
        {
            memory[KBSR] = 0;
        }
    }
    return memory[address];
}

void mem_write(u_int16_t addr, u_int16_t val) {
    memory[addr] = val;
}

int main(int argc, char **argv) {

    if(argc < 2){
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    
    for(int j = 1; j < argc; j++){
        if(!read_image(argv[j])){
            printf("failed load image: %s\n", argv[j]);
            exit(1);
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering(); 

    reg[R_COND] = FL_ZRO;
    enum{PC_START = 0x3000};
    reg[R_PC] = PC_START;
    int running = 1;
    while(running){
        u_int16_t instr = mem_read(reg[R_PC]++);
        u_int16_t op = instr >> 12;
        switch(op) {
            case OP_ADD:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t r1 = (instr >> 6)&0x7;
                u_int16_t imm_flag = (instr >> 5)&0x1;
                if(imm_flag) {
                    u_int16_t imm5 = sign_extended(instr&0x1F,5);
                    reg[r0] = reg[r1] + imm5;
                }else {
                    u_int16_t r2 = instr&0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }
                update_flag(r0);
            }
            break;
            case OP_AND:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t r1 = (instr >> 6)&0x7;
                u_int16_t imm_flag = (instr >> 5)&0x1;
                if(imm_flag) {
                    u_int16_t imm5 = sign_extended(instr&0x1F,5);
                    reg[r0] = reg[r1]&imm5;
                }else {
                    u_int16_t r2 = instr&0x7;
                    reg[r0] = reg[r1]&reg[r2];
                }
                update_flag(r0);

            }
            break;
            case OP_NOT:
            { 
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                reg[r0] = ~reg[r1];
                update_flag(r0); 
            }
            break;
            case OP_BR:
            {
                u_int16_t cond = (instr >> 9)&0x7;
                u_int16_t addr_off = sign_extended(instr&0x1FF,9);
                if(cond & reg[R_COND]){
                    reg[R_PC] += addr_off;
                }
            }
            break;
            case OP_JMP:
            {
                u_int16_t r1 = (instr >> 6)&0x7;
                reg[R_PC] = reg[r1];
            }
            break;
            case OP_JSR:
            {
                u_int16_t f = (instr >> 11)&0x1;
                reg[R_R7] = reg[R_PC];
                if(f){
                    u_int16_t addr_off = sign_extended(instr&0x7FF,11);
                    reg[R_PC] += addr_off;
                }else{
                    u_int16_t r1 = (instr >> 6)&0x7;
                    reg[R_PC] = reg[r1];
                }
            }
            break;
            case OP_LD:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t off = sign_extended(instr&0x1ff,9);
                reg[r0] = mem_read(reg[R_PC] + off);
                update_flag(r0);
            }
            break;
            case OP_LDI:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t pc_offset = sign_extended(instr&0x1FF,9);
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flag(r0);
            }
            break;
            case OP_LDR:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t r1 = (instr >> 6)&0x7;
                u_int16_t off = sign_extended(instr&0x3F, 6);
                reg[r0] = mem_read((reg[r1] + off));
                update_flag(r0);
            }
            break;
            case OP_LEA:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t off = sign_extended(instr&0x1ff,9);
                reg[r0] = reg[R_PC] + off;
                update_flag(r0);
            }
            break;
            case OP_ST:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t off = sign_extended(instr&0x1ff,9);
                mem_write(reg[R_PC] + off, reg[r0]);
            }
            break;
            case OP_STI:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t off = sign_extended(instr&0x1ff,9);
                mem_write(mem_read(reg[R_PC] + off), reg[r0]);
            }
            break;
            case OP_STR:
            {
                u_int16_t r0 = (instr >> 9)&0x7;
                u_int16_t r1 = (instr >> 6)&0x7;
                u_int16_t off = sign_extended(instr&0x3f,6);
                mem_write(reg[r1]+off, reg[r0]);
            }
            break;
            case OP_TRAP:
            {
                reg[R_R7] = reg[R_PC];
                switch(instr&0xff){
                    case TRAP_GETC:
                    {
                        reg[R_R0] = (uint16_t)getchar();
                        //fflush(stdin); 
                        update_flag(R_R0);
                    }
                    break;
                    case TRAP_OUT:
                    {
                        putc((char)reg[R_R0],stdout);
                        fflush(stdout);
                    }
                    break;  
                    case TRAP_PUTS:
                    {
                        u_int16_t *c = memory + reg[R_R0];
                        while(*c){
                            putc((char)*c, stdout);
                            ++c;
                        }
                        fflush(stdout);
                    }
                    break;
                    case TRAP_IN:
                    {
                        printf("Enter a character: ");
                        char c = getchar();
                        putc(c,stdout);
                        fflush(stdout); 
                        reg[R_R0] = (u_int16_t)c;
                        update_flag(R_R0);
                    }
                    break;
                    case TRAP_PUTSP:
                    {
                        u_int16_t *c = memory + reg[R_R0];
                        while(*c) {
                            char c1 = *c&0xff;
                            putc(c1,stdout);
                            char c2 = (*c) >> 8;
                            if(c2){putc(c2, stdout);}
                            ++c;
                        }
                        fflush(stdout);
                    }
                    break;
                    case TRAP_HALT:
                    {
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                    }
                    break;
                }

            }
            break;
            case OP_RES:
            case OP_RTI:
            break;
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}