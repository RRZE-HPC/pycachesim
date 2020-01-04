#include "pin.H"
#include "pinMarker.h"
#include <iostream>
#include <fstream>
#include <stdlib.h>

extern "C"
{
#include "backend.h"
}

Cache* firstLevel;

KNOB<bool> KnobFollowCalls(KNOB_MODE_WRITEONCE, "pintool",
    "follow_calls", "0", "specify if the instrumentation has to follow function calls between the markers");

ADDRINT startCall;
ADDRINT startIns;
ADDRINT stopCall;
ADDRINT stopIns;


LOCALFUN VOID activate()
{
    std::cerr << "activate" << std::endl;
    _pinMarker_active = true;
}
LOCALFUN VOID deactivate()
{
    std::cerr << "deactivate" << std::endl;
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
        Cache__load(firstLevel, {addr, size});
}

LOCALFUN VOID MemRead(UINT64 addr, UINT32 size)
{
    Cache__load(firstLevel, {addr, size});
}

LOCALFUN VOID MemWrite_check(UINT64 addr, UINT32 size)
{
    if (_pinMarker_active)
        Cache__store(firstLevel, {addr, size},0);
}

LOCALFUN VOID MemWrite(UINT64 addr, UINT32 size)
{
    Cache__store(firstLevel, {addr, size},0);
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

Cache* getCacheSim()
{
    Cache* cacheL1 = new Cache;
    cacheL1->name = "L1";
    cacheL1->sets = 64;
    cacheL1->ways= 8;
    cacheL1->cl_size = 64;
    cacheL1->replacement_policy_id = 1;
    cacheL1->write_back = 1;
    cacheL1->write_allocate = 1;
    cacheL1->subblock_size = cacheL1->cl_size;

    cacheL1->placement = new cache_entry[cacheL1->sets * cacheL1->ways];
    for(unsigned int i=0; i<cacheL1->sets*cacheL1->ways; i++) {
        cacheL1->placement[i].invalid = 1;
        cacheL1->placement[i].dirty = 0;
    }
    cacheL1->cl_bits = log2_uint(cacheL1->cl_size);
    cacheL1->subblock_bitfield = NULL;

    Cache* cacheL2 = new Cache;
    cacheL2->name = "L2";
    cacheL2->sets = 512;
    cacheL2->ways= 8;
    cacheL2->cl_size = 64;
    cacheL2->replacement_policy_id = 1;
    cacheL2->write_back = 1;
    cacheL2->write_allocate = 1;
    cacheL2->subblock_size = cacheL2->cl_size;

    cacheL2->placement = new cache_entry[cacheL2->sets * cacheL2->ways];
    for(unsigned int i=0; i<cacheL2->sets*cacheL2->ways; i++) {
        cacheL2->placement[i].invalid = 1;
        cacheL2->placement[i].dirty = 0;
    }
    cacheL2->cl_bits = log2_uint(cacheL2->cl_size);
    cacheL2->subblock_bitfield = NULL;
    
    Cache* cacheL3 = new Cache;
    cacheL3->name = "L3";
    cacheL3->sets = 9216;
    cacheL3->ways= 16;
    cacheL3->cl_size = 64;
    cacheL3->replacement_policy_id = 1;
    cacheL3->write_back = 1;
    cacheL3->write_allocate = 1;
    cacheL3->subblock_size = cacheL3->cl_size;

    cacheL3->placement = new cache_entry[cacheL3->sets * cacheL3->ways];
    for(unsigned int i=0; i<cacheL3->sets*cacheL3->ways; i++) {
        cacheL3->placement[i].invalid = 1;
        cacheL3->placement[i].dirty = 0;
    }
    cacheL3->cl_bits = log2_uint(cacheL3->cl_size);
    cacheL3->subblock_bitfield = NULL;

    // Py_INCREF(cacheL3);
    
    // Cache mem = {.name="MEM", .sets=0, .ways=0, .cl_size=0, .replacement_policy_id=1, .write_back=1, .write_allocate=1, .write_combining=1};
    // Py_INCREF(mem);

    cacheL1->load_from = cacheL2;
    cacheL1->store_to = cacheL2;
    
    cacheL2->load_from = cacheL3;
    cacheL2->store_to = cacheL3;

    // cacheL3.load_from = (PyObject*)&mem;
    // cacheL3.store_to = (PyObject*)&mem;

    return cacheL1;
}

VOID printStats(Cache* cache)
{
    std::cout << std::string(cache->name) << "\n";
    std::cout << "LOAD: " << cache->LOAD.count << " size: " << cache->LOAD.byte << "B\n";
    std::cout << "STORE: " << cache->STORE.count << " size: " << cache->STORE.byte << "B\n";
    std::cout << "HIT: " << cache->HIT.count << " size: " << cache->HIT.byte << "B\n";
    std::cout << "MISS: " << cache->MISS.count << " size: " << cache->MISS.byte << "B\n";
    std::cout << "EVICT: " << cache->EVICT.count << " size: " << cache->EVICT.byte << "B\n";
    std::cout << "\n";
    if (cache->load_from != NULL)
        printStats(cache->load_from);
}

void deallocCache(Cache* cache)
{
    if (cache->load_from != NULL)
        deallocCache(cache->load_from);
    delete[] cache->placement;
    delete cache;
}

VOID Fini(int code, VOID * v)
{
    printStats(firstLevel);
    deallocCache(firstLevel);
}

int main(int argc, char *argv[])
{

    firstLevel = getCacheSim(); //TODO check if this works

    PIN_InitSymbols();

    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return 1;
    }


    // if (KnobFollowCalls)
    // {
    //     std::cerr << "follow calls" << std::endl;
    // }

    IMG_AddInstrumentFunction(ImageLoad, 0);

    INS_AddInstrumentFunction(Instruction, 0);

    PIN_AddFiniFunction(Fini, 0);
    
    std::cerr << "starting\n" << std::endl;
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}