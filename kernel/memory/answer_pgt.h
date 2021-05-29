void enable_paging() {
    // TODO: Homework 2: Enable paging
    // After initializing the page table, write to register SATP register for kernel registers.
    // Flush the TLB to invalidate the existing TLB Entries
    // Suggested: 2 LoCs
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

// Return the address of the PTE in page table *pagetable*
// The Risc-v Sv48 scheme has four levels of page table.
// For VA:
//   47...63 zero
//   39...47 -- 9  bits of level-3 index
//   30...38 -- 9  bits of level-2 index
//   21...29 -- 9  bits of level-1 index
//   12...20 -- 9  bits of level-0 index
//   0...11  -- 12 bits of byte offset within the page
// Return the last-level page table entry.
static pte_t* pt_query(pagetable_t pagetable, vaddr_t va, int alloc){
    if(va >= MAXVA) BUG_FMT("get va[0x%lx] >= MAXVA[0x%lx]", va, MAXVA);
    // Suggested: 18 LoCs
    for(int level = 3; level > 0; --level){
        pte_t *pte;
        pte =&pagetable[PX(level,va)];
        if(*pte & PTE_V){
  //        DEBUG("pt_query,%lx\n",*pte);
          pagetable = (pagetable_t) PTE2PA(*pte);
        } else {
            if(!alloc) return 0;
            pagetable = (pte_t*)mm_kalloc();
            if(pagetable==0) return 0;
            memset(pagetable,0,PGSIZE);
            *pte= PA2PTE(pagetable)|PTE_V;
        }
    }
//    DEBUG("va[0x%lx],pa[0x%lx]\n",va,pagetable[PX(0,va)]);
    return /* Return value here */ &pagetable[PX(0,va)];
}
int pt_map_pages(pagetable_t pagetable, vaddr_t va, paddr_t pa, uint64 size, int perm) {
    // Suggested: 11 LoCs
    pte_t *pte;
    vaddr_t a = PGROUNDDOWN(va);
    vaddr_t last = PGROUNDDOWN(va + size - 1);
 //   DEBUG("%lx,%lx\n",a,last);
    while (1) {
        pte = pt_query(pagetable, a, 1);
        if (pte == 0)
            return -1;
        if (*pte & PTE_V)
            BUG_FMT("remap error");
        *pte = PA2PTE(pa) | perm | PTE_V;
//        DEBUG("%lx\n",*pte);
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0; // Do not modify
}

paddr_t pt_query_address(pagetable_t pagetable, vaddr_t va){
    // Suggested: 3 LoCs
    pte_t *pte;
    pte = pt_query(pagetable,va,0);
    if(pte == 0 || ((*pte & PTE_V) == 0) /*|| ((*pte & PTE_U)==0)*/) {
   //     DEBUG("query_addr_fault\n" );
     //   if((*pte&PTE_V)==0) DEBUG(" %d notvalid", *pte);
        return 0;
    }
    paddr_t  offset = (va&(0xFFF));
//    DEBUG("query_addr %d,  %d,  %lx.\n",va, (PTE2PA(*pte)|offset),(1<<PGSHIFT)-1);
    return /* Return value here */ PTE2PA(*pte)|offset;
}

int pt_unmap_addrs(pagetable_t pagetable, vaddr_t va){
    // Suggested: 2 LoCs
    pte_t *pte;
    pte= pt_query(pagetable,va,0);
    if(pte ==0)
        BUG_FMT("unmap error");
    *pte &= ~PTE_V;
    return 0; // Do not modify
}

int pt_map_addrs(pagetable_t pagetable, vaddr_t va, paddr_t pa, int perm){
    // Suggested: 2 LoCs
    vaddr_t a = PGROUNDDOWN(va);
    pte_t *pte;
    pte = pt_query(pagetable, a, 1);
    if (pte == 0) return -1;
    if (*pte & PTE_V)
            BUG_FMT("remap error");
    *pte = PA2PTE(pa) | perm | PTE_V;
    return 0; // Do not modify
}
