#define DEBUG_INST  0
#define THRESHOLD   5

using namespace llvm;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::init("-"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("<output filename>"), cl::value_desc("filename"));

static cl::opt<int>
NumRegs("num_regs", cl::desc("<number of registers available>"), cl::init(16));

/* burm_trace - print trace message for matching p */
static void burm_trace(NODEPTR p, int eruleno, COST cost) {
    if (shouldTrace)
        std::cerr << p << " matched " << burm_string[eruleno] << " = " << eruleno << " with cost " << cost.cost << "\n";
}

static std::string MRI2String(int type, Tree t) {
    std::stringstream ss;
    switch (type) {
        case IMM:
            ss << "$" << t->val.val.i32s;
            return ss.str();
        case REG:
            if (t->val.val.i32s < 0) {
                return std::string("<reg-not-allocated>");
            }
            if (t->val.val.i32s > NUM_REGS) {
                std::cerr << "EXCEED MAXIMUM NUMBER OF REGS: " << t->val.val.i32s << std::endl;
                exit(EXIT_FAILURE);
            }
            ss << "$" << registers[t->val.val.i32s];
            return ss.str();
        case MEM:
            ss << "(" << t->val.val.i32u << ")";
            return ss.str();
        default:
            std::cerr << "PrintMRI(...) Error: Unexpected type " << type << std::endl;
            exit(EXIT_FAILURE);
    }
}

static Tree tree(int op, Tree l, Tree r, VALUE v = 0) {
	Tree t = (Tree) malloc(sizeof *t);
	t->op = op;
	t->kids[0] = l; t->kids[1] = r;
	t->val = v;
	t->x.state = 0;
    t->level = 0;
    t->refcnt = 0;
	return t;
}

static void gen(NODEPTR p, FunctionState *fstate) {
	if (burm_label(p) == 0)
		std::cerr << "no cover\n";
	else {
		stmt_action(p->x.state, fstate);
		if (shouldCover != 0)
			dumpCover(p, 1, 0);
	}
}

/**
 * Wrapper for tree
 * */
class TreeWrapper
{
public:
    TreeWrapper(int opcode, Tree l = nullptr, Tree r = nullptr): nchild(0) {
        t = tree(opcode, l, r);
        t->refcnt = 0;
        t->level = 1;
        t->val.val.i32s = -1;           // implicitly defined as a register, -1 means not allocated
        if (l) { t->level = std::max(t->level, 1 + l->level); nchild++; }
        if (r) { t->level = std::max(t->level, 1 + r->level); nchild++; }
    }
    virtual ~TreeWrapper() {
        free(t);
    }
    int GetOpCode() const { return t->op; }
    void SetValue(VALUE v) {
        t->val = v;
    }
    int GetVirtualReg() const { return t->val.val.i32s; }
    void SetLevel(int level) { t->level = level; }
    void SetChild(int n, Tree ct) {
        if (n < 2) {
            if (!t->kids[n] && ct) nchild++;     // was nullptr, but not null now, increment the nchild counter
            if (ct) t->level = std::max(t->level, 1 + ct->level);
            t->kids[n] = ct;
        } else {
            // TODO: IMPLEMENT A WAY TO STORE MORE THAN 2 CHILDREN
            errs() << "SetChild for n=" << n << " is not implemented yet!\n";
        }
    }
    Tree GetChild(int n) {
        if (n < 2) {
            return t->kids[n];
        } else {
            // TODO: IMPLEMENT A WAY TO FETCH MORE THAN 2 CHILDREN
            errs() << "GetChild for n=" << n << " is not implemented yet!\n";
            return nullptr;
        }
    }
    Tree GetTree() { return t; }
    Tree GetTreeWrapper() { t->refcnt++; return t; }

    int GetLevel() const { return t->level; }
    int GetRefCount() const { return t->refcnt; }
    int GetNumChildren() const { return nchild; }

    void CastInt(int n, ConstantInt *cnst) {
        Tree ct = tree(IMM, nullptr, nullptr);
        const APInt integer = cnst->getValue();
        unsigned bitWidth = cnst->getBitWidth();
        if (integer.isSignedIntN(bitWidth)) {
            switch (bitWidth) {
                case 8:
                    ct->val.val.i8s = (int8_t)cnst->getSExtValue();
                    break;
                case 16:
                    ct->val.val.i16s = (int16_t)cnst->getSExtValue();
                    break;
                case 32:
                    ct->val.val.i32s = (int32_t)cnst->getSExtValue();
                    break;
                case 64:
                    ct->val.val.i64s = (int64_t)cnst->getSExtValue();
                    break;
                default:
                    errs() << "CAST CONSTANT INT FAILURE\n";
                    exit(EXIT_FAILURE);
            }
        }
        else {
            switch (bitWidth) {
                case 8:
                    ct->val.val.i8u = (uint8_t)cnst->getZExtValue();
                    break;
                case 16:
                    ct->val.val.i16u = (uint16_t)cnst->getZExtValue();
                    break;
                case 32:
                    ct->val.val.i32u = (uint32_t)cnst->getZExtValue();
                    break;
                case 64:
                    ct->val.val.i64u = (uint64_t)cnst->getZExtValue();
                    break;
                default:
                    errs() << "CAST CONSTANT INT FAILURE\n";
                    exit(EXIT_FAILURE);
            }
        }
        this->SetChild(n, ct);
    }

    void CastFP(int n, ConstantFP *cnst) {
        Tree ct = tree(IMM, nullptr, nullptr);
        // for now, assume all floating point uses 64 bit double
        ct->val.val.f64 = (double) cnst->getValueAPF().convertToDouble();
        this->SetChild(n, ct);
    }


    void DisplayTree() const {
        DisplayTree(t, 0);
    }

private:
    void DisplayTree(Tree t, int indent = 0) const {
        if (t == nullptr) return;
        for (int i = 0; i < 2 * indent; i++)
            errs() << " ";
        switch (t->op) {
        case REG:
            errs() << "op: " << "reg" << "\tval: " << t->val.val.i32s << "\n";
            break;
        case IMM:
            errs() << "op: " << "imm" << "\tval: " << t->val.val.i32s << "\n";
            break;
        case MEM:
            errs() << "op: " << "mem" << "\tval: " << t->val.val.i32s << "\n";
            break;
        default:
            errs() << "op: " << Instruction::getOpcodeName(t->op) << "\tval: " << t->val.val.i32s << "\n";
        }
        DisplayTree(t->kids[0], indent + 1);
        DisplayTree(t->kids[1], indent + 1);
    }

private:
    Tree t;
    int nchild;
};



/**
 * Generate assembly for a single function
 * */
void FunctionToAssembly(Function &func) {

    // prepare a function state container
    // to store function information, such as
    // local variables, free registers, etc
    FunctionState fstate(NumRegs);

    Function::BasicBlockListType &basic_blocks = func.getBasicBlockList();
    std::map<Instruction*, TreeWrapper*> treeMap;
    std::vector<TreeWrapper*> treeList;
    // === First Pass: collect instruction information, build expr tree
    for (BasicBlock &bb : basic_blocks) {
        for (auto inst = bb.begin(); inst != bb.end(); inst++) {
            Instruction &instruction = *inst;
#if DEBUG_INST
            errs() << "OperandNum: " << instruction.getNumOperands() << "\t";
            instruction.print(errs()); 
            errs() << "\n";
            errs() << "\tinstruction (" << instruction << " ) opcode: " << instruction.getOpcode() << "\n";
#endif
            TreeWrapper *t = new TreeWrapper(instruction.getOpcode());
            int num_operands = instruction.getNumOperands();
            for (int i = 0; i < num_operands; i++) {
                Value *v = instruction.getOperand(i);

                if (Constant *def = dyn_cast<Constant>(v)) {
                    // check if the operand is a constant
                    if (ConstantInt *cnst = dyn_cast<ConstantInt>(v)) {
                        // check if the operand is a constant int
#if DEBUG_INST
                        errs() << "FOUND CONST INT:\t" << *cnst << "\n";
#endif
                        // it->second->SetValue(def->getValue());
                        t->CastInt(i, cnst);
                    }
                    else if (ConstantFP *cnst = dyn_cast<ConstantFP>(v)) {
                        // check if the operand is a constant int
#if DEBUG_INST
                        errs() << "FOUND CONST FP:\t" << *cnst << "\n";
#endif
                        t->CastFP(i, cnst);
                    }
                    else if (ConstantExpr *cnst = dyn_cast<ConstantExpr>(v)) {
                        // check if the operand is a constant int
                        // errs() << "FOUND CONST EXPR:\t" << *cnst << "\n";
                        errs() << "NOT IMPLEMENTED CONST EXPR\n";
                        exit(EXIT_FAILURE);
                    }
                    // ... There are many kinds of constant, right now we do not deal with them ...
                    else {
                        // this is bad and probably needs to terminate the execution
                        errs() << "FAILED TO FIND DEF:\t"; instruction.print(errs()); errs() << "\n";
                        exit(EXIT_FAILURE);
                    }
                }
                else if (Instruction *def = dyn_cast<Instruction>(v)) {
                    // check if we cant find operand's definition
                    auto it = treeMap.find(def);
                    assert(it != treeMap.end() && "operands must be previously defined");
                    t->SetChild(i, it->second->GetTreeWrapper());
#if DEBUG_INST
                    errs() << "FOUND DEF:\t"; it->first->print(errs()); errs() << "\n";
#endif
                }
                else {
                    // TODO: write code to handle these situations
                    errs() << "Unhandle-able instruction operand! Quit\n";
                    exit(EXIT_FAILURE);
                }

            }
            // store the current tree in map
            treeMap.insert(std::pair<Instruction*, TreeWrapper*>(&instruction, t));
            treeList.push_back(t);
        } // end of instruction loop
    } // end of basic block loop

    // === Second Pass: generate assembly code using olive
    int FunctionRegCounter = 0;
    // iterate through tree list for each individual instruction tree
    // replace the complicated/common tree expression with registers
    std::map<Tree, int> virtualRegs;
    for (TreeWrapper *t : treeList) {
        // replace the already computed tree branch with virtual register
        int level = t->GetLevel();
        for (int i = 0; i < t->GetNumChildren(); i++) {
            auto child = t->GetChild(i);
            auto it = virtualRegs.find(child);
            if (it != virtualRegs.end()) {
                t->SetChild(i, tree(REG, nullptr, nullptr, it->second));
                level = std::max(child->level + 1, level);
            }
        }
        t->SetLevel(level);     // since we are changing the structure of the tree, levels may change

#if DEBUG_INST
        errs() << Instruction::getOpcodeName(t->GetOpCode()) << "\tLEVEL:\t" << t->GetLevel() << "\tRefCount:\t" << t->GetRefCount() << "\n";
#endif
        // check if this tree satisfies the condition for saving into register
        if (t->GetRefCount() == 0 || t->GetLevel() * t->GetRefCount() > THRESHOLD) {
            t->SetValue(FunctionRegCounter++);        // every root of tree is implicitly stored in a register
#if DEBUG_INST
            t->DisplayTree();
#endif
            gen(t->GetTree(), &fstate);
            virtualRegs.insert(std::pair<Tree, int>(t->GetTree(), t->GetVirtualReg()));
        }
    } // end of TreeWrapper iteration

    // clean up
    for (TreeWrapper *t : treeList)
        delete t;
}

int main(int argc, char *argv[])
{
#if 0
    // Tree t = tree(Add, 
    //             tree(IMM, nullptr, nullptr, 1), 
    //             tree(IMM, nullptr, nullptr, 2), 
    //         1);
    // Tree t = tree(Store, 
    //             tree(IMM, nullptr, nullptr, 2), 
    //             tree(REG, nullptr, nullptr, 1), 
    //         1);
    Tree t = tree(Store,
                tree(Load,
                    tree(IMM, nullptr, nullptr, 10),
                    tree(Alloca, nullptr, nullptr, 1)),
                tree(REG, nullptr, nullptr, 2),
            1);
    gen(t);
#else
    // parse arguments from command line
    cl::ParseCommandLineOptions(argc, argv, "llc-olive\n");

    // prepare llvm context to read bitcode file
    LLVMContext context;
    SMDiagnostic error;
    std::unique_ptr<Module> module = parseIRFile(StringRef(InputFilename.c_str()), error, context);

    // obtain a function list in module, and iterate over function
    Module::FunctionListType &function_list = module->getFunctionList();
    for (Function &func : function_list) {
        FunctionToAssembly(func);
    }
#endif

    return 0;
}
