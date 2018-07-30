// SPDX-License-Identifier: GPL-2.0
/*
 * This code tests several aspects of the STACKLEAK feature:
 *  - the current task stack is properly erased (filled with STACKLEAK_POISON);
 *  - exhausting the current task stack with deep recursion is detected by
 *     CONFIG_VMAP_STACK (which is implied by CONFIG_GCC_PLUGIN_STACKLEAK);
 *  - alloca() calls which overflow the kernel stack hit BUG()/panic() in
 *     stackleak_check_alloca().
 *
 * Authors:
 *   Alexander Popov <alex.popov@linux.com>
 *   Tycho Andersen <tycho@tycho.ws>
 */

#include "lkdtm.h"
#include <linux/stackleak.h>

static noinline bool stack_is_erased(void)
{
	unsigned long *sp, left, found, i;
	const unsigned long check_depth =
			STACKLEAK_SEARCH_DEPTH / sizeof(unsigned long);

	/*
	 * For the details about the alignment of the poison values, see
	 * the comment in stackleak_track_stack().
	 */
	sp = PTR_ALIGN(&i, sizeof(unsigned long));

	left = ((unsigned long)sp & (THREAD_SIZE - 1)) / sizeof(unsigned long);
	sp--;

	/*
	 * One 'long int' at the bottom of the thread stack is reserved
	 * and not poisoned.
	 */
	if (left > 1)
		left--;
	else
		return false;

	pr_info("checking unused part of the thread stack (%lu bytes)...\n",
					left * sizeof(unsigned long));

	/*
	 * Search for 'check_depth' poison values in a row (just like
	 * stackleak_erase() does).
	 */
	for (i = 0, found = 0; i < left && found <= check_depth; i++) {
		if (*(sp - i) == STACKLEAK_POISON)
			found++;
		else
			found = 0;
	}

	if (found <= check_depth) {
		pr_err("FAIL: thread stack is not erased (checked %lu bytes)\n",
						i * sizeof(unsigned long));
		return false;
	}

	pr_info("first %lu bytes are unpoisoned\n",
				(i - found) * sizeof(unsigned long));

	/* The rest of thread stack should be erased */
	for (; i < left; i++) {
		if (*(sp - i) != STACKLEAK_POISON) {
			pr_err("FAIL: thread stack is NOT properly erased\n");
			return false;
		}
	}

	pr_info("the rest of the thread stack is properly erased\n");
	return true;
}

static noinline void do_alloca(unsigned long size)
{
	char buf[size];

	/* So this doesn't get inlined or optimized out */
	snprintf(buf, size, "testing alloca...\n");
}

void lkdtm_STACKLEAK_BIG_ALLOCA(void)
{
	if (!stack_is_erased())
		return;

	pr_info("try a small alloca() of 16 bytes...\n");
	do_alloca(16);
	pr_info("small alloca() is successful\n");

	pr_info("try alloca() over the thread stack boundary...\n");
	do_alloca(THREAD_SIZE);
	pr_err("FAIL: alloca() over the thread stack boundary is NOT detected\n");
}

static noinline unsigned long recursion(unsigned long prev_sp, bool with_alloca)
{
	char buf[400];
	unsigned long sp = (unsigned long)&sp;

	snprintf(buf, sizeof(buf), "testing deep recursion...\n");

	if (with_alloca)
		do_alloca(400);

	if (prev_sp < sp + THREAD_SIZE)
		sp = recursion(prev_sp, with_alloca);

	return sp;
}

void lkdtm_STACKLEAK_DEEP_RECURSION(void)
{
	unsigned long sp = (unsigned long)&sp;

	if (!stack_is_erased())
		return;

	/*
	 * Overflow the thread stack using deep recursion. It should hit the
	 * guard page provided by CONFIG_VMAP_STACK (which is implied by
	 * CONFIG_GCC_PLUGIN_STACKLEAK).
	 */
	pr_info("try to overflow the thread stack using deep recursion...\n");
	pr_err("FAIL: stack depth overflow (%lu bytes) is not detected\n",
							sp - recursion(sp, 0));
}

void lkdtm_STACKLEAK_RECURSION_WITH_ALLOCA(void)
{
	unsigned long sp = (unsigned long)&sp;

	if (!stack_is_erased())
		return;

	/*
	 * Overflow the thread stack using deep recursion with alloca.
	 * It should hit BUG()/panic() in stackleak_check_alloca().
	 */
	pr_info("try to overflow the thread stack using recursion & alloca\n");
	recursion(sp, 1);
	pr_err("FAIL: stack depth overflow is not detected\n");
}
