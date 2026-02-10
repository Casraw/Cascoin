// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/vmstate.h>
#include <stdexcept>

namespace CVM {

VMState::VMState() 
    : programCounter(0), gasRemaining(0), gasLimit(0), 
      callValue(0), blockHeight(0), timestamp(0),
      status(Status::RUNNING) {
}

VMState::~VMState() {
}

void VMState::Push(const arith_uint256& value) {
    if (stack.size() >= MAX_STACK_SIZE) {
        status = Status::STACK_OVERFLOW;
        errorMessage = "Stack overflow";
        return;
    }
    stack.push_back(value);
}

arith_uint256 VMState::Pop() {
    if (stack.empty()) {
        status = Status::STACK_UNDERFLOW;
        errorMessage = "Stack underflow";
        return arith_uint256();
    }
    arith_uint256 value = stack.back();
    stack.pop_back();
    return value;
}

arith_uint256 VMState::Peek(size_t depth) const {
    if (depth >= stack.size()) {
        return arith_uint256();
    }
    return stack[stack.size() - 1 - depth];
}

void VMState::Swap(size_t depth) {
    if (depth >= stack.size() || stack.empty()) {
        status = Status::STACK_UNDERFLOW;
        errorMessage = "Stack underflow in swap";
        return;
    }
    size_t idx = stack.size() - 1 - depth;
    std::swap(stack.back(), stack[idx]);
}

void VMState::Dup(size_t depth) {
    if (depth >= stack.size()) {
        status = Status::STACK_UNDERFLOW;
        errorMessage = "Stack underflow in dup";
        return;
    }
    if (stack.size() >= MAX_STACK_SIZE) {
        status = Status::STACK_OVERFLOW;
        errorMessage = "Stack overflow in dup";
        return;
    }
    arith_uint256 value = Peek(depth);
    stack.push_back(value);
}

bool VMState::UseGas(uint64_t amount) {
    if (amount > gasRemaining) {
        gasRemaining = 0;
        status = Status::OUT_OF_GAS;
        errorMessage = "Out of gas";
        return false;
    }
    gasRemaining -= amount;
    return true;
}

void VMState::SaveSnapshot() {
    Snapshot snapshot;
    snapshot.stack = stack;
    snapshot.programCounter = programCounter;
    snapshot.gasRemaining = gasRemaining;
    snapshots.push_back(snapshot);
}

void VMState::RevertToSnapshot() {
    if (snapshots.empty()) {
        return;
    }
    Snapshot& snapshot = snapshots.back();
    stack = snapshot.stack;
    programCounter = snapshot.programCounter;
    gasRemaining = snapshot.gasRemaining;
    snapshots.pop_back();
}

void VMState::CommitSnapshot() {
    if (!snapshots.empty()) {
        snapshots.pop_back();
    }
}

} // namespace CVM

