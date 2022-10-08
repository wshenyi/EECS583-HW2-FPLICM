//===-- Frequent Path Loop Invariant Code Motion Pass ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// EECS583 F22 - This pass can be used as a template for your Frequent Path LICM
//               homework assignment. The pass gets registered as "fplicm".
//
// This pass performs loop invariant code motion, attempting to remove as much
// code from the body of a loop as possible.  It does this by either hoisting
// code into the preheader block, or by sinking code to the exit blocks if it is
// safe.
//
////===----------------------------------------------------------------------===//
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"

/* *******Implementation Starts Here******* */
// include necessary header files
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <stack>
/* *******Implementation Ends Here******* */

using namespace llvm;

#define DEBUG_TYPE "fplicm"


namespace Correctness{
class OperandInfo {
public:
//    OperandInfo() = default;
    explicit OperandInfo(Value *Operand, LoadInst* LI, StoreInst* SI) {
        operand = Operand;
        loads.push_back(LI);
        stores.push_back(SI);
    }

    void Insert(LoadInst* LI, StoreInst* SI) {
        if (std::find(loads.begin(), loads.end(), LI) == loads.end())
            loads.push_back(LI);
        if (std::find(stores.begin(), stores.end(), SI) == stores.end())
            stores.push_back(SI);
    }

public:
    Value *operand; // The instruction that defines this operand
    std::vector<LoadInst*> loads;
    std::vector<StoreInst*> stores;
};

struct FPLICMPass : public LoopPass {
    static char ID;
    double Threshold = 0.799999;
    FPLICMPass() : LoopPass(ID) {}

    bool runOnLoop(Loop *L, LPPassManager &LPM) override {
      BlockFrequencyInfo &bfi = getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
      BranchProbabilityInfo &bpi = getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
      LoopInfo &LoopInfo = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

      /* *******Implementation Starts Here******* */

      auto BBs = L->getBlocks();
      BasicBlock *cur = BBs[1];
      std::set<BasicBlock*> fb;
      std::set<BasicBlock*> ifb;
      std::vector<Instruction*> frequent_loads;
      std::vector<Instruction*> frequent_stores;
      std::vector<Instruction*> infrequent_stores;

      // Traverse BBs to find frequent path
      while (cur != BBs[0]) {
          fb.insert(cur);
          // Check Instructions in current BB
          for (auto &I : *cur) {
              if (I.getOpcode() == Instruction::Load){
                  frequent_loads.push_back(&I);
              }else if (I.getOpcode() == Instruction::Store) {
                  frequent_stores.push_back(&I);
              }
          }

          // Determine where to go next
          auto exit_ins = cur->getTerminator();
          if (exit_ins->getNumSuccessors() > 1){
              auto ite = successors(cur).begin();
              BasicBlock *left  = *ite;
              BasicBlock *right = *(ite+1);
              double N = bpi.getEdgeProbability(&*cur, left).getNumerator();
              double p = N / (bpi.getEdgeProbability(&*cur, left).getDenominator());
              if (p > Threshold) {
                  cur = left;
                  ifb.insert(right);
                  // errs() << *right->getInstList().begin() << "\n";
              }else if (p < 1 - Threshold){
                  cur = right;
                  ifb.insert(left);
                  // errs() << *left->getInstList().begin() << "\n";
              }else{
                  errs() << p << " is less than 0.8 but greater than 0.2!\n";
                  break;
              }
          }else{
              cur = cur->getUniqueSuccessor();
          }
      }

      // If no infrequent path
      if (ifb.empty()) return false;

      // Get infrequent blocks
      std::deque<BasicBlock*> bfs;
      for (auto *BB : ifb) {
          bfs.push_back(BB);
          while (!bfs.empty()) {
              // Check Instructions in current BB
              for (auto &I : *bfs.front()) {
                  if (I.getOpcode() == Instruction::Store) {
                      infrequent_stores.push_back(&I);
                  }
              }
              for (auto *succ : successors(bfs.front())) {
                  if (std::find(bfs.begin(), bfs.end(), succ) == bfs.end()
                      && fb.find(succ) == fb.end()){
                      bfs.push_back(succ);
                  }
              }
              bfs.pop_front();
          }
      }

      // Check if we need to do FPLICM
      std::map<Value*, Correctness::OperandInfo> info;
      for (auto I : infrequent_stores) {
          auto operand = I->getOperand(1);
          for (auto li : frequent_loads) {
              if (li->getOperand(0) == operand) {
                  bool dep = false;
                  for (auto si : frequent_stores) {
                      if (si->getOperand(1) == operand) {
                          dep = true;
                          break;
                      }
                  }
                  if (!dep) {
                      auto ite = info.find(operand);
                      if (ite != info.end()) {
                          ite->second.Insert(dyn_cast<LoadInst>(li), dyn_cast<StoreInst>(I));
                      }else{
                          info.insert(ite, std::pair<Value*, Correctness::OperandInfo>(operand, Correctness::OperandInfo(operand, dyn_cast<LoadInst>(li), dyn_cast<StoreInst>(I))));
                      }
                  }
              }
          }
      }

      // If no instructions need to be hoisted
      if (info.empty()) return false;

      // Analyze FPLICM
      for (auto ite : info) {
          FPLICM(L->getLoopPreheader(), ite.second);
      }

      return true;
  }

    static void FPLICM(BasicBlock *PreHeader, OperandInfo& info) {
        Instruction *terminator = PreHeader->getTerminator();
        std::vector<Instruction *> ins_list;

        for (auto load : info.loads) {
            auto *cur = dyn_cast<Instruction>(*load->user_begin());
            auto prev = load;
            load->moveBefore(terminator);

            // Allocate new var on stack ot store post-calculated value.
            auto *var = new AllocaInst(prev->getType(), 0, nullptr, Align(4), "var", terminator);
            // Insert new store to ins_list
            ins_list.push_back(new StoreInst(prev, var, terminator));
            // Insert new load to directly load post-calculated value.
            auto *new_load = new LoadInst(prev->getType(), var, "fix", cur);
            // Chang specific operand of cur instruction to the post-calculated value
            cur->setOperand(cur->getOperand(0) == prev ? 0 : 1, new_load);
        }

        for (auto store : info.stores) {
            Instruction *curr, *prev, *origin;
            origin = dyn_cast<Instruction>(store->getOperand(0));
            prev = origin;
            for (auto I : ins_list) {
                curr = I->clone();
                if (I->getOpcode() == Instruction::Store){
                    curr->setOperand(0, prev);
                    prev = origin;
                }else{
                    unsigned idx = 0;
                    // Assume instructions are continues. Maybe not correct!!!
                    for (auto p = I->op_begin(); p != I->op_end() ; p++ ) {
                        if (*p == I->getPrevNode()){
                            curr->setOperand(idx, prev);
                        }
                        idx++;
                    }
                    prev = curr;
                }
                curr->insertBefore(store);
            }
            store->removeFromParent();
            store->deleteValue();
        }
    }


    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<BranchProbabilityInfoWrapperPass>();
        AU.addRequired<BlockFrequencyInfoWrapperPass>();
        AU.addRequired<LoopInfoWrapperPass>();
    }

private:
  /// Little predicate that returns true if the specified basic block is in
  /// a subloop of the current one, not the current one itself.
    bool inSubLoop(BasicBlock *BB, Loop *CurLoop, LoopInfo *LI) {
        assert(CurLoop->contains(BB) && "Only valid if BB is IN the loop");
        return LI->getLoopFor(BB) != CurLoop;
    }

};
} // end of namespace Correctness

char Correctness::FPLICMPass::ID = 0;
static RegisterPass<Correctness::FPLICMPass> X("fplicm-correctness", "Frequent Loop Invariant Code Motion for correctness test",
                                               false, false);


namespace Performance{
struct FPLICMPass : public LoopPass {
    static char ID;
    double Threshold = 0.799999;
    FPLICMPass() : LoopPass(ID) {}

    bool runOnLoop(Loop *L, LPPassManager &LPM) override {
        BlockFrequencyInfo &bfi = getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
        BranchProbabilityInfo &bpi = getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
        LoopInfo &LoopInfo = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

        /* *******Implementation Starts Here******* */
        auto BBs = L->getBlocks();
        BasicBlock *cur = BBs[1];
        std::set<BasicBlock*> fb;
        std::set<BasicBlock*> ifb;
        std::vector<Instruction*> frequent_loads;
        std::vector<Instruction*> frequent_stores;
        std::vector<Instruction*> infrequent_stores;

        // Traverse BBs to find frequent path
        while (cur != BBs[0]) {
            fb.insert(cur);
            // Check Instructions in current BB
            for (auto &I : *cur) {
                if (I.getOpcode() == Instruction::Load){
                    frequent_loads.push_back(&I);
                }else if (I.getOpcode() == Instruction::Store) {
                    frequent_stores.push_back(&I);
                }
            }

            // Determine where to go next
            auto exit_ins = cur->getTerminator();
            if (exit_ins->getNumSuccessors() > 1){
                auto ite = successors(cur).begin();
                BasicBlock *left  = *ite;
                BasicBlock *right = *(ite+1);
                double N = bpi.getEdgeProbability(&*cur, left).getNumerator();
                double p = N / (bpi.getEdgeProbability(&*cur, left).getDenominator());
                if (p > Threshold) {
                  cur = left;
                  ifb.insert(right);
                  // errs() << *right->getInstList().begin() << "\n";
                }else if (p < 1 - Threshold){
                  cur = right;
                  ifb.insert(left);
                  // errs() << *left->getInstList().begin() << "\n";
                }else{
                      errs() << p << " is less than 0.8 but greater than 0.2!\n";
                      break;
                }
            }else{
                cur = cur->getUniqueSuccessor();
            }
        }

        // If no infrequent path
        if (ifb.empty()) return false;

        // Get infrequent blocks
        std::deque<BasicBlock*> bfs;
        for (auto *BB : ifb) {
            bfs.push_back(BB);
            while (!bfs.empty()) {
                // Check Instructions in current BB
                for (auto &I : *bfs.front()) {
                    if (I.getOpcode() == Instruction::Store) {
                        infrequent_stores.push_back(&I);
                    }
                }
                for (auto *succ : successors(bfs.front())) {
                    if (std::find(bfs.begin(), bfs.end(), succ) == bfs.end()
                        && fb.find(succ) == fb.end()
                        && !inSubLoop(succ, L, &LoopInfo)){
                        bfs.push_back(succ);
                    }
                }
                bfs.pop_front();
            }
        }

        // Check if we need to do FPLICM
        std::map<Value*, Correctness::OperandInfo> info;
//        std::set<Instruction*> insert_pos;
        for (auto I : infrequent_stores) {
            auto operand = I->getOperand(1);
            for (auto li : frequent_loads) {
                if (li->getOperand(0) == operand) {
                    bool dep = false;
                    for (auto si : frequent_stores) {
                        if (si->getOperand(1) == operand) {
                            dep = true;
                            break;
                        }
                    }
                    if (!dep) {
                        auto ite = info.find(operand);
//                        insert_pos.insert(I);
                        if (ite != info.end()) {
                            ite->second.Insert(dyn_cast<LoadInst>(li), dyn_cast<StoreInst>(I));
                        }else{
                            info.insert(ite, std::pair<Value*, Correctness::OperandInfo>(operand, Correctness::OperandInfo(operand, dyn_cast<LoadInst>(li), dyn_cast<StoreInst>(I))));
                        }
                    }
                }
            }
        }

        // If no instructions need to be hoisted
        if (info.empty()) return false;

        // Analyze FPLICM
//        errs() << "-----------FPLICM Start!-------------\n";
        for (auto ite : info) {
            FPLICM(L->getLoopPreheader(), ite.second);
        }
//        errs() << "-----------FPLICM Done!-------------\n";

        // Doing constant folding here
//        errs() << "-------Constant Folding Start!-------\n";
        ConstantFolding(BBs[1], L->getLoopPreheader());
//        errs() << "-------Constant Folding Done!-------\n";
        /* *******Implementation Ends Here******* */

        return true;
    }

    static void FPLICM(BasicBlock *PreHeader, Correctness::OperandInfo& info) {
        Instruction *terminator = PreHeader->getTerminator();
        std::vector<Instruction *> ins_list;

//        errs() << "=========FPLICM==========\n";
//        errs() << "Number of load and store\n";
//        errs() << "load: " << info.loads.size() << "\n";
//        errs() << "store: " << info.stores.size() << "\n";
//        errs() << "Hoisted load instructions\n";
//        for (auto &I : info.loads) errs() << *I << "\n";
//        errs() << "=========================\n";

        unsigned num = 0;
        for (auto load : info.loads) {
            auto *cur = dyn_cast<Instruction>(*load->user_begin());
            while (true) {
                if (cur->getNumOperands() == 2) {
                    if (dyn_cast<Instruction>(cur->getOperand(1)) == nullptr) {
                        ins_list.push_back(cur);
                        cur = dyn_cast<Instruction>(*cur->user_begin());
                        continue;
                    }

                    break;

                } else {
                    ins_list.push_back(cur);
                    cur = dyn_cast<Instruction>(*cur->user_begin());
                }
            }

            auto prev = *ins_list.rbegin();
            load->moveBefore(terminator);
            for(auto ite = ins_list.begin()+num; ite != ins_list.end(); ite++){
                (*ite)->moveBefore(terminator);
            }

            // Allocate new var on stack ot store post-calculated value.
            auto *var = new AllocaInst(prev->getType(), 0, nullptr, Align(16), "var", terminator);
            // Insert new store to ins_list
            ins_list.push_back(new StoreInst(prev, var, terminator));
            // Insert new load to directly load post-calculated value.
            auto *new_load = new LoadInst(prev->getType(), var, "fix", cur);
            // Chang specific operand of cur instruction to the post-calculated value
            if (cur->getOpcode() == Instruction::Store) {
                std::vector<Value*> temp_save;
                for (auto *usr : cur->getOperand(1)->users()) {
                    if (dyn_cast<Instruction>(usr)->getOpcode() == Instruction::Load 
                        && dyn_cast<Instruction>(usr)->getParent() == dyn_cast<Instruction>(cur)->getParent()){
                            usr->replaceAllUsesWith(new_load);
                            temp_save.push_back(usr);
                    }
                }
                for (auto *i : temp_save) dyn_cast<Instruction>(i)->eraseFromParent();
                cur->eraseFromParent();
                dyn_cast<Instruction>(cur->getOperand(1))->eraseFromParent();
            }else{
                cur->setOperand(cur->getOperand(0) == prev ? 0 : 1, new_load);
            }

            num = ins_list.size();
        }

        Value *curr, *prev, *origin;
        for (auto store : info.stores) {
            origin = store->getOperand(0);
            prev = origin;
            for (auto I : ins_list) {
                curr = I->clone();
                auto Icurr = dyn_cast<Instruction>(curr);
                if (I->getOpcode() == Instruction::Store){
                    Icurr->setOperand(0, prev);
                    prev = origin;
                }else{
                    unsigned idx = 0;
                    // Assume instructions are continues. Maybe not correct!!!
                    for (auto p = I->op_begin(); p != I->op_end() ; p++ ) {
                        if (*p == I->getPrevNode()){
                            Icurr->setOperand(idx, prev);
                        }
                        idx++;
                    }
                    prev = curr;
                }
                Icurr->insertBefore(store);
            }
            store->eraseFromParent();
        }
    }

    static void ConstantFolding(BasicBlock* cur_bb, BasicBlock* PreHeader) {
        std::vector<Instruction*> loads;
        std::vector<Instruction*> stores;
//        std::set<Value*> meet;
        for (auto &I: *cur_bb) {
            if (I.getOpcode() == Instruction::Store) stores.push_back(&I);
        }
        for (auto store : stores) {
            for (auto usr : store->getOperand(1)->users()) {
                auto load = dyn_cast<Instruction>(usr);
                if (store == load || load->getParent() != cur_bb) continue; // usr may be store itself
                loads.push_back(load);
            }
            if (!loads.empty()) {
//                auto insert_position = PreHeader->getTerminator()->getPrevNode();
//                auto cur = dyn_cast<Instruction>(store->getOperand(0));
//                Value *right;
//                meet.insert(cur);
//
//                while (cur != nullptr) {
//                    if (meet.find(cur) != meet.end()) {
////                        backward.push(cur);
//                        cur->moveAfter(insert_position);
//                        meet.insert(cur->getOperand(0));
//                        if (cur->getNumOperands() == 2) {
//                            right = cur->getOperand(1);
//                            if (dyn_cast<Instruction>(right) != nullptr)
//                                meet.insert(right);
//                        }else if (cur->getNumOperands() == 3){
//                            right = cur->getOperand(2);
//                            if (dyn_cast<Instruction>(right) != nullptr)
//                                meet.insert(right);
//                        }
//                    }
//                    cur = cur->getPrevNode();
//                }

                for (auto li : loads){
                    li->replaceAllUsesWith(store->getOperand(0));
                    li->eraseFromParent();
                }
                store->eraseFromParent();
                dyn_cast<Instruction>(store->getOperand(1))->eraseFromParent();
                loads.clear();
//                meet.clear();
            }
        }
    }

    static void ConstantFolding(BasicBlock* cur_bb, std::vector<Instruction*> &ins_list) {

        std::vector<std::pair<Instruction*,Instruction*>> store_load;
        for (auto &I : *cur_bb) {
            if (I.getOpcode() == Instruction::Store) {
                for (auto usr : I.getOperand(1)->users()) {
                    // assert((dyn_cast<Instruction>(usr)->getParent() == cur_bb) && "load store pair should be in the same BB");
                    if (&I != dyn_cast<Instruction>(usr) && dyn_cast<Instruction>(usr)->getParent() == cur_bb)
                        store_load.emplace_back(&I, dyn_cast<Instruction>(usr));
                }
            }
        }
        std::deque<Instruction*> bfs;
        for (auto &sl : store_load) {
            std::stack<Instruction*> backward;
            std::set<Value*> meet;
            Value *right;
            // add backword instuctions
            auto cur = dyn_cast<Instruction>(sl.first->getOperand(0));
            meet.insert(cur);

            while (cur != nullptr) {
                if (meet.find(cur) != meet.end()) {
                    backward.push(cur);
                    meet.insert(cur->getOperand(0));
                    if (cur->getNumOperands() == 2) {
                        right = cur->getOperand(1);
                        if (dyn_cast<Instruction>(right) != nullptr)
                            meet.insert(right);
                    }else if (cur->getNumOperands() == 3){
                        right = cur->getOperand(2);
                        if (dyn_cast<Instruction>(right) != nullptr)
                            meet.insert(right);
                    }
                }
                cur = cur->getPrevNode();
            }
            errs() << "+++++++++++++++\n";
            while (!backward.empty()) {
                errs() << *backward.top() << "\n";
                ins_list.push_back(backward.top());
                backward.pop();
            }
            errs() << "+++++++++++++++\n";

            sl.second->replaceAllUsesWith(sl.first->getOperand(0));
            sl.first->eraseFromParent();
            sl.second->eraseFromParent();
//            dyn_cast<Instruction>(sl.first->getOperand(1))->eraseFromParent();
    }
}

void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<BranchProbabilityInfoWrapperPass>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
}

private:
    /// Little predicate that returns true if the specified basic block is in
    /// a subloop of the current one, not the current one itself.
    bool inSubLoop(BasicBlock *BB, Loop *CurLoop, LoopInfo *LI) {
        assert(CurLoop->contains(BB) && "Only valid if BB is IN the loop");
        return LI->getLoopFor(BB) != CurLoop && BB != LI->getLoopFor(BB)->getHeader();
    }

};
} // end of namespace Performance

char Performance::FPLICMPass::ID = 0;
static RegisterPass<Performance::FPLICMPass> Y("fplicm-performance", "Frequent Loop Invariant Code Motion for performance test",
                                               false, false);
