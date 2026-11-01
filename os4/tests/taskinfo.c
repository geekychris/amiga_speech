// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// taskinfo [name...]: list tasks (name, state, priority, wait mask, received
// signals), or only those whose name contains one of the arguments.
#include <exec/exec.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

static void show(struct Task *t, const char *state, int argc, char **argv)
{
    const char *n = t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?";
    int i, ok = argc < 2;
    for (i = 1; i < argc; i++) if (strstr(n, argv[i])) ok = 1;
    if (!ok) return;
    printf("%-28s %-6s pri %4d wait %08lx recvd %08lx type %d\n", n, state,
           t->tc_Node.ln_Pri, (unsigned long)t->tc_SigWait, (unsigned long)t->tc_SigRecvd,
           t->tc_Node.ln_Type);
}

int main(int argc, char **argv)
{
    struct ExecBase *eb = (struct ExecBase *)SysBase;
    struct Node *n;
    static struct Task copy[200];
    static const char *st[200];
    static char names[200][32];
    int k = 0, i;
    Forbid();
    for (n = eb->TaskWait.lh_Head; n->ln_Succ && k < 200; n = n->ln_Succ, k++) {
        copy[k] = *(struct Task *)n; st[k] = "wait";
        strncpy(names[k], n->ln_Name ? n->ln_Name : "?", 31); copy[k].tc_Node.ln_Name = names[k];
    }
    for (n = eb->TaskReady.lh_Head; n->ln_Succ && k < 200; n = n->ln_Succ, k++) {
        copy[k] = *(struct Task *)n; st[k] = "ready";
        strncpy(names[k], n->ln_Name ? n->ln_Name : "?", 31); copy[k].tc_Node.ln_Name = names[k];
    }
    Permit();
    for (i = 0; i < k; i++) show(&copy[i], st[i], argc, argv);
    return 0;
}
