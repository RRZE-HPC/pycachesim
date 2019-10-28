
#include "Python.h"
#include "pin.H"
#include <iostream>
#include <stdio.h>
#include <stddef.h>
using std::cerr;
using std::string;
using std::endl;

/* Struct for holding memory references.  Rather than having two separate
 * buffers for loads and stores, we just use one struct that includes a
 * flag for type.
 */
struct MEMREF
{
    ADDRINT pc;
    ADDRINT address;
    UINT32 size;
    UINT32 load;
};

PyObject *cs;
PyObject *pName, *pModule, *pInit, *pLoad, *pStore, *pFini;
PyObject *pArgs, *pVal;

BUFFER_ID bufId;
PIN_LOCK fileLock;
TLS_KEY buf_key;

#define NUM_BUF_PAGES 8192

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool demonstrates the basic use of the buffering API." << endl << endl;

    return -1;
}

VOID Trace(TRACE trace, VOID *v){

    UINT32 refSize;
           
    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl)){
        for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins=INS_Next(ins)){
            if(INS_IsMemoryRead(ins)){

                refSize = INS_MemoryReadSize(ins);

                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                    IARG_INST_PTR, offsetof(struct MEMREF, pc),
                    IARG_MEMORYREAD_EA, offsetof(struct MEMREF, address),
                    IARG_UINT32, refSize, offsetof(struct MEMREF, size), 
                    IARG_UINT32, 1, offsetof(struct MEMREF, load),
                    IARG_END);

            }
            if(INS_HasMemoryRead2(ins)){

                refSize = INS_MemoryReadSize(ins);

                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                    IARG_INST_PTR, offsetof(struct MEMREF, pc),
                    IARG_MEMORYREAD2_EA, offsetof(struct MEMREF, address),
                    IARG_UINT32, refSize, offsetof(struct MEMREF, size), 
                    IARG_UINT32, 1, offsetof(struct MEMREF, load),
                    IARG_END);

            }
            if(INS_IsMemoryWrite(ins)){

                refSize = INS_MemoryWriteSize(ins);

                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                    IARG_INST_PTR, offsetof(struct MEMREF, pc),
                    IARG_MEMORYWRITE_EA, offsetof(struct MEMREF, address),
                    IARG_UINT32, refSize, offsetof(struct MEMREF, size), 
                    IARG_UINT32, 0, offsetof(struct MEMREF, load),
                    IARG_END);
            }
        }
    }
}

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
 * @param[in] id		buffer handle
 * @param[in] tid		id of owning thread
 * @param[in] ctxt		application context when the buffer filled
 * @param[in] buf		actual pointer to buffer
 * @param[in] numElements	number of records
 * @param[in] v			callback value
 * @return  A pointer to the buffer to resume filling.
 */
VOID * BufferFull(BUFFER_ID bid, THREADID tid, const CONTEXT *ctxt, VOID *buf,
                  UINT64 numElements, VOID *v)
{
    PIN_GetLock(&fileLock, 1);

    ASSERTX(buf == PIN_GetThreadData(buf_key, tid));
    
    struct MEMREF* reference=(struct MEMREF*)buf;
    UINT64 i;

    for(i=0; i<numElements; i++, reference++){

        pArgs = PyTuple_New(3);
        PyTuple_SetItem(pArgs, 0, cs);

        pVal = PyLong_FromUnsignedLong((unsigned long)reference->address);
        if (!pVal)
        {
            Py_DECREF(pArgs);
            continue;
        }
        PyTuple_SetItem(pArgs, 1, pVal);


        pVal = PyInt_FromLong((long)reference->size);
        if (!pVal)
        {
            Py_DECREF(pArgs);
            continue;
        }
        PyTuple_SetItem(pArgs, 2, pVal);

        if (reference->load)
        {
            PyObject_CallObject(pLoad, pArgs);
        }
        else
        {
            PyObject_CallObject(pStore, pArgs);
        }
        
        Py_DECREF(pArgs);
    }

    PIN_ReleaseLock(&fileLock);

    return buf;
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{

    PIN_GetLock(&fileLock, 1);

    pArgs = PyTuple_New(1);
    PyTuple_SetItem(pArgs, 0, cs);
    PyObject_CallObject(pFini, pArgs);

    Py_DECREF(pArgs);
    Py_DECREF(pVal);
    Py_XDECREF(pLoad);
    Py_XDECREF(pStore);
    Py_XDECREF(pFini);
    Py_DECREF(cs);
    Py_DECREF(pModule);

    Py_Finalize();

    PIN_ReleaseLock(&fileLock);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    pName = PyString_FromString("cachesim_itf.py");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule != NULL)
    {
        pInit = PyObject_GetAttrString(pModule, "init_cachesim");
        if (pInit && PyCallable_Check(pInit))
        {
            pArgs = PyTuple_New(0);
            cs = PyObject_CallObject(pInit, pArgs);
            Py_DECREF(pArgs);
            if (cs == Null)
            {
                cerr << "Could not initialize cache simulator!" << endl;
                return 1;
            }
        }
        else
        {
            if (PyErr_Occurred())
                PyErr_Print();
            cerr << "Cannot find function init_cachesim" << endl;
            return 1;
        }
        Py_XDECREF(pInit);

        pLoad = PyObject_GetAttrString(pModule, "load");
        if (!(pLoad && PyCallable_Check(pLoad)))
        {
            if (PyErr_Occurred())
                PyErr_Print();
            cerr << "Cannot find function load" << endl;
            return 1;
        }

        pStore = PyObject_GetAttrString(pModule, "store");
        if (!(pStore && PyCallable_Check(pStore)))
        {
            if (PyErr_Occurred())
                PyErr_Print();
            cerr << "Cannot find function store" << endl;
            return 1;
        }

        pFini = PyObject_GetAttrString(pModule, "finalize");
        if (!(pFini && PyCallable_Check(pFini)))
        {
            if (PyErr_Occurred())
                PyErr_Print();
            cerr << "Cannot find function finalize" << endl;
            return 1;
        }
    }
    else
    {
        PyErr_Print();
        cerr << "Failed to load cachesim_itf.py" << endl;
        return 1;
    }



    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    // Initialize the memory reference buffer
    bufId = PIN_DefineTraceBuffer(sizeof(struct MEMREF), NUM_BUF_PAGES,
                                  BufferFull, 0);

    if (bufId == BUFFER_ID_INVALID)
    {
        cerr << "Error allocating initial buffer" << endl;
        return 1;
    }

    PIN_InitLock(&fileLock);

    // add an instrumentation function
    TRACE_AddInstrumentFunction(Trace, 0);
    
    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // buf_key = PIN_CreateThreadDataKey(0);
    // PIN_AddThreadStartFunction(ThreadStart, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}


