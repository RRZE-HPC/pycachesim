#include "pin.H"
#include "pinMarker.h"
#include <iostream>
#include <fstream>
#include <stdlib.h>

extern "C"
{
#include "backend.h"
}

KNOB<bool> KnobFollowCalls(KNOB_MODE_WRITEONCE, "pintool",
    "follow_calls", "0", "specify if the instrumentation has to follow function calls between the markers");

ADDRINT startCall;
ADDRINT startIns;
ADDRINT stopCall;
ADDRINT stopIns;


LOCALFUN VOID activate()
{
    _pinMarker_active = true;

}
LOCALFUN VOID deactivate()
{
    _pinMarker_active = false;
}

VOID ImageLoad(IMG img, VOID *v)
{
    if (IMG_IsMainExecutable(img))
    {
        for( SYM sym= IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym) )
        {
            if (PIN_UndecorateSymbolName ( SYM_Name(sym), UNDECORATION_NAME_ONLY) == "_magic_pin_start")
            {
                startCall = SYM_Address(sym);
            }
            if (PIN_UndecorateSymbolName ( SYM_Name(sym), UNDECORATION_NAME_ONLY) == "_magic_pin_stop")
            {
                stopCall = SYM_Address(sym);
            }
        }

        for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) )
        {
                for( RTN rtn= SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn) )
                {
                    RTN_Open(rtn);

                    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
                    {
                        if (INS_IsDirectControlFlow(ins))
                        {
                            if (INS_DirectControlFlowTargetAddress(ins) == startCall)
                            {
                                if (KnobFollowCalls)
                                {
                                    const AFUNPTR Activate = (AFUNPTR) activate;
                                    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, Activate, IARG_END);
                                }
                                else
                                {
                                    startIns = INS_Address(ins);
                                }
                            }
                            if (INS_DirectControlFlowTargetAddress(ins) == stopCall)
                            {
                                if (KnobFollowCalls)
                                {
                                    const AFUNPTR Deactivate = (AFUNPTR) deactivate;
                                    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, Deactivate, IARG_END);
                                }
                                else
                                {
                                    stopIns = INS_Address(ins);
                                }
                            }
                        }
                    }
                    RTN_Close(rtn);
                }
        }
    }
}

LOCALFUN VOID MemRead_check(UINT64 addr, UINT32 size)
{
    if (_pinMarker_active)
    std::cout << addr << " " << size << " " << 1 << "\n";
}

LOCALFUN VOID MemRead(UINT64 addr, UINT32 size)
{
    std::cout << addr << " " << size << " " << 1 << "\n";
}

LOCALFUN VOID MemWrite_check(UINT64 addr, UINT32 size)
{
    if (_pinMarker_active)
    std::cout << addr << " " << size << " " << 0 << "\n";
}

LOCALFUN VOID MemWrite(UINT64 addr, UINT32 size)
{
    std::cout << addr << " " << size << " " << 0 << "\n";
}

VOID Instruction(INS ins, VOID *v)
{
    if (KnobFollowCalls)
    {
        const AFUNPTR readFun = (AFUNPTR) MemRead_check;
        const AFUNPTR writeFun = (AFUNPTR) MemWrite_check;
        
        if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, readFun,
                IARG_MEMORYREAD_EA,
                IARG_MEMORYREAD_SIZE,
                IARG_END);
        }

        if(INS_IsMemoryWrite(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, writeFun,
                IARG_MEMORYWRITE_EA,
                IARG_MEMORYWRITE_SIZE,
                IARG_END);
        }
    }
    else if(INS_Address(ins) > startIns && INS_Address(ins) < stopIns)
    {
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

        if(INS_IsMemoryWrite(ins))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, writeFun,
                IARG_MEMORYWRITE_EA,
                IARG_MEMORYWRITE_SIZE,
                IARG_END);
        }
    }
}

int main(int argc, char *argv[])
{

    int i = 0;

    i = testLink();

    if (i==1)
    std::cout << "success!!!" << std::endl;

    PIN_InitSymbols();

    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return 1;
    }

    std::cerr << "starting" << std::endl;

    if (KnobFollowCalls)
    {
        std::cerr << "follow calls" << std::endl;
    }

    // IMG_AddInstrumentFunction(ImageLoad, 0);

    // INS_AddInstrumentFunction(Instruction, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}