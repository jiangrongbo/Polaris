/*
 * Copyright 2021 NSG650
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "thread.h"
#include "../cpu/apic.h"
#include "../kernel/panic.h"
#include "../klibc/lock.h"
#include "../klibc/printf.h"
#include "scheduler.h"
#include "../mm/pmm.h"

static uint32_t nextid = 1;
lock_t thread_lock;

struct thread *alloc_new_thread(struct process *proc) {
	struct thread *thrd = kmalloc(sizeof(struct thread));
	LOCK(thread_lock);
	thrd->tstack = pmm_allocz(TSTACK_SIZE / PAGE_SIZE);
	for(size_t i = 0; i < TSTACK_SIZE; i += PAGE_SIZE) {
		vmm_map(proc->ppagemap, proc->current_top_addr + i, thrd->tstack + i, 0b11);
	}
	if (!thrd->tstack)
		PANIC("Failed to allocate kernel stack page");
	thrd->state_t = INITIAL;
	thrd->block_t = NOTHING;
	thrd->tid = nextid++;
	UNLOCK(thread_lock);
	uint64_t sp = (uintptr_t)thrd->tstack + KSTACK_SIZE;
	sp -= sizeof(struct cpu_context);
	thrd->context = (struct cpu_context *)sp;
	memset(thrd->context, 0, sizeof(struct cpu_context));
	return thrd;
}

void thread_init(uintptr_t addr, uint64_t args, struct process *proc) {
	struct thread *thrd = alloc_new_thread(proc);
	thrd->context->rip = addr;
	thrd->context->rdi = args;
	thrd->killed = false;
	LOCK(thread_lock);
	thrd->state_t = READY;
	UNLOCK(thread_lock);
	vec_push(&proc->ttable, thrd);
}

void thread_create(uintptr_t addr, uint64_t args) {
	struct thread *thrd = alloc_new_thread(running_proc());
	thrd->context->rip = addr;
	thrd->context->rdi = args;
	thrd->killed = false;
	LOCK(thread_lock);
	thrd->state_t = READY;
	UNLOCK(thread_lock);
	vec_push(&running_proc()->ttable, thrd);
}

inline void thread_block(enum block_on reason) {
	asm volatile("cli");
	struct thread *thrd = running_thrd();
	thrd->block_t = reason;
	thrd->state_t = BLOCKED;
	yield_to_scheduler();
}

inline void thread_unblock(struct thread *thrd) {
	asm volatile("cli");
	thrd->block_t = NOTHING;
	thrd->state_t = READY;
}

void thread_exit(uint64_t return_val) {
	struct thread *thrd = running_thrd();
	if (thrd->state_t == BLOCKED && thrd->block_t == ON_WAIT)
		thread_unblock(thrd);
	thrd->state_t = TERMINATED;
	thrd->return_val = return_val;
	pmm_free(thrd->tstack, TSTACK_SIZE/PAGE_SIZE);
	if (thrd == running_proc()->ttable.data[0]) {
		running_proc()->return_code = (uint8_t)thrd->return_val;
		process_exit();
	}
	yield_to_scheduler();
}

void thread_sleep(size_t sleep_ticks) {
	struct thread *thrd = running_thrd();
	size_t curr_tick = timer_tick;
	size_t targetick = curr_tick + sleep_ticks;
	thrd->target_tick = targetick;
	LOCK(thread_lock);
	thread_block(ON_SLEEP);
	UNLOCK(thread_lock);
}
