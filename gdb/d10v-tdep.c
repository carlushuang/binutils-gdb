/* Target-dependent code for MItsubishi D10V, for GDB.
   Copyright (C) 1996 Free Software Foundation, Inc.
This file is part of GDB.
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*  Contributed by Martin Hunt, hunt@cygnus.com */

#include "defs.h"
#include "frame.h"
#include "obstack.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "gdb_string.h"
#include "value.h"
#include "inferior.h"
#include "dis-asm.h"  

void d10v_frame_find_saved_regs PARAMS ((struct frame_info *fi, struct frame_saved_regs *fsr));

/* Discard from the stack the innermost frame,
   restoring all saved registers.  */

void
d10v_pop_frame ()
{
  struct frame_info *frame = get_current_frame ();
  CORE_ADDR fp;
  int regnum;
  struct frame_saved_regs fsr;
  char raw_buffer[8];

  fp = FRAME_FP (frame);

  /* fill out fsr with the address of where each */
  /* register was stored in the frame */
  get_frame_saved_regs (frame, &fsr);
  
  /* now update the current registers with the old values */
  for (regnum = A0_REGNUM; regnum < A0_REGNUM+2 ; regnum++)
    {
      if (fsr.regs[regnum])
	{
	  read_memory (fsr.regs[regnum], raw_buffer, 8);
	  write_register_bytes (REGISTER_BYTE (regnum), raw_buffer, 8);
	}
    }
  for (regnum = 0; regnum < SP_REGNUM; regnum++)
    {
      if (fsr.regs[regnum])
	{
	  write_register (regnum, read_memory_unsigned_integer (fsr.regs[regnum], 2));
	}
    }
  if (fsr.regs[PSW_REGNUM])
    {
      write_register (PSW_REGNUM, read_memory_unsigned_integer (fsr.regs[PSW_REGNUM], 2));
    }

  write_register (PC_REGNUM, read_register(13));
  write_register (SP_REGNUM, fp + frame->size);
  target_store_registers (-1);
  flush_cached_frames ();
}

static int 
check_prologue (op)
     unsigned short op;
{
  /* st  rn, @-sp */
  if ((op & 0x7E1F) == 0x6C1F)
    return 1;

  /* st2w  rn, @-sp */
  if ((op & 0x7E3F) == 0x6E1F)
    return 1;

  /* subi  sp, n */
  if ((op & 0x7FE1) == 0x01E1)
    return 1;

  /* mv  r11, sp */
  if (op == 0x417E)
    return 1;

  /* nop */
  if (op == 0x5E00)
    return 1;

  /* st  rn, @sp */
  if ((op & 0x7E1F) == 0x681E)
    return 1;

  /* st2w  rn, @sp */
 if ((op & 0x7E3F) == 0x3A1E)
   return 1;

  return 0;
}

CORE_ADDR
d10v_skip_prologue (pc)
     CORE_ADDR pc;
{
  unsigned long op;
  unsigned short op1, op2;

  if (target_read_memory (pc, (char *)&op, 4))
    return pc;			/* Can't access it -- assume no prologue. */

  while (1)
    {
      op = (unsigned long)read_memory_integer (pc, 4);
      if ((op & 0xC0000000) == 0xC0000000)
	{
	  /* long instruction */
	  if ( ((op & 0x3FFF0000) != 0x01FF0000) &&   /* add3 sp,sp,n */
	       ((op & 0x3F0F0000) != 0x340F0000) &&   /* st  rn, @(offset,sp) */
 	       ((op & 0x3F1F0000) != 0x350F0000))     /* st2w  rn, @(offset,sp) */
	    break;
	}
      else
	{
	  /* short instructions */
	  op1 = (op & 0x3FFF8000) >> 15;
	  op2 = op & 0x7FFF;
	  if (!check_prologue(op1) || !check_prologue(op2))
	    break;
	}
      pc += 4;
    }
  return pc;
}
 
/* Given a GDB frame, determine the address of the calling function's frame.
   This will be used to create a new GDB frame struct, and then
   INIT_EXTRA_FRAME_INFO and INIT_FRAME_PC will be called for the new frame.
*/

CORE_ADDR
d10v_frame_chain (frame)
     struct frame_info *frame;
{
  struct frame_saved_regs fsr;
  d10v_frame_find_saved_regs (frame, &fsr);
  return read_memory_unsigned_integer(fsr.regs[FP_REGNUM],2);
}  

static int next_addr;

static int 
prologue_find_regs (op, fsr, addr)
     unsigned short op;
     struct frame_saved_regs *fsr;
     CORE_ADDR addr;
{
  int n;

  /* st  rn, @-sp */
  if ((op & 0x7E1F) == 0x6C1F)
    {
      n = (op & 0x1E0) >> 5;
      next_addr -= 2;
      fsr->regs[n] = next_addr;
      return 1;
    }

  /* st2w  rn, @-sp */
  else if ((op & 0x7E3F) == 0x6E1F)
    {
      n = (op & 0x1E0) >> 5;
      next_addr -= 4;
      fsr->regs[n] = next_addr;
      fsr->regs[n+1] = next_addr+2;
      return 1;
    }

  /* subi  sp, n */
  if ((op & 0x7FE1) == 0x01E1)
    {
      n = (op & 0x1E) >> 1;
      if (n == 0)
	n = 16;
      next_addr -= n;
      return 1;
    }

  /* mv  r11, sp */
  if (op == 0x417E)
    return 1;

  /* nop */
  if (op == 0x5E00)
    return 1;

  /* st  rn, @sp */
  if ((op & 0x7E1F) == 0x681E)
    {
      n = (op & 0x1E0) >> 5;
      fsr->regs[n] = next_addr;
      return 1;
    }

  /* st2w  rn, @sp */
  if ((op & 0x7E3F) == 0x3A1E)
    {
      n = (op & 0x1E0) >> 5;
      fsr->regs[n] = next_addr;
      fsr->regs[n+1] = next_addr+2;
      return 1;
    }

  return 0;
}

/* Put here the code to store, into a struct frame_saved_regs, the
   addresses of the saved registers of frame described by FRAME_INFO.
   This includes special registers such as pc and fp saved in special
   ways in the stack frame.  sp is even more special: the address we
   return for it IS the sp for the next frame. */
void
d10v_frame_find_saved_regs (fi, fsr)
     struct frame_info *fi;
     struct frame_saved_regs *fsr;
{
  CORE_ADDR fp, pc;
  unsigned long op;
  unsigned short op1, op2;
  int i;

  fp = fi->frame;
  memset (fsr, 0, sizeof (*fsr));
  next_addr = 0;

  pc = get_pc_function_start (fi->pc);

  while (1)
    {
      op = (unsigned long)read_memory_integer (pc, 4);
      if ((op & 0xC0000000) == 0xC0000000)
	{
	  /* long instruction */
	  if ((op & 0x3FFF0000) == 0x01FF0000)
	    {
	      /* add3 sp,sp,n */
	      short n = op & 0xFFFF;
	      next_addr += n;
	    }
	  else if ((op & 0x3F0F0000) == 0x340F0000)
	    {
	      /* st  rn, @(offset,sp) */
	      short offset = op & 0xFFFF;
	      short n = (op >> 20) & 0xF;
	      fsr->regs[n] = next_addr + offset;
	    }
	  else if ((op & 0x3F1F0000) == 0x350F0000)
	    {
	      /* st2w  rn, @(offset,sp) */
	      short offset = op & 0xFFFF;
	      short n = (op >> 20) & 0xF;
	      fsr->regs[n] = next_addr + offset;
	      fsr->regs[n+1] = next_addr + offset + 2;
	    }
	  else
	    break;
	}
      else
	{
	  /* short instructions */
	  op1 = (op & 0x3FFF8000) >> 15;
	  op2 = op & 0x7FFF;
	  if (!prologue_find_regs(op1,fsr,pc) || !prologue_find_regs(op2,fsr,pc))
	    break;
	}
      pc += 4;
    }
  
  fi->size = -next_addr;

  for (i=0; i<NUM_REGS; i++)
    if (fsr->regs[i])
      {
	fsr->regs[i] = fp - (next_addr - fsr->regs[i]); 
      }

  if (fsr->regs[13])
    fi->return_pc = (read_memory_unsigned_integer(fsr->regs[13],2)-1) << 2;
  else
    fi->return_pc = (read_register(13) - 1)  << 2;

  if (!fsr->regs[SP_REGNUM])
    fsr->regs[SP_REGNUM] = read_register(FP_REGNUM) + fi->size;
}

void
d10v_init_extra_frame_info (fromleaf, fi)
     int fromleaf;
     struct frame_info *fi;
{
  struct frame_saved_regs dummy;

  if (fi->next && (fi->pc == 0))
    fi->pc = fi->next->return_pc; 

  d10v_frame_find_saved_regs (fi, &dummy); 
}

static void
show_regs (args, from_tty)
     char *args;
     int from_tty;
{
  long long num1, num2;
  printf_filtered ("PC=%04x (0x%x) PSW=%04x RPT_S=%04x RPT_E=%04x RPT_C=%04x\n",
                   read_register (PC_REGNUM), read_register (PC_REGNUM) << 2,
                   read_register (PSW_REGNUM),
                   read_register (24),
                   read_register (25),
                   read_register (23));
  printf_filtered ("R0-R7  %04x %04x %04x %04x %04x %04x %04x %04x\n",
                   read_register (0),
                   read_register (1),
                   read_register (2),
                   read_register (3),
                   read_register (4),
                   read_register (5),
                   read_register (6),
                   read_register (7));
  printf_filtered ("R8-R15 %04x %04x %04x %04x %04x %04x %04x %04x\n",
                   read_register (8), 
                   read_register (9),
                   read_register (10),
                   read_register (11),
                   read_register (12),
                   read_register (13),
                   read_register (14),
                   read_register (15));
  read_register_gen (A0_REGNUM, (char *)&num1);
  read_register_gen (A0_REGNUM+1, (char *)&num2);
  printf_filtered ("A0-A1  %010llx %010llx\n",num1, num2);
}

void
_initialize_d10v_tdep ()
{
  tm_print_insn = print_insn_d10v;
  add_com ("regs", class_vars, show_regs, "Print all registers");
} 

CORE_ADDR
d10v_read_register_pid (regno, pid)
     int regno, pid;
{
  int save_pid;
  CORE_ADDR retval;

  if (pid == inferior_pid)
    return (read_register(regno)) << 2;

  save_pid = inferior_pid;
  inferior_pid = pid;
  retval = read_register (regno);
  inferior_pid = save_pid;
  return (retval << 2);
}

void
d10v_write_register_pid (regno, val, pid)
     int regno;
     LONGEST val;
     int pid;
{
  int save_pid;

  val >>= 2;

  if (pid == inferior_pid)
    {
      write_register (regno, val);
      return;
    }

  save_pid = inferior_pid;
  inferior_pid = pid;
  write_register (regno, val);
  inferior_pid = save_pid;
}
