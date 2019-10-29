#include "pin.H"
#include <iostream>
#include <stdio.h>
// #include <stddef.h>
using std::cerr;
using std::cout;
using std::string;
using std::endl;

LOCALFUN VOID MemRead(UINT64 addr, UINT32 size)
{
    cout << addr << " " << size << " " << 1 << endl;
}

LOCALFUN VOID MemWrite(UINT64 addr, UINT32 size)
{
    cout << addr << " " << size << " " << 0 << endl;
}

VOID Instruction(INS ins, VOID *v){

    const AFUNPTR readFun = (AFUNPTR) MemRead;
    const AFUNPTR writeFun = (AFUNPTR) MemWrite;
           
    if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
    {

        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, readFun,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_END);
    }
    
    if(INS_HasMemoryRead2(ins)){

        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, readFun,
            IARG_MEMORYREAD2_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_END);

    }

    if(INS_IsMemoryWrite(ins)){

        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, writeFun,
            IARG_MEMORYWRITE_EA,
            IARG_MEMORYWRITE_SIZE,
            IARG_END);

    }
}

VOID Fini(INT32 code, VOID *v)
{
    std::cout << "Finished" << endl;
}

int main(int argc, char *argv[])
{

    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return 1;
    }

    // add an instrumentation function
    INS_AddInstrumentFunction(Instruction, 0);
    
    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}


