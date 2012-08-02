/* Copyright (c) 2012 Rebecca (bx) Shapiro

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#include <libelfsh.h>
#include "elf_bf_ops.h"
#include "reloc.h"
#include "symtab.h"

eresi_Addr elfops_move_ptr(elf_bf_exec_t *e, eresi_Addr delta);
eresi_Addr elfops_add(elf_bf_exec_t *ee, eresi_Addr delta);
eresi_Addr elfops_unconditional_restart(elf_bf_exec_t *ee);
eresi_Addr elfops_prepare_branch(elf_bf_exec_t *ee);
eresi_Addr set_end_ifunc(elf_bf_exec_t *ee);
eresi_Addr set_end_value(elf_bf_exec_t *ee, eresi_Addr value);

eresi_Addr elfops_increment_ptr(elf_bf_exec_t *e)
{
  return elfops_move_ptr(e, 1);
}

eresi_Addr elfops_decrement_ptr(elf_bf_exec_t *e)
{
  return elfops_move_ptr(e, -1);
}

eresi_Addr elfops_move_ptr(elf_bf_exec_t *e, eresi_Addr delta)
{

  elf_bf_link_map_t *l = &(e->ee_lm);
  eresi_Addr start = l->lm_next_reloc;
  //change tape pointer
  set_next_reloc(l, R_X86_64_64, symtab_get_index(e->ee_ptr_tape_ptr),
		 symtab_get_value_addr(e->ee_ptr_tape_ptr), delta);
  //copy new tape value into workspace
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(e->ee_ptr_tape_ptr),
		 symtab_get_value_addr(e->ee_tape_ptr), 0);
  return l->lm_next_reloc - start;
 }

 eresi_Addr elfops_increment(elf_bf_exec_t *e)
 {
   return elfops_add(e, 1);
 }

 eresi_Addr elfops_decrement(elf_bf_exec_t *e)
 {
   return elfops_add(e,-1);
 }


 eresi_Addr elfops_add(elf_bf_exec_t *ee, eresi_Addr delta)
 {
   elf_bf_link_map_t *l = &(ee->ee_lm);

   eresi_Addr start = l->lm_next_reloc;
   //we assume that correct value is in scratch space, just do addition
   set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_tape_ptr),
		  symtab_get_value_addr(ee->ee_tape_ptr), delta);

   eresi_Addr setupi, copyi;
   elf_bf_Rela setup, copy;

   //update tape
   setupi = set_next_reloc(l, 0, 0, 0, 0);

   //copy new value back to tape
   copyi = set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_ptr_tape_copy),
			    0, 0);

   //go back at actually setup first entry
   reloc_get_reloc_entry(l, setupi, &setup);
   reloc_get_reloc_entry(l, copyi, &copy);
   //setup next entry so it knows where tape pointer is
   reloc_set_rela(&setup, R_X86_64_64, symtab_get_index(ee->ee_ptr_tape_ptr),
		  reloc_get_offset_addr(&copy), 0);
  return l->lm_next_reloc - start;
}

eresi_Addr elfops_branch_start(elf_bf_exec_t *ee)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;
  elfops_prepare_branch(ee);


  eresi_Addr firsti, sizei, landi;
  elf_bf_Rela first, next, size, land;

  //setup relocation entry that fixes dt_rela to jump past unconditional branch
  //we don't know address of next yet, so don't actually set it yet
  firsti = set_next_reloc(l, 0, 0, 0, 0);

  //setup relocation entry that fixes dt_relasz assuming unconditional branch
  sizei = set_next_reloc(l, 0, 0, 0, 0);

  //sets symbol's link addr to (value on tape)
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_ptr_tape_copy),
		 symtab_get_link_addr(ee->ee_lm.lm_ifunc), 0);

  //sets end to zero or ifunc, depending on link value of sym1 set earlier
  set_end_ifunc(ee);
  //set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_lm.lm_ifunc),
  //ee->ee_reloc_end,0);

  //unconditional branch past ] (if tape is zero)
  //update dt_rela (addend to be filled in once ] is known)
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_rela, 0);

  //update dt_relasz (addend to be filled in once ] is known)
  landi = set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz, 0);

  //set end to zero to force immediate branch
  landi += set_end_value(ee, 0);
  landi += 1;
  //fix first entry to it points to last entry
  reloc_get_reloc_entry(l, firsti, &first);
  reloc_get_reloc_entry(l, sizei, &size);
  reloc_get_reloc_entry(l, landi, &land);
  reloc_set_rela(&first, R_X86_64_RELATIVE, 0, ee->ee_dt_rela,
		 reloc_get_addr(&land));
  reloc_set_rela(&size, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz, ee->ee_relasz_orig +  ((landi)*sizeof(Elf64_Rela)));

  return l->lm_next_reloc - start;
}

/*
eresi_Addr elfops_unconditional_restart(elf_bf_exec_t *ee)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;


  elfops_prepare_branch(ee);

  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_reloc_end, 0); //force branch

  return l->lm_next_reloc - start;
  }*/

eresi_Addr init_scatch_space(elf_bf_exec_t *ee)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;

  //set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_jmprel, 0);
  //set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_pltrelsz, 0);

  //copy new tape value into workspace
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_ptr_tape_ptr),
		 symtab_get_value_addr(ee->ee_tape_ptr), 0);

  //get linkmap value
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_exec_map), symtab_get_value_addr(ee->ee_exec_map), 0);

  //get address of base of ld then stack addr (ee_stac_addr)
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_exec_map), symtab_get_value_addr(ee->ee_stack_addr), get_l_next(0)); //calculate l_next address location
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0); //calculate l_next address location

  //get address of base of ld then stack addr (ee_stac_addr)
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_exec_map), symtab_get_value_addr(ee->ee_stack_addr), get_l_next(0)); //calculate l_next address location
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0); //calculate l_next address location

  //get address of base of ld then stack addr (ee_stac_addr)
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), get_l_next(0)); //calculate l_next  address location
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0);

  //get address of base of ld then stack addr (ee_stac_addr)
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), get_l_next(0)); //calculate l_next address location
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0);


  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0);



  // save addess of ROP code that returns 0, ldbase + 0x148DE, (at addr 5555555688de)
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_lm.lm_ifunc), ee->ee_ifunc_offset); //calculate ifunc addr
  //calculate address of _dl_auxv  (0x555555771e28- 0x555555554000)=0x21DE28
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), ee->ee_dl_auxv); //calculate address of _dl_auxv that is a pointer to the aux vector that lives on stack
  //save addres of auxv
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), 0);

  // calculate &end on stack with respect to auxv
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), symtab_get_value_addr(ee->ee_stack_addr), ee->ee_end_offset); //calculate l_next address location

  return l->lm_next_reloc - start;
}

eresi_Addr set_end_ifunc(elf_bf_exec_t *ee)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;
  eresi_Addr getendi, setendi;
  elf_bf_Rela getend, setend;
  getendi = set_next_reloc(l, 0, 0, 0, 0);
  setendi = set_next_reloc(l, 0, 0, 0, 0);
  reloc_get_reloc_entry(l, getendi, &getend);
  reloc_get_reloc_entry(l, setendi, &setend);

  reloc_set_rela(&getend, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), reloc_get_offset_addr(&setend), 0);
  reloc_set_rela(&setend, R_X86_64_64,  symtab_get_index(ee->ee_lm.lm_ifunc), 0, 0);
  return l->lm_next_reloc - start;
}
eresi_Addr set_end_value(elf_bf_exec_t *ee, eresi_Addr value)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;
  eresi_Addr getendi, setendi;
  elf_bf_Rela getend, setend;
  getendi = set_next_reloc(l, 0, 0, 0, 0);
  setendi = set_next_reloc(l, 0, 0, 0, 0);
  reloc_get_reloc_entry(l, getendi, &getend);
  reloc_get_reloc_entry(l, setendi, &setend);

  reloc_set_rela(&getend, R_X86_64_64, symtab_get_index(ee->ee_stack_addr), reloc_get_offset_addr(&setend), 0);
  reloc_set_rela(&setend, R_X86_64_RELATIVE,  0, 0, value);
  return l->lm_next_reloc - start;
}

eresi_Addr elfops_branch_end(elf_bf_exec_t *ee)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;
  //setup relocation entry that fixes dt_rela
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_rela,0);

  //setup relocation entry that fixes dt_relasz
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz,0);

  elfops_prepare_branch(ee);

  //sets symbol's indx to (value on tape)
  //sets symbol's link addr to (value on tape)
  set_next_reloc(l, R_X86_64_COPY, symtab_get_index(ee->ee_ptr_tape_copy),
		 symtab_get_link_addr(ee->ee_lm.lm_ifunc), 0);

  //sets end to zero or ifunc, depending on link value of sym1 set earlier
  //address of end is in a symbol so we need to copy it to the next rel entry's offset
  set_end_ifunc(ee);

  //sets end to original value

  set_end_value(ee, ee->ee_reloc_end_value);


  return l->lm_next_reloc - start;
}



//set exec linkmap at offset <offset> to value <value>
eresi_Addr update_exec_linkmap(elf_bf_exec_t *ee, eresi_Addr offset, eresi_Addr value)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;

  //calculate address of item for linkmap to update in scratchspace
  elf_bf_Rela calculate, update;
  eresi_Addr calculatei, updatei;

  //don't know all the information we need to set in this yet
  calculatei = set_next_reloc(l, 0, 0, 0, 0);
  updatei = set_next_reloc(l, 0, 0, 0, 0);
  reloc_get_reloc_entry(l, calculatei, &calculate);
  reloc_get_reloc_entry(l, updatei, &update);
  //l->lm_next_reloc+=2;
  //save value of offset+link_map addr in next relocation entry
  reloc_set_rela(&calculate, R_X86_64_64, symtab_get_index(ee->ee_exec_map), reloc_get_offset_addr(&update), offset);
  reloc_set_rela(&update, R_X86_64_RELATIVE, 0, 0, value);//offset filled in at runtime

  return l->lm_next_reloc - start;
}


//set exec linkmap at offset <offset> to value (&linkmap + <value>)
eresi_Addr update_exec_linkmap_offset(elf_bf_exec_t *ee, eresi_Addr offset, eresi_Addr value)
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;

  //calculate address of item for linkmap to update in scratchspace
  elf_bf_Rela calculate, update;
  eresi_Addr calculatei, updatei;

  //don't know all the information we need to set in this yet
  calculatei = set_next_reloc(l, 0, 0, 0, 0);
  updatei = set_next_reloc(l, 0, 0, 0, 0);
  reloc_get_reloc_entry(l, calculatei, &calculate);
  reloc_get_reloc_entry(l, updatei, &update);
  //l->lm_next_reloc++;
  //save value of offset+link_map addr in next relocation entry
  reloc_set_rela(&calculate, R_X86_64_64, symtab_get_index(ee->ee_exec_map), reloc_get_offset_addr(&update), offset);
  reloc_set_rela(&update, R_X86_64_64, symtab_get_index(ee->ee_exec_map), 0, value);//offset filled in at runtime


  return l->lm_next_reloc - start;
}

eresi_Addr elfops_prepare_branch(elf_bf_exec_t *ee) //10
{
  elf_bf_link_map_t *l = &(ee->ee_lm);
  eresi_Addr start = l->lm_next_reloc;


  update_exec_linkmap(ee, get_l_buckets(0), 0); //let l_buckets to NULL


  // set l_direct_opencount to 0
  update_exec_linkmap(ee, get_l_direct_opencount(0), 0);

  update_exec_linkmap_offset(ee, get_l_libname_next(0), get_l_relocated(0) - 4*sizeof(int));
    //set_next_reloc(l, R_X86_64_RELATIVE, 0, get_l_libname_next(l->lm_l_s), get_l_relocated(l->lm_l_s)-4*sizeof(int)); // rewire l_libname->next so relocated bit gets cleared
  //set_next_reloc(l, R_X86_64_RELATIVE, 0, get_l_prev(l->lm_l_s), l->lm_l_s); // prev points to self so it gets processed again
  update_exec_linkmap_offset(ee, get_l_prev(0), 0);

  //set own relrosize as 0 so memory protections arent set
  update_exec_linkmap(ee, get_l_relro_size(0), 0);
  //wipe pltgot
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_pltgot, 0);

  return l->lm_next_reloc - start;
}

eresi_Addr elfops_exit(elf_bf_exec_t *ee)
{
  eresi_Addr start;
  elf_bf_link_map_t *l = &(ee->ee_lm);
  start = l->lm_next_reloc;
  //this will end up causing the loader to divide by zero
  //if (l->lm_allocated) {
  //  reloc_set_rela(&r0, R_X86_64_TLSDESC, 0, 0, 0);
  //  l->lm_next_reloc = index;
  //}
  // force a branch to run original relocation entrires

  // restore PLT stuff
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_jmprel, ee->ee_dt_jmprel_value);
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_pltrelsz, ee->ee_dt_pltrelsz_value);

  elfops_prepare_branch(ee);


  //restore  RELA to point to entry after branch
  eresi_Addr relai, bucketi, relaszi, last;
  elf_bf_Rela bucket, relasz, rela;

  //don't know all the information we need to set in this yet
  relai = set_next_reloc(l, 0, 0, 0, 0);

  //fix RELACOUNT accordingly
  // should probably auto calculate the 8
  //set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz,
  //ee->ee_relasz_orig + 8*sizeof(Elf64_Rela));

  relaszi = set_next_reloc(l, 0, 0, 0, 0); //placeholder for setting relasz before unconditional branch

  //force immediate branch so that we finally process PLT entries
  //address of end is in symbol...
  set_end_value(ee, 0);

  // restore l_buckets
  bucketi = relaszi + 3;
  update_exec_linkmap(ee, get_l_buckets(0), ee->ee_dt_gnu_hash);

  // now go back at steup relai
  reloc_get_reloc_entry(l, relai, &rela);
  reloc_get_reloc_entry(l, relaszi, &relasz);
  reloc_get_reloc_entry(l, bucketi, &bucket); //bucket is the first thing that processes after unconditional jump
  reloc_set_rela(&rela, R_X86_64_RELATIVE, 0, ee->ee_dt_rela,
		 reloc_get_addr(&bucket)); //now we know where bucket is set DT_RELA

  //restore l->libname->next
  update_exec_linkmap(ee, get_l_libname_next(0), 0);

  //restore l_direct_opencount
  update_exec_linkmap(ee, get_l_direct_opencount(0), 1);

  //restore l_prev
  update_exec_linkmap(ee, get_l_prev(0), 0);

  //restore symtable to original value
  //printf("symorig %x\n", ee->ee_sym_orig);
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_sym, ee->ee_sym_orig);

  //restore  RELA to point to original dynrel
  set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_rela,ee->ee_rela_orig);

  //restore PLTGOT
  set_next_reloc(l, R_X86_64_64, symtab_get_index(ee->ee_exec_map),ee->ee_dt_pltgot, 0);


  //restore RELACOUNT or whatever to original value
  last = set_next_reloc(l, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz, ee->ee_relasz_orig);


  //now we know how many rela entries there are on last pass
  reloc_set_rela(&relasz, R_X86_64_RELATIVE, 0, ee->ee_dt_relasz,
		 ee->ee_relasz_orig + sizeof(Elf64_Rela) * (last - (bucketi-1)));


  return l->lm_next_reloc - start;
}
