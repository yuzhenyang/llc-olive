#ifndef LLC_OLIVE_H
#define LLC_OLIVE_H

#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <set>
#include <map>
#include <vector>
#include <cmath>

#include <llvm/Support/CommandLine.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>

#include "Insts.h"
#include "Tree.h"
#include "FunctionState.h"
#include "GlobalState.h"

class GlobalState;
class FunctionState;

typedef struct COST {
    int cost;
} COST;
#define COST_LESS(a,b) ((a).cost < (b).cost)

static COST COST_INFINITY = { 32767 };
static COST COST_ZERO     = { 0 };

static int _ern = 0;

static int shouldTrace = 0;
static int shouldCover = 0;

static void burm_trace(NODEPTR, int, COST);
void gen(NODEPTR p, FunctionState *fstate);


#endif
