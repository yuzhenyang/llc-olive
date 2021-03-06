#ifndef TREE_H
#define TREE_H

#include <cmath>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <vector>

#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#include "Insts.h"
#include "Value.h"

enum X86OperandType;
class X86Operand;
class FunctionState;

enum OPERATION { READ, WRITE };

class Tree
{
public:
    Tree(int opcode)
        : op(opcode), val(-1), refcnt(0), level(1), operand(nullptr), 
          otype(-1), isReg(false), isPhysicalReg(false), computed(false), 
          isPtr(false), isPhi(false), suffix("q"), phiParent(nullptr),
          cnstInt(64, 0), cnstFP(0.0), hasValue(false), num_args(-1)
          
    {
    }
    Tree(int opcode, VALUE v)
        : op(opcode), val(v), refcnt(0), level(1), operand(nullptr), 
          otype(-1), isReg(false), isPhysicalReg(false), computed(false), 
          isPtr(false), isPhi(false), suffix("q"), phiParent(nullptr),
          cnstInt(64, 0), cnstFP(0.0), hasValue(false), num_args(-1)
    {
        SetValue(v);        // hasValue=true
    }
    Tree(int opcode, Tree *l, Tree *r)
        : op(opcode), val(-1), refcnt(0), level(1), operand(nullptr), 
          otype(-1), isReg(false), isPhysicalReg(false), computed(false), 
          isPtr(false), isPhi(false), suffix("q"), phiParent(nullptr),
          cnstInt(64, 0), cnstFP(0.0), hasValue(false), num_args(-1)
    {
        AddChild(l);
        AddChild(r);
    }

    virtual ~Tree();

    void SetOpCode(int c) { op = c; }
    int GetOpCode() const { 
        // std::cerr << "GET OPCODE: " << op << std::endl;
        return op; 
    }
    int GetNumArgs() const { return num_args; }
    
    void SetValue(VALUE v) { val = v; hasValue = true; }
    VALUE& GetValue() { return val; }
    bool HasValue() { return hasValue; }

    void SetFuncName(std::string n) { func_name = n; }
    std::string GetFuncName() const { return func_name; }

    void SetVariableName(std::string n) { variable_name = n; }
    std::string GetVariableName() const { return variable_name; }

    void UseAsPhi() { isPhi = true; otype = REG; }
    void UseAsPtr() { isReg = false; isPtr = true; otype = MEM; }
    void UseAsMemory() { isReg = false; otype = MEM; }
    void UseAsImmediate() { isReg = false; otype = IMM; }
    void UseAsRegister() { isReg = true; otype = REG; }

    void UseAsVirtualRegister() { isReg = true; otype = REG; }
    void UseAsPhysicalRegister() { isReg = true; isPhysicalReg = true; otype = REG; }
    bool IsVirtualReg() const { return isReg && !isPhysicalReg; }
    bool IsPhysicalReg() const { return isPhysicalReg; }
    bool IsPointer() const { return isPtr; }
    bool IsPhiNode() const { return isPhi; }
    bool IsMemory() const { return otype == MEM; }
    int  GetVirtualReg() const { return val.AsVirtualReg(); }
    int  GetPhysicalReg() const { return val.AsVirtualReg(); }

    void SetComputed(bool c) { computed = c; }
    bool IsComputed() const { return computed; }

    void SetInst(llvm::Instruction *inst) { 
        using namespace llvm;
        this->llvmValue = dyn_cast<llvm::Value>(inst); 
    }
    llvm::Instruction *GetInst() { 
        using namespace llvm;
        return dyn_cast<llvm::Instruction>(llvmValue); 
    }

    void SetLLVMValue(llvm::Value *value) { this->llvmValue = value; }
    llvm::Value *GetLLVMValue() { return llvmValue; }

    void SetLevel(int level) { this->level = level; }
    int GetLevel() const { return this->level; }

    Tree** GetKids() { return &kids[0]; }
    void AddChild(Tree *ct);
    Tree* GetChild(int n);
    void KidsAsArguments();

    Tree* GetTreeRef() { 
        if (!isPhi) { refcnt++; return this; }
        assert(phiParent != nullptr && "a phi instruction cannot find its origin");
        int oldref = phiParent->GetRefCount();
        phiParent->GetTreeRef();    // increment parent's refcnt
        int newref = phiParent->GetRefCount();
        assert(oldref != newref);
        return this;
    }
    void RemoveRef() { 
        if (!isPhi) { refcnt--; }
        else {
            assert(phiParent != nullptr && "a phi instruction cannot find its origin");
            phiParent->RemoveRef();     // remove parent's refcnt
        }
    }

    int GetRefCount() const { return refcnt; }
    int GetNumKids() const { return kids.size(); }

    void CastAPInt(llvm::APInt cnst);
    void CastAPFloat(llvm::APFloat cnst);
    void CastInt(llvm::ConstantInt *cnst);
    void CastFP(llvm::ConstantFP *cnst);

    void DisplayTree() { DisplayTree(0); std::cerr << "\n"; }

    // leave these attributes public for simplicity
	struct { struct burm_state *state; } x;
	VALUE val;

    void SetX86Operand(X86Operand *oper) { operand = oper; }
    X86Operand *AsX86Operand(FunctionState *fs);

    void SetSuffix(std::string sf) { suffix = sf; }
    const char* GetSuffix() const { return suffix.c_str(); }

    llvm::APInt GetInteger() const { return cnstInt; }
    llvm::APFloat GetFloat() const { return cnstFP; }

    void SetParent(Tree *parent) { phiParent = parent; }
    Tree *GetParent() { return phiParent; }
    
    
    void AddRead() { 
        operations.push_back(OPERATION::READ); 
        for (auto kid : kids) kid->AddRead();
    }
    void AddWrite() { 
        operations.push_back(OPERATION::WRITE); 
        for (auto kid : kids) kid->AddWrite();
    }

    bool IsFlowDepdendent() {
        if (computed) return true;      // if this node has been computed, then there is no dependency problem

        if (operations.size() >= 2) {
            for (auto it = operations.begin(); it != operations.end() - 1; it++) {
                OPERATION curr = *it;
                OPERATION next = *(it + 1);
                // flow dependence is read after write
                if (curr == OPERATION::WRITE && next == OPERATION::READ)
                    return true;
            }
        }
        // check children's dependencies
        for (auto kid : kids)
            if (kid->IsFlowDepdendent())
                return true;
        return false;
    }

    void ClearDependence() {
        operations.clear();
        for (auto kid : kids)
            kid->ClearDependence();
    }

private:
    void DisplayTree(int indent);

private:
	int op;
    int otype;      // used for operand generation

    int level;
    int refcnt;
    int num_args;

    bool isReg;
    bool computed;
    bool hasValue;
    bool isPhysicalReg;
    bool isPtr;
    bool isPhi;

    Tree *phiParent;

    // dependence analysis
    std::vector<OPERATION> operations;

    X86Operand *operand;
    llvm::APInt   cnstInt;
    llvm::APFloat cnstFP;

    std::string func_name;
    std::string variable_name;
    std::string suffix;

    X86OperandType operandType;
    llvm::Value *llvmValue;
    std::vector<Tree*> kids;
    std::vector<Tree*> freeList;
};

#define GET_KIDS(p)	((p)->GetKids())
#define PANIC printf
#define STATE_LABEL(p)  ((p)->x.state)
#define SET_STATE(p,s)  (p)->x.state=(s)
#define DEFAULT_COST	break
#define OP_LABEL(p)     ((p)->GetOpCode())
#define NO_ACTION(x)

typedef Tree* NODEPTR;

#endif /* end of include guard: TREE_H */
