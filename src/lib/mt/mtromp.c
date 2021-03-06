/*
   ============================================================================
	mtromp.c -- ROMP multi-tasking debug monitor for the Buchla 700
	(c) Copyright 1988 -- D.N. Lynx Crowe

	See ROMPVER, below, for version number and date.

	CAUTION:  This code is designed to live in PROM, so initialized
	variables are PERMANENTLY initialized, since the compiler assigns
	initialized variables to the data segment, which ends up in PROM.

	Un-initialized variables go into the bss segment, which ends up in RAM.
   ============================================================================
*/

#define	ROMPVER	"24.04 -- 1988-04-17"

#include "ascii.h"
#include "biosdefs.h"
#include "ctype.h"
#include "errdefs.h"
#include "glcfns.h"
#include "glcdefs.h"
#include "hwdefs.h"
#include "memory.h"
#include "mtdefs.h"
#include "mtfuncs.h"
#include "rawio.h"
#include "regs.h"
#include "stddefs.h"
#include "strings.h"
#include "vsdd.h"
#include "graphdef.h"
#include "vsddsw.h"
#include "vsddvars.h"
#include "debug.h"

typedef unsigned short UWORD16;	/* unsigned 16 bit word */

/* 
*/

#define	BOOTKEY		39	/* Boot panel key */
#define	ROMPKEY		40	/* ROMP panel key */

#define	BOOTFILE	"midas.abs"	/* Boot file name */

#define	BOOT_PRI	(MT_DfPri/2)	/* default boot priority */
#define	DFRUNPRI	(MT_DfPri/2)	/* default 'run' priority */

#define	KB_EI		2	/* Enable level for panel */
#define	DEFIPL		2	/* Default internal processor level */
#define	INITSR		0x2200	/* Default initial status register */

#define	LED_TIME	100	/* LED scan timer value (loops) */
#define	FIFOLIM		60000	/* FIFO clear limit */

#define	NSTAX		5	/* Number of interrupt stacks */
#define	STKLEN		256	/* Default / Int. stack size (words) */

#define	USER_RAM	0x00010000L	/* Start of user RAM  (TPA) */
#define	RAM_TOP		0x000FFFFEL	/* Default top word of memory */
#define	ISTACK		0x000FFFFEL	/* Default initial stack */

#define	ROMADDR		0x00100000L	/* Start of PROM */
#define	PRM_DATE	0x00100008L	/* PROM date address */
#define	PRM_VERS	0x00100002L	/* PROM version address */
#define	PDATELN		14	/* PROM date length */

#define	BARBASE		5120L	/* Base of bar display in VSDD RAM */
#define	SWBASE		38400L	/* Base of switch display in VSDD RAM */

#define	BAR_ON		P_WHT	/* color of bar when on */
#define	BAR_OFF		P_BRN	/* color of bar when off */
#define	SW_ON		P_DKGRN	/* color of switch when on */
#define	SW_OFF		P_RED	/* color of switch when off */

/* 
*/

#define	I_TABX		53	/* Tablet X */
#define	I_TABY		54	/* Tablet Y */
#define	I_CURX		55	/* Cursor X */
#define	I_CURY		56	/* Cursor Y */
#define	I_LONGL		57	/* LongPot Left */
#define	I_LONGR		58	/* LongPot Right */
#define	I_SCROLL	59	/* Scroll */
#define	I_TEMPO		73	/* Tempo Multiplier */
#define	I_TIME		74	/* Time Scaling */
#define	I_TUNE		75	/* Fine Tuning */
#define	I_LEVEL		76	/* Amplitude */

#define	MARGIN		4	/* Offset from left edge of screen */

#define	MAXFNLN		13	/* xxxxxxxx.xxx + CR */
#define	MAXARGLN	80	/* maximum argument length */
#define	MAXCMDLN	128	/* maximum command line length */
#define	MAXHS		80	/* maximum help string length */
#define	MAXID		90	/* maximum ID string length */

#define	BPINST		0x4E4F	/* breakpoint instruction */

#define	CRLF		"\r\n"	/* CR/LF constant */

#define	TACK		"K\r"	/* good response from load command */
#define	NACK		"?\r"	/* bad response from load command */
#define	SREC9		"04000000FB"	/* tail of a type 9 Motorola S-Record */

#define	MON_C		1	/* monitor code - character */
#define	MON_S		2	/* monitor code - short */
#define	MON_L		4	/* monitor code - long */

/* 
*/

/*
   ============================================================================
	Externals and forward references
   ============================================================================
*/

extern UWORD16 setipl ();

extern short _MTInt1 (), _MTInt2 (), _MTInt3 (), _MTInt4 (), _MTInt5 ();

extern int rjumpto (), halt (), getln (), sjumpto ();
extern int trap15 (), xtrap15 ();
extern int hdvini (), booter (), vsndpal ();

extern char *MTPrFlg ();

/* external variables */

extern TCB *MT_ITT[];		/* interrupt TCB pointer table */

extern short wzcrsh, *crshpc, *crshsp, *crshus;

extern int B_log_s, B_dbg_s, _bpbin;

extern char *B_buf_a;

extern short dfltpal[16][3], crshst[16];

extern UWORD16 crshsr;
extern long crshrg[16];
extern char crshvc[4];

extern MBOX MSG_Vid;
extern SEM SemQuit;

/* forward references */

int cp_dump (), cp_fill (), cp_copy (), cp_null (), cp_ilev (), cp_ldmp ();
int cp_go (), cp_read (), cp_mset (), cp_rset (), cp_wset (), cp_mtst ();
int cp_wdmp (), cp_wfil (), cp_vrst (), cp_monc (), cp_mons (), cp_monl ();
int cp_chek (), cx_dini (), cx_mlod (), cp_boot (), cx_boot (), cx_adsp ();
int cx_mtsn (), cp_run (), cx_run (), cx_wait (), cx_copy (), cx_rset ();
int cx_dump (), cx_fill (), cx_load (), cx_go (), cx_read (), cx_help ();
int cx_exit (), cx_writ (), cx_regs (), cx_mset (), cx_omap (), cx_chek ();
int cx_bpb (), cx_wset (), cx_wdmp (), cx_wfil (), cx_rest (), cx_mtid ();
int cx_vrst (), cx_vreg (), cx_mon (), cx_next (), cx_ilev (), cp_mtsd ();
int do_srec (), cx_crsh (), cx_mtst (), cx_zap (), cx_ldmp (), cx_mtsd ();
int cx_mtcb (), cp_mstp (), cx_mstp (), cx_mzap (), cp_mpri (), cx_mpri ();
int cx_mtsk (), cx_mqry (), cx_mtcs (), cx_mtrq ();

char hs_mtst[];

/* 
*/

/*
   ============================================================================
	error message string definitions
   ============================================================================
*/

#define	EMSG1	"\r\n** Command error **\r\n"
#define	EMSG2	"\r\n** Invalid parameter **\r\n"
#define	EMSG3	"\r\n** Unrecognized command **\r\n"

#define	EMSG5	"\r\n** Command not repeatable **\r\n"
#define EMSG6	"\r\n** Invalid line terminator **\r\n"
#define EMSG7	"\r\n** do_cmd() switch failed **\r\n"

#define	CANNED	"\r\n----- Cancelled -----\r\n"

/* 
*/

/*
   ============================================================================
	initialized variables  (These end up in the data segment in PROM)
   ============================================================================
*/

struct cmdent
{				/* command table entry */

  char *cname;			/* command name pointer */
  int (*cp) ();			/* command parser function pointer */
  int (*cx) ();			/* command execution function pointer */
  char *hstr;			/* help string pointer */
};

struct cmdent cmtab[] = {

  {"adisp", cp_null, cx_adsp, ""},
  {"boot", cp_boot, cx_boot, ""},
  {"bpb", cp_null, cx_bpb, ""},
  {"check", cp_chek, cx_chek, "start,end"},
  {"copy", cp_copy, cx_copy, "from,to,len"},
  {"crash", cp_null, cx_crsh, ""},
  {"dinit", cp_null, cx_dini, ""},
  {"dump", cp_dump, cx_dump, "from[,[to=from][,width=16]]"},
  {"fill", cp_fill, cx_fill, "start,count,byte"},
  {"go", cp_go, cx_go, "[addr][,[brk1][,brk2]]"},
  {"help", cp_null, cx_help, ""},
  {"ipl", cp_ilev, cx_ilev, "level"},
  {"ldump", cp_ldmp, cx_ldmp, "from[,[to=from][,width=4]]"},
  {"load", cp_null, cx_load, ""},
  {"midas", cp_null, cx_mlod, ""},
  {"monc", cp_monc, cx_mon, "addr"},
  {"mons", cp_mons, cx_mon, "addr"},
  {"monl", cp_monl, cx_mon, "addr"},
  {"mset", cp_mset, cx_mset, "addr,b1[,...,bn]"},
  {"mtidump", cp_null, cx_mtid, ""},
  {"mtsetp", cp_mpri, cx_mpri, "priority"},
  {"mtsnap", cp_null, cx_mtsn, ""},
  {"mtstat", cp_mstp, cx_mqry, "taskid"},
  {"mtstop", cp_mstp, cx_mstp, "taskid"},
  {"mtzap", cp_mstp, cx_mzap, "taskid"},
  {"next", cp_null, cx_next, ""},
  {"obmap", cp_null, cx_omap, ""},
  {"ramtest", cp_mtst, cx_mtst, hs_mtst},
  {"read", cp_read, cx_read, "sector,buffer,count"},
  {"readyq", cp_null, cx_mtrq, ""},
  {"regs", cp_null, cx_regs, ""},
  {"reset", cp_null, cx_rest, ""},
  {"rset", cp_rset, cx_rset, "register,value"},
  {"run", cp_run, cx_run, "addr[,pri]"},
  {"sem", cp_mtsd, cx_mtsd, "addr"},
  {"tasks", cp_null, cx_mtsk, "",},
  {"tcb", cp_mtsd, cx_mtcb, "addr"},
  {"tcbs", cp_null, cx_mtcs, ""},
  {"vregs", cp_null, cx_vreg, ""},
  {"vrset", cp_vrst, cx_vrst, "register,value"},
  {"wait", cp_null, cx_wait, ""},
  {"wdump", cp_wdmp, cx_wdmp, "from[,[to=from][,width=8]]"},
  {"wfill", cp_wfil, cx_wfil, "loc,count,word"},
  {"write", cp_read, cx_writ, "sector,buffer,count"},
  {"wset", cp_wset, cx_wset, "addr,w1[,...,wn]"},
  {"zap", cp_null, cx_zap, ""}
};

#define	NCMDS	((sizeof cmtab) / (sizeof cmtab[0]))

/* 
*/

char ahex[] = "0123456789abcdefABCDEF";

char *rlist[] = {		/* register name list */

  "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
  "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
  "sr", "pc", "sp",
  (char *) 0
};

char *vrlist[] = {		/* video register name list */

  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "h0", "h1", "h2", "h3", "v0", "v1", "v2", "v3",
  (char *) 0
};

int sigadr[] = {		/* display offsets for signals */

  0, 0, 0, 0, 0, 1, 1, 1, 1, 1,	/* keys      */
  2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
  4, 4, 4, 4,
  7, 7, 7, 7, 7, 8, 8, 8, 8, 8,	/* sliders   */
  9, 9, 9, 9,
  12, 12, 12, 12, 12, 13, 13, 13, 13, 13,	/* switches  */
  14, 14, 14, 14,
  17, 17,			/* tablet    */
  20, 20,			/* cursor    */
  23, 23,			/* longpot   */
  26,				/* scrollpot */
  29, 29, 29, 29, 29, 30, 30, 30, 30, 30,	/* digits    */
  32, 32, 32,			/* x, e, m   */
  35, 35, 35, 35,		/* pots      */
  37, 37,			/* pedals    */
  39, 39, 39, 39		/* analog in */
};

/* 
*/

char *tsmsg[] = {

  /* -3 */ "stopped,  waiting on a semaphore",
  /* -2 */ "stopped,  not waiting",
  /* -1 */ "not found",
  /*  0 */ "running  (current task)",
  /*  1 */ "ready to run",
  /*  2 */ "waiting on a semaphore"
};

/* 
*/

/*
   ============================================================================
	un-initialized variables  (These end up in the bss segment in RAM)
   ============================================================================
*/

char argsep;			/* argument separator */

char *aptr,			/* argument pointer */
 *monptr,			/* monitored variable pointer */
 *d_cur,			/* dump current from */
 *d_next,			/* dump next from */
 *d_last,			/* dump next to */
 *p_end,			/* end parameter */
 *p_from,			/* from parameter */
 *p_goto,			/* goto parameter */
 *p_to,				/* to parameter */
 *sptr;				/* string scan pointer */

short argln,			/* argument length */
  b0flag,			/* breakpoint 0 flag */
  b1flag,			/* breakpoint 1 flag */
  cmdunit,			/* command unit */
  dflag,			/* dump limit flag */
  exflag,			/* exit do_cmd flag */
  first1,			/* first time flag */
  goflag,			/* pc set flag */
  ilast,			/* index of last command */
  inext,			/* command index for "next" command */
  iplev,			/* ROMP IPL level */
  monsw,			/* monitor switch */
  redo,				/* re-doable command flag */
  rnum,				/* register number */
  taskid,			/* task ID */
  taskpri,			/* task priority */
  vrnum;			/* video register number */

/* 
*/

short asig,			/* signal number */
  aval,				/* signal value */
  astat,			/* signal status */
  aflag,			/* signal activity flag */
  baseled,			/* base LED for scan */
  ledcntr;			/* LED scan counter */

short sigtab[128][2];		/* signal table */

long afi,			/* analog FIFO input */
  ftimer;			/* analog FIFO clear timer */

unsigned baron,			/* bar 'on' color */
  baroff,			/* bar 'off' color */
  swon,				/* switch 'on' color */
  swoff,			/* switch 'off' color */
 *obj0;				/* object pointer */

UWORD16 *tba0,			/* breakpoint 0 temporary */
 *tba1;				/* breakpoint 1 temporary */

UWORD16 p_bv0,			/* breakpoint 0 value */
  p_bv1;			/* breakpoint 1 value */

UWORD16 *p_ba0,			/* breakpoint 0 address */
 *p_ba1;			/* breakpoint 1 address */

long p_len,			/* length parameter */
  p_value,			/* value parameter */
  p_width,			/* width parameter */
  p_pri,			/* task priority */
  p_task;			/* task ID */

struct regs *regptr;		/* register save area pointer */

char argstr[MAXARGLN + 1],	/* argument string */
  cmdline[MAXCMDLN + 1],	/* command line */
  bfname[MAXFNLN + 1],		/* boot file name */
  hs_mtst[MAXHS + 1],		/* mtest help string */
  idbuf[MAXID + 1],		/* ID string */
  promdate[PDATELN + 1];	/* PROM date area */

unsigned runstak[STKLEN],	/* initial 'run' stack */
  stax[NSTAX][STKLEN];		/* stacks for interrupt tasks */

struct _mt_def *_MT_;		/* pointer to MTStruct */

TCB runtcb,			/* 'run' TCB */
  ipltcb;			/* MIDAS boot TCB */

/* 
*/

/*
   ============================================================================
	cx_rest -- execute the reset command
   ============================================================================
*/

int
cx_rest ()
{
  setipl (7);
  rjumpto (ROMADDR);
}

/*
   ============================================================================
	cx_mtsn -- execute the mtsnap command
   ============================================================================
*/

int
cx_mtsn ()
{
  MTSnap ();
  return (TRUE);
}

/*
   ============================================================================
	cx_mtid -- execute the mtidump command
   ============================================================================
*/

int
cx_mtid ()
{
  MTIDump ();
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mlod() -- execute the midas command
   ============================================================================
*/

int
cx_mlod ()
{
  register short i;

  B_log_s = TRUE;
  B_dbg_s = FALSE;
  redo = FALSE;

  hdvini ();
  _bpbin = FALSE;

  if (booter (BOOTFILE, 0L))
    {

      return (FALSE);

    }
  else
    {

      for (i = 0; i < 8; i++)	/* clear d0..d7 */
	regptr->d_reg[i] = 0L;

      for (i = 0; i < 7; i++)	/* clear a0..a6 */
	regptr->a_reg[i] = (char *) 0L;

      regptr->a_reg[7] = ISTACK;	/* setup initial stack */
      regptr->reg_sr = INITSR;	/* setup sr */
      regptr->reg_pc = B_buf_a;	/* setup pc */

      return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	cp_boot() -- parse boot command
   ============================================================================
*/

int
cp_boot ()
{
  register int i;
  register char endc;

  redo = FALSE;

  for (;;)
    {

      writeln (cmdunit, "File name: ");
      endc = getln (cmdunit, MAXFNLN + 1, bfname);
      writeln (cmdunit, CRLF);

      if (endc == A_CR)
	break;

      if (endc == CTL ('X'))
	{

	  writeln (cmdunit, CANNED);
	  return (FALSE);
	}

      if (endc == ERR01)
	writeln (cmdunit, EMSG2);
    }

  for (i = 0; i < MAXFNLN + 1; i++)
    if (bfname[i] == A_CR)
      bfname[i] = '\0';

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_boot() -- execute boot command
   ============================================================================
*/

int
cx_boot ()
{
  register short i;

  B_log_s = TRUE;
  B_dbg_s = FALSE;

  hdvini ();
  _bpbin = FALSE;

  if (booter (bfname, 0L))
    {

      return (FALSE);

    }
  else
    {

      for (i = 0; i < 8; i++)	/* clear d0..d7 */
	regptr->d_reg[i] = 0L;

      for (i = 0; i < 7; i++)	/* clear a0..a6 */
	regptr->a_reg[i] = (char *) 0L;

      regptr->a_reg[7] = ISTACK;	/* setup initial stack */

      regptr->reg_sr = INITSR;	/* setup sr */
      regptr->reg_pc = B_buf_a;	/* setup pc */

      return (TRUE);
    }
}

/* 
*/

/*
   =============================================================================
	dobar(nb, bv) -- draw bar 'nb' with value 'bv'
   =============================================================================
*/

dobar (nb, bv)
     register int nb, bv;
{
  register unsigned *bp;
  register int i;

  if ((nb < 1) || (nb > 82))
    return;

  --nb;
  bp = obj0 + BARBASE + (long) (sigadr[nb] + MARGIN + nb);

  for (i = 127; i >= 0; --i)
    {

      if (i > bv)
	{

	  *bp = baroff;
	  bp += 128L;
	  *bp = baroff;

	}
      else
	{

	  *bp = baron;
	  bp += 128L;
	  *bp = baron;
	}

      bp += 128L;
    }
}

/* 
*/

/*
   =============================================================================
	dosw(nb, sv) -- display value 'sv' of switch 'nb'
   =============================================================================
*/

dosw (nb, sv)
     register int nb, sv;
{
  register unsigned *bp;
  register int i, j;

  if ((nb < 1) || (nb > 82))
    return;

  --nb;
  bp = obj0 + SWBASE + (long) (sigadr[nb] + MARGIN + nb);

  if (sv)
    j = swon;
  else
    j = swoff;

  for (i = 0; i < 8; i++)
    {

      *bp = j;
      bp += 128L;
    }
}

/* 
*/

/*
   =============================================================================
	exp_c() -- expand a 4 bit color into a 16 bit word
   =============================================================================
*/

unsigned
exp_c (c)
     unsigned c;
{
  c &= 0x000F;
  c |= c << 4;
  c |= c << 8;

  return (c);
}

/* 
*/

/*
   =============================================================================
	cx_adsp() -- display values of analog processor variables as a bar graph
   =============================================================================
*/

cx_adsp ()
{
  register int xasig, xastat, xaval;
  register long xafi;
  register long lc;
  register unsigned *bp;
  int i, j, k;
  int oldi;

  memsetw (sigtab, 0, sizeof sigtab / 2);

  VHinit ();
  VSinit ();
  vsndpal (dfltpal);

  obj0 = 0x200400L;

  SetObj (0, 0, 0, obj0, 512, 350, 0, 0, (V_RES3 | V_TDE), -1);

  bp = obj0;

  for (lc = 0; lc < 44800; lc++)
    *bp++ = 0x0000;

  baron = 0x0FFF & exp_c (BAR_ON);
  baroff = 0x0FFF & exp_c (BAR_OFF);
  swon = 0x0FFF & exp_c (SW_ON);
  swoff = 0x0FFF & exp_c (SW_OFF);

  SetPri (0, 7);

  for (i = 1; i < 83; i++)
    {

      dobar (i, 0);
      dosw (i, 0);
    }

  oldi = setipl (2);

/* 
*/

  while (0L == BIOS (B_RDAV, CON_DEV))
    {

      if (-1L != (xafi = XBIOS (X_ANALOG)))
	{

	  xasig = 0x007F & (xafi >> 8);
	  xastat = 0x0001 & (xafi >> 7);
	  xaval = 0x007F & xafi;

	  if (xasig)
	    {

	      sigtab[xasig][0] = xaval;
	      sigtab[xasig][1] = xastat;

	      if (xasig < 83)
		{

		  dobar (xasig, xaval);
		  dosw (xasig, xastat);
		}

	    }
	  else
	    {

	      for (i = 0; i < 83; i++)
		sigtab[i][1] = 0;
	    }
	}
    }

/* 
*/

  BIOS (B_GETC, CON_DEV);

  for (j = 1; j < 83; j++)
    {

      dobar (j, sigtab[j][0]);
      dosw (j, sigtab[j][1]);
    }

  k = 0;

  printf ("\n");
  printf
    ("       x0     x1     x2     x3     x4     x5     x6     x7     x8     x9\r\n");
  printf
    ("     -----  -----  -----  -----  -----  -----  -----  -----  -----  -----\r\n");

  for (i = 0; i < 9; i++)
    {

      printf ("%01dx   ", k / 10);

      for (j = 0; j < 10; j++)
	{

	  if (k)
	    printf ("%1d:%3d  ", sigtab[k][1], sigtab[k][0]);
	  else
	    printf ("       ");

	  if (++k == 83)
	    goto outofit;
	}

      printf ("\n");
    }

outofit:

  printf
    ("\nTempo     = %3d,  Time      = %3d,  Tuning = %3d,  Level = %3d\n",
     sigtab[I_TEMPO][0], sigtab[I_TIME][0], sigtab[I_TUNE][0],
     sigtab[I_LEVEL][0]);

  printf ("LongPot L = %3d,  LongPot R = %3d,  Scroll = %3d\n",
	  sigtab[I_LONGL][0], sigtab[I_LONGR][0], sigtab[I_SCROLL][0]);

  printf ("Tablet X  = %3d,  Tablet Y  = %3d\n",
	  sigtab[I_TABX][0], sigtab[I_TABY][0]);

  printf ("Cursor X  = %3d,  Cursor Y  = %3d\n",
	  sigtab[I_CURX][0], sigtab[I_CURY][0]);

  setipl (oldi);
  return (TRUE);
}

/* 
*/

/*
   =============================================================================
	waitcr() -- wait for a CR from CON_DEV
		Returns:	0 = continue
				1 = ^G hit - abort
   =============================================================================
*/

int
waitcr ()
{
  char c;

  BIOS (B_PUTC, CON_DEV, '\007');

  while ('\r' != (c = (0x7F & BIOS (B_GETC, CON_DEV))))
    {

      if (c == '\007')
	return (1);
    }

  return (0);
}

/* 
*/

/*
   ============================================================================
	xdtoi -- convert hex ASCII to an int digit
   ============================================================================
*/

int
xdtoi (c)
     register int c;
{
  register int i;
  register char *ap = &ahex[0];

  for (i = 0; i < 22; i++)
    if (c == * ap++)
      if (i > 15)
	return (i - 6);
      else
	return (i);

  return (-1);
}

/* 
*/

/*
   ============================================================================
	getcmd -- parse command from input line
   ============================================================================
*/

int
getcmd ()
{
  register int c;

  sptr = cmdline;
  argln = 0;
  aptr = argstr;
  memset (argstr, 0, MAXARGLN + 1);

  do
    {

      switch (c = 0x00FF & *sptr)
	{

	case '\0':
	case A_CR:
	case A_LF:

	  argsep = c;
	  return (argln);

	case ' ':

	  ++sptr;

	  while (*sptr == ' ')
	    ++sptr;

	  if (*sptr == A_CR || * sptr == A_LF || * sptr == '\0')
	    c = 0x00FF & *sptr;

	  argsep = c;
	  return (argln);

/* 
*/

	default:

	  if (isupper (c))
	    c = _tolower (c);

	  *aptr++ = c;
	  ++sptr;
	  ++argln;
	}

    }
  while (*sptr);

  argsep = 0;
  return (argln);
}

/* 
*/

/*
   ============================================================================
	getarg -- parse out an argument to analyze

	Parses the string at sptr for the next argument, if any.

	Returns the length of the parsed argument as the function value.

	Leaves the character that stopped the scan in argsep,
	argstr points at the parsed argument.

	Leaves sptr updated.
   ============================================================================
*/

int
getarg ()
{
  register int c;

  argln = 0;
  aptr = argstr;
  memset (argstr, 0, MAXARGLN + 1);

  do
    {

      switch (c = 0x00FF & *sptr)
	{

	case '\0':
	case A_CR:
	case A_LF:

	  argsep = c;
	  return (argln);

	case ' ':

	  ++sptr;

	  while (*sptr == ' ')
	    ++sptr;

	  if (*sptr == A_CR || * sptr == A_LF || * sptr == '\0')
	    c = 0x00FF & *sptr;

	  argsep = c;
	  return (argln);

	case ',':

	  ++sptr;
	  argsep = c;
	  return (argln);

/* 
*/

	default:

	  *aptr++ = c;
	  ++sptr;
	  ++argln;
	}

    }
  while (*sptr);

  argsep = 0;
  return (argln);
}

/* 
*/

/*
   ============================================================================
	getlong -- get a long integer (either hex or decimal)

	Stops on first non-digit  (past leading $ if present).
	Returns the character that stopped the scan.
   ============================================================================
*/

int
getlong (var)
     long *var;
{
  register long temp = 0L;
  register int csw = FALSE, c;

  if (*aptr == '$')
    {				/* leading $ makes it HEX */

      ++aptr;

      while (isxdigit (c = *aptr++))
	{

	  temp = (temp << 4) + xdtoi (c);
	  csw = TRUE;
	}

    }
  else
    {				/* otherwise, it's decimal */

      while (isdigit (c = *aptr++))
	{

	  temp = (temp * 10) + (c - '0');
	  csw = TRUE;
	}
    }

  if (csw)			/* set the new value if we got one */
    *var = temp;

  return (c);			/* return character that stopped us */
}

/* 
*/

/*
   ============================================================================
	setvar -- parse an expression and set a long variable
   ============================================================================
*/

int
setvar (var, deflt)
     long *var, deflt;
{
  int rc;
  long temp;

  *var = deflt;			/* initialize with default value */
  aptr = argstr;

  rc = getlong (var);		/* get first number in expression */

  if (rc)
    {

      do
	{

	  switch (rc)
	    {

	    case '+':		/* var + num */

	      temp = 0L;
	      rc = getlong (&temp);
	      *var = *var + temp;
	      continue;

	    case '-':		/* var - num */

	      temp = 0L;
	      rc = getlong (&temp);
	      *var = *var - temp;
	      continue;

	    default:		/* something else is an error */

	      *var = deflt;
	      return (FALSE);
	    }

	}
      while (rc);

      return (TRUE);
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	putn -- output a decimal number
   ============================================================================
*/

putn (num, cw, unit)
     long num;
     int cw, unit;
{
  register int d;

  if (!cw)
    return;

  putn (num / 10, cw - 1, unit);

  d = num % 10;

  BIOS (B_PUTC, unit, (d + '0'));

  return;
}

/* 
*/

/*
   ============================================================================
	puthn -- output a hex number
   ============================================================================
*/

puthn (num, cw, unit)
     long num;
     int cw, unit;
{
  register int d;

  if (!cw)
    return;

  puthn (num >> 4, cw - 1, unit);

  d = 0x0F & num;

  if (d > 9)
    BIOS (B_PUTC, unit, (d - 10 + 'A'));
  else
    BIOS (B_PUTC, unit, (d + '0'));

  return;
}

/* 
*/

/*
   ============================================================================
	ddump -- do the hex portion of a dump
   ============================================================================
*/

int
ddump (loc, lastloc, nwide, unit)
     register char *loc, *lastloc;
     register int nwide, unit;
{
  while (nwide--)
    {

      puthn ((long) (0xFF & *loc), 2, unit);
      BIOS (B_PUTC, unit, ' ');

      if (BIOS (B_RDAV, unit))
	{

	  BIOS (B_GETC, unit);
	  return (TRUE);
	}

      if (loc == lastloc)
	{

	  dflag = TRUE;
	  return (FALSE);
	}

      ++loc;
    }


  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	padr -- print dump address
   ============================================================================
*/

padr (adr, unit)
     long adr;
     int unit;
{
  puthn (adr, 8, unit);
  BIOS (B_PUTC, unit, ' ');
  BIOS (B_PUTC, unit, '-');
  BIOS (B_PUTC, unit, ' ');
}

/* 
*/

/*
   ============================================================================
	dtext -- do the text portion of a dump
   ============================================================================
*/

int
dtext (loc, lastloc, nwide, unit)
     register char *loc, *lastloc;
     register int nwide, unit;
{
  register int c;

  BIOS (B_PUTC, unit, ' ');
  BIOS (B_PUTC, unit, '|');

  while (nwide--)
    {

      c = 0xFF & *loc;

      if (isascii (c) && isprint (c))
	BIOS (B_PUTC, unit, c);
      else
	BIOS (B_PUTC, unit, '.');

      if (BIOS (B_RDAV, unit))
	{

	  BIOS (B_GETC, unit);
	  BIOS (B_PUTC, unit, '|');
	  return (TRUE);
	}

      if (loc == lastloc)
	{

	  BIOS (B_PUTC, unit, '|');
	  return (FALSE);
	}

      ++loc;
    }

  BIOS (B_PUTC, unit, '|');
  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cp_mset -- parse parameters for mset command
   ============================================================================
*/

int
cp_mset ()
{
  redo = FALSE;

  if (0 == getarg ())
    return (FALSE);

  if (argsep != ',')
    return (FALSE);

  if (setvar (&p_from, p_from) == FALSE)
    return (FALSE);

  return (TRUE);
}

/*
   ============================================================================
	cx_mset -- execute the mset command
   ============================================================================
*/

int
cx_mset ()
{
  while (TRUE)
    {

      if (getarg ())
	if (setvar (&p_value, p_value) == FALSE)
	  return (FALSE);

      if (p_value & ~0xFFL)
	return (FALSE);

      *p_from++ = 0xFF & p_value;

      if (argsep == A_CR)
	return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	cp_wset -- parse parameters for wset command
   ============================================================================
*/

int
cp_wset ()
{
  redo = FALSE;

  if (0 == getarg ())
    return (FALSE);

  if (argsep != ',')
    return (FALSE);

  if (setvar (&p_from, p_from) == FALSE)
    return (FALSE);

  if ((long) p_from & 1L)
    return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_wset -- execute the wset command
   ============================================================================
*/

int
cx_wset ()
{
  UWORD16 *p_uint;

  p_uint = (UWORD16 *) p_from;

  while (TRUE)
    {

      if (getarg ())
	if (setvar (&p_value, p_value) == FALSE)
	  return (FALSE);

      if (p_value & ~0xFFFFL)
	return (FALSE);

      *p_uint++ = 0xFFFF & p_value;

      if (argsep == A_CR)
	return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	cp_mtst -- parse mtest command arguments
   ============================================================================
*/

int
cp_mtst ()
{
  inext = ilast;

  if (argsep == A_CR || argsep == '\0')
    {

      p_from = (char *) 0x00000008L;
      p_to = (char *) USER_RAM - 2L;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_from, USER_RAM) == FALSE)
      return (FALSE);

  if (argsep != ',')
    return (FALSE);

  if (getarg ())
    if (setvar (&p_to, RAM_TOP) == FALSE)
      return (FALSE);

  if ((long) p_from & 1L)
    return (FALSE);

  if ((long) p_to & 1L)
    return (FALSE);

  if (p_from > p_to)
    return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mtst -- execute the mtest command
   ============================================================================
*/

int
cx_mtst ()
{
  register short mask, was, *loc, *eloc, *oldloc;

  mask = 0x0001;
  loc = (short *) p_from;
  eloc = (short *) p_to;
  oldloc = loc;

  if (p_from < (char *)USER_RAM)
      setipl (7);

  do
    {

      while (mask)
	{

	  *loc = mask;

	  if (mask != (was = *loc))
	    if (p_from < (char *)USER_RAM)
	        halt ();
	    else
	      printf ("%08lX was %04X, expected %04X\r\n", loc, was, mask);

	  *loc = ~mask;

	  if (~mask != (was = *loc))
	    if (p_from < (char *)USER_RAM)
	        halt ();
	    else
	      printf ("%08lX was %04X, expected %04X\r\n", loc, was, ~mask);

	  mask <<= 1;
	}

      mask = 0x0001;
      loc++;

    }
  while (loc <= eloc);

  if (oldloc < (short *)USER_RAM)
      rjumpto ((long) ROMADDR);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_go -- parse parameters for go command
   ============================================================================
*/

int
cp_go ()
{
  redo = FALSE;
  b0flag = FALSE;
  b1flag = FALSE;
  goflag = FALSE;

  if (getarg ())
    {

      if (setvar (&p_goto, p_goto) == FALSE)
	return (FALSE);

      if (1L & (long) p_goto)
	return (FALSE);

      goflag = TRUE;

    }

  if (getarg ())
    {

      if (setvar (&tba0, 0L) == FALSE)
	return (FALSE);

      if (1L & (long) tba0)
	return (FALSE);

      b0flag = TRUE;
    }

  if (getarg ())
    {

      if (setvar (&tba1, 0L) == FALSE)
	return (FALSE);

      if (1L & (long) tba1)
	return (FALSE);

      b1flag = TRUE;
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_mtsd -- parse parameters for mtsdump command
   ============================================================================
*/

int
cp_mtsd ()
{
  redo = TRUE;

  if (getarg ())
    {

      if (setvar (&p_from, p_from) == FALSE)
	return (FALSE);

      if (1L & (long) p_from)
	return (FALSE);
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_mpri -- parse parameters for mtsetp command
   ============================================================================
*/

int
cp_mpri ()
{
  redo = FALSE;

  if (getarg ())
    {

      if (setvar (&p_pri, p_pri) == FALSE)
	return (FALSE);

      taskpri = (unsigned) p_pri;
      return (TRUE);
    }

  return (FALSE);
}

/*
   ============================================================================
	cp_mstp -- parse parameters for mtstop command
   ============================================================================
*/

int
cp_mstp ()
{
  redo = FALSE;

  if (getarg ())
    {

      if (setvar (&p_task, p_task) == FALSE)
	return (FALSE);

      taskid = (unsigned) p_task;
      return (TRUE);
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cx_mpri -- execute the mtsetp command
   ============================================================================
*/

int
cx_mpri ()
{
  MTSetP (taskpri);
  return (TRUE);
}

/*
   ============================================================================
	cx_mqry -- execute the mtstat command
   ============================================================================
*/

int
cx_mqry ()
{
  short rc;

  rc = MTStat (taskid);

  if ((rc < -3) || (rc > 2))
    {

      printf ("unrecognized return code from MTStat:  %d\n", rc);
      return (TRUE);
    }

  printf ("task %d status:  %s\n", taskid, tsmsg[rc + 3]);
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mzap -- execute the mtzap command
   ============================================================================
*/

int
cx_mzap ()
{
  short rc;

  rc = MTZap (taskid);

  switch (rc)
    {

    case -1:

      printf ("task %d not deleted:  active or waiting\n", taskid);
      break;

    case 0:

      printf ("task %d deleted\n", taskid);
      break;

    case 1:

      printf ("task %d not found\n", taskid);
      break;

    default:

      printf ("unrecognized return code from MTZap:  %d\n", rc);
      break;
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mstp -- execute the mtstop command
   ============================================================================
*/

int
cx_mstp ()
{
  short rc;

  rc = MTStop (taskid);

  switch (rc)
    {

    case -3:

      printf ("task %d not stopped:  current task\n", taskid);
      break;

    case -2:

      printf ("task %d not stopped:  can't find it on the ready queue\n",
	      taskid);
      break;

    case -1:

      printf ("task %d not stopped:  can't find its TCB\n", taskid);
      break;

    case 0:

      printf ("task %d stopped\n", taskid);
      break;

    case 1:

      printf ("task %d already stopped\n", taskid);
      break;

    case 2:

      printf ("task %d flagged to stop\n", taskid);
      break;

    default:

      printf ("unrecognized return code from MTStop:  %d\n", rc);
      break;
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mtsk -- execute the tasks command
   ============================================================================
*/

int
cx_mtsk ()
{
  MTPrTS ();
  return (TRUE);
}

/*
   ============================================================================
	cx_mtcs -- execute the tcbs command
   ============================================================================
*/

int
cx_mtcs ()
{
  MTPrTL ();
  return (TRUE);
}

/*
   ============================================================================
	cx_mtrq -- execute the readyq command
   ============================================================================
*/

int
cx_mtrq ()
{
  MTPrRq ();
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mtsd -- execute the sem command
   ============================================================================
*/

int
cx_mtsd ()
{
  MTSDump (p_from, (char *) 0L);
  return (TRUE);
}

/*
   ============================================================================
	cx_mtcb -- execute the tcb command
   ============================================================================
*/

int
cx_mtcb ()
{
  MTPrTCB (p_from, (char *) 0L);
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_run -- parse parameters for run command
   ============================================================================
*/

int
cp_run ()
{
  redo = FALSE;

  if (getarg ())
    {

      if (setvar (&p_goto, p_goto) == FALSE)
	return (FALSE);

      if (1L & (long) p_goto)
	return (FALSE);
    }

  if (getarg ())
    {

      if (setvar (&p_pri, (long) DFRUNPRI) == FALSE)
	return (FALSE);

      if (0xFFFF0000L & (long) p_pri)
	return (FALSE);
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_run() -- execute the run command
   ============================================================================
*/

int
cx_run ()
{
  register unsigned task;

  if ((struct _mt_def *) NIL == _MT_)
    _MT_ = (struct _mt_def *) XBIOS (X_MTDEFS);

  task = MTID ();

  MTSetT (&runtcb, task, (unsigned) (0x0000FFFFL & p_pri), 0x2200, -1L,
	  &runstak[STKLEN], p_goto, _MT_);

  printf ("TCB setup - TID = %d ... Swapping ...\n", task);

  MTSwap ();

  printf ("Task %d initiated\n", task);
  return (TRUE);
}


/*
   ============================================================================
	cx_wait() -- execute the wait command
   ============================================================================
*/

int
cx_wait ()
{
  printf ("Waiting on \"SemQuit\" ...\n");
  SMWait (&SemQuit);
  printf ("Task signalled \"SemQuit\"\n");
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_dini() -- execute the dinit command
   ============================================================================
*/

int
cx_dini ()
{
  redo = TRUE;
  hdvini ();
  return (TRUE);
}

/*
   ============================================================================
	cx_zap -- execute the zap command
   ============================================================================
*/

int
cx_zap ()
{
  register short *p, *q;

  p = (short *) USER_RAM;
  q = (short *) RAM_TOP;

  setipl (7);

  while (p <= q)
    *p++ = 0;

  rjumpto (ROMADDR);
}

/* 
*/

/*
   ============================================================================
	cx_omap() -- execute the omap command
   ============================================================================
*/

int
cx_omap ()
{
  register short i, width, xloc;

  printf ("Pr B/C Locn      Wd Xloc Flags\r\n");

  for (i = 0; i < 16; i++)
    {

      xloc = v_odtab[i][1] & 0x03FF;

      if (xloc & 0x0200)	/* sign extend xloc */
	xloc |= 0xFC00;

      width = (v_odtab[i][1] >> 10) & 0x003F;

      printf ("%2d %s ", i, ((v_odtab[i][0] & V_CBS) ? "Chr" : "Bit"));

      printf ("$%08lX %2d %4d ", ((long) v_odtab[i][2] << 1), width, xloc);

      printf ("$%04X\r\n", v_odtab[i][0]);
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_help -- execute the help command
   ============================================================================
*/

int
cx_help ()
{
  int i, j;

  j = 0;

  writeln (cmdunit, CRLF);

  for (i = 0; i < NCMDS; i++)
    {

      if (j++ == 22)
	{

	  j = 0;

	  if (waitcr ())
	    return (TRUE);
	}

      writeln (cmdunit, "     ");
      writeln (cmdunit, cmtab[i].cname);
      writeln (cmdunit, "  ");
      writeln (cmdunit, cmtab[i].hstr);
      writeln (cmdunit, CRLF);
    }

  writeln (cmdunit, CRLF);
  return (TRUE);
}

/* 
* /

/*
   ============================================================================
	cx_bpb -- execute bpb command
   ============================================================================
*/

int
cx_bpb ()
{
  register struct bpb *bpp;

  if (0L == (bpp = (struct bpb *) BIOS (B_GBPB, 0)))
    {

      writeln (cmdunit, "\r\n\nERROR -- Unable to read BPB\r\n\n");
      return (FALSE);

    }
  else
    {

      writeln (cmdunit, "\r\n\nBPB values:\r\n");
      writeln (cmdunit, "\r\n   recsiz   ");
      putn ((long) bpp->recsiz, 5, cmdunit);
      writeln (cmdunit, "\r\n   clsiz     ");
      putn ((long) bpp->clsiz, 4, cmdunit);
      writeln (cmdunit, "\r\n   clsizb   ");
      putn ((long) bpp->clsizb, 5, cmdunit);
      writeln (cmdunit, "\r\n   rdlen     ");
      putn ((long) bpp->rdlen, 4, cmdunit);
      writeln (cmdunit, "\r\n   fsiz      ");
      putn ((long) bpp->fsiz, 4, cmdunit);
      writeln (cmdunit, "\r\n   fatrec   ");
      putn ((long) bpp->fatrec, 5, cmdunit);
      writeln (cmdunit, "\r\n   datrec   ");
      putn ((long) bpp->datrec, 5, cmdunit);
      writeln (cmdunit, "\r\n   numcl    ");
      putn ((long) bpp->numcl, 5, cmdunit);
      writeln (cmdunit, "\r\n   bflags    ");
      puthn ((long) bpp->bflags, 4, cmdunit);
      writeln (cmdunit, "\r\n   ntracks   ");
      putn ((long) bpp->ntracks, 4, cmdunit);
      writeln (cmdunit, "\r\n   nsides    ");
      putn ((long) bpp->nsides, 4, cmdunit);
      writeln (cmdunit, "\r\n   sec/cyl  ");
      putn ((long) bpp->dspc, 5, cmdunit);
      writeln (cmdunit, "\r\n   sec/trk  ");
      putn ((long) bpp->dspt, 5, cmdunit);
      writeln (cmdunit, "\r\n   hidden    ");
      putn ((long) bpp->hidden, 4, cmdunit);
      writeln (cmdunit, "\r\n\n");
      return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	cx_go -- execute the go command
   ============================================================================
*/

int
cx_go ()
{
  redo = FALSE;
  exflag = TRUE;
  wzcrsh = FALSE;

  if (goflag)
    regptr->reg_pc = p_goto;

  if (b0flag)
    {

      if (p_ba0)
	{

	  if (*p_ba0 != (UWORD16) BPINST)
	    {

	      writeln (cmdunit, "\r\n\n** Breakpoint 0 at ");
	      puthn ((long) p_ba0, 8, cmdunit);
	      writeln (cmdunit, " was ");
	      puthn (0xFFFFL & (long) (*p_ba0), 4, cmdunit);
	      writeln (cmdunit, " instead of ");
	      puthn (0xFFFFL & (long) BPINST, 4, cmdunit);
	      writeln (cmdunit, " **\r\n\n");
	    }

	  *p_ba0 = p_bv0;
	}

      p_ba0 = tba0;
      p_bv0 = *p_ba0;
      *p_ba0 = (UWORD16) BPINST;
    }

/* 
*/

  if (b1flag)
    {

      if (p_ba1)
	{

	  if (*p_ba1 != (UWORD16) BPINST)
	    {

	      writeln (cmdunit, "\r\n\n** Breakpoint 1 at ");
	      puthn ((long) p_ba1, 8, cmdunit);
	      writeln (cmdunit, " was ");
	      puthn (0xFFFFL & (long) (*p_ba1), 4, cmdunit);
	      writeln (cmdunit, " instead of ");
	      puthn (0xFFFFL & (long) BPINST, 4, cmdunit);
	      writeln (cmdunit, " **\r\n\n");
	    }

	  *p_ba1 = p_bv1;
	}

      p_ba1 = tba1;
      p_bv1 = *p_ba1;
      *p_ba1 = (UWORD16) BPINST;
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_dump -- parse dump parameters
   ============================================================================
*/

int
cp_dump ()
{
  inext = ilast;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if (argsep == A_CR || argsep == '\0')
    {

      p_to = p_from;
      p_width = 16L;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if (argsep == A_CR || argsep == '\0')
    {

      p_width = 16L;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_width, p_width) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if ((p_width <= 0L) || (p_width > 16L))
    {

      p_width = 16L;
      redo = FALSE;
      return (FALSE);
    }

  redo = TRUE;
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_fill -- parse parameters for fill command
   ============================================================================
*/

int
cp_fill ()
{
  redo = FALSE;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_len, p_len) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_value, p_value) == FALSE)
      return (FALSE);

  if (p_value & ~0xFFL)
    return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_wfil -- parse parameters for wfill command
   ============================================================================
*/

int
cp_wfil ()
{
  redo = FALSE;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_len, p_len) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_value, p_value) == FALSE)
      return (FALSE);

  if ((long) p_from & 1L)
    return (FALSE);

  if (p_value & ~0xFFFFL)
    return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_copy -- parse parameters for copy command
   ============================================================================
*/

int
cp_copy ()
{
  redo = FALSE;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_len, p_len) == FALSE)
      return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_chek -- parse parameters for chek command
   ============================================================================
*/

int
cp_chek ()
{
  redo = FALSE;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      return (FALSE);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_read -- parse parameters for read command
   ============================================================================
*/

int
cp_read ()
{
  redo = FALSE;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      return (FALSE);

  if (getarg ())
    if (setvar (&p_len, p_len) == FALSE)
      return (FALSE);

  if ((~0x7FFFL) & p_len)
    return (FALSE);

  if ((~0xFFFFL) & (long) p_from)
    return (FALSE);

  return (TRUE);
}

/*
   ============================================================================
	cp_null -- parse null parameter line
   ============================================================================
*/

int
cp_null ()
{
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_rset -- parse rset command parameters
   ============================================================================
*/

int
cp_rset ()
{
  int rc;

  rc = 0;
  redo = FALSE;

  if (0 == getarg ())
    return (FALSE);

  str2lc (argstr);

  if (0 == (rc = strlcmp (argstr, rlist)))
    return (FALSE);

  if (0 == getarg ())
    return (FALSE);

  if (FALSE == setvar (&p_value, 0L))
    return (FALSE);

  rnum = rc;
  return (TRUE);
}

/* 
*/
/*
   ============================================================================
	cx_chek -- process chek command
   ============================================================================
*/

int
cx_chek ()
{
  register long csum;
  register char *cp;

  redo = FALSE;
  csum = 0L;

  for (cp = p_from; cp <= p_to; cp++)
    csum += 0x000000FFL & *cp;

  printf ("Checksum = 0x%08lX\r\n", csum);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_rset -- process rset command
   ============================================================================
*/

int
cx_rset ()
{
  redo = FALSE;

  if (rnum < 1)
    return (FALSE);

  if (rnum < 9)
    {				/* d0..d7 -- data register */

      regptr->d_reg[rnum - 1] = p_value;
      return (TRUE);
    }

  if (rnum < 17)
    {				/* a0..a7 -- address register */

      regptr->a_reg[rnum - 9] = (char *) p_value;
      return (TRUE);
    }

/* 
*/

  if (rnum == 17)
    {				/* sr -- status register */

      if ((~0xFFFFL) & p_value)
	return (FALSE);

      regptr->reg_sr = (UWORD16) p_value;
      return (TRUE);
    }

  if (rnum == 18)
    {				/* pc -- program counter */

      if (1L & p_value)
	return (FALSE);

      regptr->reg_pc = (char *) p_value;
      return (TRUE);
    }

  if (rnum == 19)
    {				/* sp -- stack pointer */

      if (1L & p_value)
	return (FALSE);

      regptr->a_reg[7] = (char *) p_value;
      return (TRUE);
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cp_vrst -- parse vrset command parameters
   ============================================================================
*/

int
cp_vrst ()
{
  int rc;

  rc = 0;
  redo = FALSE;

  if (0 == getarg ())
    return (FALSE);

  str2lc (argstr);

  if (0 == (rc = strlcmp (argstr, vrlist)))
    return (FALSE);

  if (0 == getarg ())
    return (FALSE);

  if (FALSE == setvar (&p_value, 0L))
    return (FALSE);

  if (vrnum < 17)
    {				/* complete register */

      vrnum = rc;
      return (TRUE);
    }

  if (vrnum < 21)
    {				/* horizontal register */

      if (p_value & ~0x003F)
	return (FALSE);

      vrnum = rc;
      return (TRUE);
    }

  if (vrnum < 25)
    {				/* vertical register */

      if (p_value & ~0x03FF)
	return (FALSE);

      vrnum = rc;
      return (TRUE);
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cx_vrst -- process vrset command
   ============================================================================
*/

int
cx_vrst ()
{
  redo = FALSE;

  if (vrnum < 1)
    return (FALSE);

  if (vrnum < 17)
    {				/* 1..16 -- r0..r15 -- complete register */

      v_regs[vrnum - 1] = p_value;
      return (TRUE);
    }

  if (vrnum < 21)
    {				/* 17..20 -- h0..h3 -- horizontal register */

      v_regs[vrnum - 5] = (v_regs[vrnum - 5] & 0x03FF) |
	((p_value << 10) & 0xFC00);
      return (TRUE);
    }

  if (vrnum < 25)
    {				/* 21..24 -- v0..v3 -- vertical register */

      v_regs[vrnum - 9] = (v_regs[vrnum - 9] & 0xFC00) | p_value;
      return (TRUE);
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cx_vreg -- process vregs command
   ============================================================================
*/

int
cx_vreg ()
{
  register int i, j, k, l;
  register unsigned *rp;

  rp = &v_regs[0];
  l = 0;

  for (i = 0; i < 2; i++)
    {

      for (j = 0; j < 2; j++)
	{

	  for (k = 0; k < 4; k++)
	    {

	      writeln (cmdunit, "  ");
	      putn ((long) l++, 2, cmdunit);
	      writeln (cmdunit, ":");
	      puthn ((long) *rp++, 4, cmdunit);
	    }

	  writeln (cmdunit, "  ");
	}

      writeln (cmdunit, "\r\n");
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	do_srec -- load a Motorola S record
   ============================================================================
*/

int
do_srec (line)
     register char *line;
{
  register char *ldadr;
  register int c, csum, i, len;
  register unsigned val;

  if ('S' != (c = *line++))
    return (-1);		/* error 1 = missing initial S */

  switch (c = *line++)
    {

    case '2':

      csum = 0;

      if (isxdigit (c = *line++))
	len = xdtoi (c);
      else
	return (-2);		/* error 2 = bad length byte */

      if (isxdigit (c = *line++))
	len = (len << 4) + xdtoi (c);
      else
	return (-2);

      csum += (len & 0xFF);
      ldadr = (char *) 0;
      len -= 4;

      for (i = 0; i < 3; i++)
	{

	  if (isxdigit (c = *line++))
	    val = xdtoi (c);
	  else
	    return (-3);	/* error 3 = bad address byte */

	  if (isxdigit (c = *line++))
	    val = (val << 4) + xdtoi (c);
	  else
	    return (-3);

	  ldadr = (char *) (((long) ldadr << 8) + (long) val);
	  csum += (val & 0xFF);
	}

      for (i = 0; i < len; i++)
	{

	  if (isxdigit (c = *line++))
	    val = xdtoi (c);
	  else
	    return (-4);	/* error 4 = bad data byte */

	  if (isxdigit (c = *line++))
	    val = (val << 4) + xdtoi (c);
	  else
	    return (-4);

	  csum += (val & 0xFF);
	  *ldadr = val & 0xFF;

	  if ((*ldadr & 0xFF) != (val & 0xFF))
	    return (-5);	/* error 5 = store failed */

	  ldadr++;
	}

      csum = 0xFF & ~csum;

      if (isxdigit (c = *line++))
	val = xdtoi (c);
      else
	return (-6);		/* error 6 = bad checksum byte */

      if (isxdigit (c = *line++))
	val = (val << 4) + xdtoi (c);
      else
	return (-6);

      if (csum != (val & 0xFF))
	return (-7);		/* error 7 = bad checksum */

      return (1);

    case '9':

      if (memcmpu (line, SREC9, 10) == 0)
	return (0);
      else
	return (-8);		/* error 8 = bad end record */

    default:

      return (-9);		/* error 9 = unknown s type */
    }

  return (-10);			/* error 10 = switch failed */
}

/* 
*/

/*
   ============================================================================
	cx_load -- process load command
   ============================================================================
*/

int
cx_load ()
{
  register int rc;

  do
    {

      rc = getrln (cmdunit, MAXCMDLN, cmdline);

      switch (rc)
	{

	case A_CR:

	  rc = do_srec (cmdline);

	  if (rc < 0)
	    {

	      rc = -rc;
	      writeln (cmdunit, NACK);
	      writeln (cmdunit, "** Load error ");
	      putn ((long) rc, 3, cmdunit);
	      writeln (cmdunit, " **\r\n\n");
	      return (FALSE);

	    }
	  else
	    {

	      writeln (cmdunit, TACK);
	    }

	  continue;

	case CTL ('X'):

	  rc = 1;
	  writeln (cmdunit, TACK);
	  continue;

/* 
*/

	default:

	  writeln (cmdunit, NACK);
	  writeln (cmdunit, "** Load aborted on ");
	  puthn (rc, 2, cmdunit);
	  writeln (cmdunit, " **\r\n\n");
	  return (FALSE);
	}

    }
  while (rc);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_fill -- execute the fill command
   ============================================================================
*/

int
cx_fill ()
{
  register char *cp = p_from;
  register long count;

  redo = FALSE;

  for (count = p_len; count > 0L; count--)
    {

      *cp = (char) (0xFFL & p_value);

      if (*cp != (char) (0xFFL & p_value))
	{

	  writeln (cmdunit, "\r\n** FILL failed at ");
	  puthn ((long) cp, 8, cmdunit);
	  writeln (cmdunit, " **\r\n");
	  return (FALSE);
	}

      ++cp;
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_wfill -- execute the wfill command
   ============================================================================
*/

int
cx_wfil ()
{
  register UWORD16 *cp = (UWORD16 *) p_from;
  register long count;

  redo = FALSE;

  for (count = p_len; count > 0L; count--)
    *cp++ = (UWORD16) (0xFFFFL & p_value);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_copy -- execute the copy command
   ============================================================================
*/

int
cx_copy ()
{
  register char *from = p_from, *to = p_to;
  register long count = p_len;

  redo = FALSE;

  if (to > from)
    {

      from = from + count;
      to = to + count;

      while (count--)
	{

	  --from;
	  --to;
	  *to = *from;

	  if (*from != * to)
	    {

	      writeln (cmdunit, "\r\n** COPY failed from ");
	      puthn ((long) from, 8, cmdunit);
	      writeln (cmdunit, " to ");
	      puthn ((long) to, 8, cmdunit);
	      writeln (cmdunit, " with (from) = ");
	      puthn ((long) (*from), 2, cmdunit);
	      writeln (cmdunit, " and (to) = ");
	      puthn ((long) (*to), 2, cmdunit);
	      writeln (cmdunit, " **\r\n");
	      return (FALSE);
	    }
	}

/* 
*/

    }
  else
    {

      while (count--)
	{

	  *to = *from;

	  if (*from != * to)
	    {

	      writeln (cmdunit, "\r\n** COPY failed from ");
	      puthn ((long) from, 8, cmdunit);
	      writeln (cmdunit, " to ");
	      puthn ((long) to, 8, cmdunit);
	      writeln (cmdunit, " with (from) = ");
	      puthn ((long) (*from), 2, cmdunit);
	      writeln (cmdunit, " and (to) = ");
	      puthn ((long) (*to), 2, cmdunit);
	      writeln (cmdunit, " **\r\n");
	      return (FALSE);
	    }

	  ++from;
	  ++to;
	}
    }

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_dump -- execute the dump command
   ============================================================================
*/

int
cx_dump ()
{
  register int nw, rc;

  redo = TRUE;
  d_cur = p_from;
  d_next = p_to + 1;
  d_last = ((long) p_to - (long) p_from) + d_next;
  nw = p_width;
  rc = TRUE;

  do
    {

      writeln (cmdunit, CRLF);
      padr (p_from, cmdunit);

      dflag = FALSE;

      if (ddump (p_from, p_to, nw, cmdunit))
	rc = FALSE;

      if (rc)
	if (dtext (p_from, p_to, nw, cmdunit))
	  rc = FALSE;

      p_from = p_from + p_width;

      if (dflag)
	rc = FALSE;

    }
  while (rc);

  p_from = d_cur;

  writeln (cmdunit, CRLF);
  writeln (cmdunit, CRLF);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	wdump -- dump words in hex  (no ASCII)
   ============================================================================
*/

int
wdump (loc, lastloc, nwide, unit)
     register UWORD16 *loc, *lastloc;
     int nwide, unit;
{
  while (nwide--)
    {

      puthn ((long) (0xFFFFL & *loc), 4, unit);
      BIOS (B_PUTC, unit, ' ');

      if (BIOS (B_RDAV, unit))
	return (TRUE);

      if (loc == lastloc)
	{

	  dflag = TRUE;
	  return (FALSE);
	}

      ++loc;
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	ldump -- dump longs in hex  (no ASCII)
   ============================================================================
*/

int
ldump (loc, lastloc, nwide, unit)
     register long *loc, *lastloc;
     int nwide, unit;
{
  while (nwide--)
    {

      puthn (*loc, 8, unit);
      BIOS (B_PUTC, unit, ' ');

      if (BIOS (B_RDAV, unit))
	return (TRUE);

      if (loc == lastloc)
	{

	  dflag = TRUE;
	  return (FALSE);
	}

      ++loc;
    }

  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cp_wdmp -- process parameters for wdump command
   ============================================================================
*/

int
cp_wdmp ()
{
  inext = ilast;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if ((long) p_from & 1L)
    {

      redo = FALSE;
      return (FALSE);
    }

  if (argsep == A_CR || argsep == '\0')
    {

      p_to = p_from;
      p_width = 8;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if ((long) p_to & 1L)
    {

      redo = FALSE;
      return (FALSE);
    }

  if (argsep == A_CR || argsep == '\0')
    {

      p_width = 8;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_width, p_width) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  redo = TRUE;
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_ldmp -- process parameters for ldump command
   ============================================================================
*/

int
cp_ldmp ()
{
  inext = ilast;

  if (getarg ())
    if (setvar (&p_from, p_from) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if ((long) p_from & 1L)
    {

      redo = FALSE;
      return (FALSE);
    }

  if (argsep == A_CR || argsep == '\0')
    {

      p_to = p_from;
      p_width = 4;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_to, p_to) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  if ((long) p_to & 1L)
    {

      redo = FALSE;
      return (FALSE);
    }

  if (argsep == A_CR || argsep == '\0')
    {

      p_width = 4;
      redo = TRUE;
      return (TRUE);
    }

  if (getarg ())
    if (setvar (&p_width, p_width) == FALSE)
      {

	redo = FALSE;
	return (FALSE);
      }

  redo = TRUE;
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_ilev -- parse the ipl command
   ============================================================================
*/

int
cp_ilev ()
{
  long iplevl;

  if (argsep == A_CR || argsep == '\0')
    return (TRUE);

  if (getarg ())
    if (setvar (&iplevl, iplevl) == FALSE)
      return (FALSE);

  if (iplevl > 7)
    return (FALSE);

  iplev = iplevl;

  return (TRUE);
}

/*
   ============================================================================
	cx_ilev -- execute ipl command
   ============================================================================
*/

int
cx_ilev ()
{
  if (-1 == setipl (iplev))
    {

      printf ("ERROR -- Could not set IPL to %d\r\n", iplev);
      return (FALSE);
    }
  else
    printf ("ROMP IPL now set to %d\r\n", iplev);

  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_monc() -- parse the monc command
   ============================================================================
*/

int
cp_monc ()
{
  if (getarg ())
    if (setvar (&monptr, monptr) == FALSE)
      return (FALSE);

  monsw = MON_C;
  redo = TRUE;
  return (TRUE);
}

/*
   ============================================================================
	cp_mons() -- parse the mons command
   ============================================================================
*/

int
cp_mons ()
{
  if (getarg ())
    if (setvar (&monptr, monptr) == FALSE)
      return (FALSE);

  monsw = MON_S;
  redo = TRUE;
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cp_monl() -- parse the monl command
   ============================================================================
*/

int
cp_monl ()
{
  if (getarg ())
    if (setvar (&monptr, monptr) == FALSE)
      return (FALSE);

  monsw = MON_L;
  redo = TRUE;
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_mon() -- process the mon commands
   ============================================================================
*/

int
cx_mon ()
{
  register char vc, vcc;
  register short vs, vss, *vsp;
  register long vl, vll, *vlp;

  switch (monsw)
    {

    case MON_C:

      vc = *monptr & 0x0FF;
      puthn ((long) vc, 2, cmdunit);
      writeln (cmdunit, "\r\n");

      while (!(0xFFFFL & BIOS (B_RDAV, CON_DEV)))
	{

	  vcc = *monptr & 0x0FF;

	  if (vc != vcc)
	    {

	      vc = vcc;
	      puthn ((long) vc, 2, cmdunit);
	      writeln (cmdunit, "\r\n");
	    }
	}

      BIOS (B_GETC, CON_DEV);
      return (TRUE);

    case MON_S:

      vsp = (short *) monptr;
      vs = *vsp;
      puthn ((long) vs, 4, cmdunit);
      writeln (cmdunit, "\r\n");

      while (!(0xFFFFL & BIOS (B_RDAV, CON_DEV)))
	{

	  vss = *vsp;

	  if (vs != vss)
	    {

	      vs = vss;
	      puthn ((long) vs, 4, cmdunit);
	      writeln (cmdunit, "\r\n");
	    }
	}

      BIOS (B_GETC, CON_DEV);
      return (TRUE);

/* 
*/
    case MON_L:

      vlp = (long *) monptr;
      vl = *vlp;
      puthn (vl, 8, cmdunit);
      writeln (cmdunit, "\r\n");

      while (!(0xFFFFL & BIOS (B_RDAV, CON_DEV)))
	{

	  vll = *vlp;

	  if (vl != vll)
	    {

	      vl = vll;
	      puthn (vl, 8, cmdunit);
	      writeln (cmdunit, "\r\n");
	    }
	}

      BIOS (B_GETC, CON_DEV);
      return (TRUE);

    default:
      return (FALSE);
    }
}

/* 
*/

/*
   ============================================================================
	cx_wdmp -- process wdump command
   ============================================================================
*/

int
cx_wdmp ()
{
  int nw, rc;

  d_cur = p_from;
  d_next = p_to + 2;
  d_last = ((long) p_to - (long) p_from) + d_next;
  nw = p_width;
  rc = TRUE;

  do
    {

      writeln (cmdunit, CRLF);
      padr (p_from, cmdunit);
      dflag = FALSE;

      if (wdump (p_from, p_to, nw, cmdunit))
	rc = FALSE;

      p_from = p_from + (p_width << 1);

      if (dflag)
	rc = FALSE;

    }
  while (rc);

  p_from = d_cur;

  writeln (cmdunit, CRLF);
  writeln (cmdunit, CRLF);
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	cx_ldmp -- process ldump command
   ============================================================================
*/

int
cx_ldmp ()
{
  int nw, rc;

  d_cur = p_from;
  d_next = p_to + 4;
  d_last = ((long) p_to - (long) p_from) + d_next;
  nw = p_width;
  rc = TRUE;

  do
    {

      writeln (cmdunit, CRLF);
      padr (p_from, cmdunit);
      dflag = FALSE;

      if (ldump (p_from, p_to, nw, cmdunit))
	rc = FALSE;

      p_from = p_from + (p_width << 2);

      if (dflag)
	rc = FALSE;

    }
  while (rc);

  p_from = d_cur;

  writeln (cmdunit, CRLF);
  writeln (cmdunit, CRLF);
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	do_cmd -- process a command line
   ============================================================================
*/

do_cmd ()
{
  int rc, i;

  /* prompt for a command line */

  writeln (cmdunit, "ROMP: ");

  /*  get a command line */

  rc = getln (cmdunit, MAXCMDLN, cmdline);

/* 
*/

  /* dispatch based on rc */

  switch (rc)
    {

    case A_CR:

      writeln (cmdunit, CRLF);

      if (getcmd ())
	{

	  for (i = 0; i < NCMDS; i++)
	    {

	      if (0 == strcmp (argstr, cmtab[i].cname))
		{

		  ilast = i;

		  if ((*cmtab[i].cp) ())
		    {

		      if (FALSE == (*cmtab[i].cx) ())
			writeln (cmdunit, EMSG1);

		    }
		  else
		    {

		      writeln (cmdunit, EMSG2);
		    }

		  return;
		}
	    }

	  writeln (cmdunit, EMSG3);
	  return;

	}
      else
	{

/* 
*/

	  if (redo)
	    {

	      if (FALSE == (*cmtab[ilast].cx) ())
		writeln (cmdunit, EMSG1);

	    }
	  else
	    {

	      writeln (cmdunit, EMSG5);
	    }

	  return;
	}

    case CTL ('X'):

      writeln (cmdunit, CANNED);
      return;

    default:

      writeln (cmdunit, EMSG6);
      return;
    }

  writeln (cmdunit, EMSG7);
  return;
}

/* 
*/

/*
   ============================================================================
	cx_next -- process the next command
   ============================================================================
*/

int
cx_next ()
{
  p_to = d_last;
  p_from = d_next;
  return ((*cmtab[inext].cx) ());
}

/* 
*/

/*
   ============================================================================
	cx_read() -- process the read command
   ============================================================================
*/

int
cx_read ()
{
  long rc;
  int ns, recno;

  ns = p_len & 0x7FFFL;
  recno = (long) p_from & 0xFFFFL;

  rc = BIOS (B_RDWR, 2, p_to, ns, recno, 0);

  if (rc & 0xFFFFL)
    {

      writeln (cmdunit, "\r\nERROR reading disk:  ");
      puthn (rc, 8, cmdunit);
      writeln (cmdunit, "\r\n\n");
      return (FALSE);

    }
  else
    {

      return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	cx_writ() -- process the write command
   ============================================================================
*/

int
cx_writ ()
{
  long rc;
  int ns, recno;

  ns = p_len & 0x7FFFL;
  recno = (long) p_from & 0xFFFFL;

  rc = BIOS (B_RDWR, 3, p_to, ns, recno, 0);

  if (rc & 0xFFFFL)
    {

      writeln (cmdunit, "\r\nERROR writing disk:  ");
      puthn (rc, 8, cmdunit);
      writeln (cmdunit, "\r\n\n");
      return (FALSE);

    }
  else
    {

      return (TRUE);
    }
}

/* 
*/

/*
   ============================================================================
	showrs() -- show registers
   ============================================================================
*/

showrs (rp)
     struct regs *rp;
{
  int i;
  UWORD16 srtemp;

  writeln (cmdunit, "       ");

  for (i = 0; i < 8; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      BIOS (B_PUTC, cmdunit, '0' + i);
      writeln (cmdunit, "_______");
    }

  writeln (cmdunit, "\r\nd0..d7 ");

  for (i = 0; i < 8; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      puthn (rp->d_reg[i], 8, cmdunit);
    }

  writeln (cmdunit, "\r\na0..a7 ");

  for (i = 0; i < 8; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      puthn (rp->a_reg[i], 8, cmdunit);
    }

  writeln (cmdunit, "\r\nPC =  ");
  puthn ((long) rp->reg_pc, 8, cmdunit);

  srtemp = rp->reg_sr;
  writeln (cmdunit, ",    SR = ");
  puthn ((long) srtemp & 0xFFFFL, 4, cmdunit);

/* 
*/

  writeln (cmdunit, "  (IPL = ");
  puthn ((long) (srtemp >> 8) & 0x7L, 1, cmdunit);
  writeln (cmdunit, ", ");

  if (srtemp & 0x8000)
    BIOS (B_PUTC, cmdunit, 'T');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x2000)
    BIOS (B_PUTC, cmdunit, 'S');
  else
    BIOS (B_PUTC, cmdunit, '-');

  BIOS (B_PUTC, cmdunit, ' ');

  if (srtemp & 0x10)
    BIOS (B_PUTC, cmdunit, 'X');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x8)
    BIOS (B_PUTC, cmdunit, 'N');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x4)
    BIOS (B_PUTC, cmdunit, 'Z');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x2)
    BIOS (B_PUTC, cmdunit, 'V');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x1)
    BIOS (B_PUTC, cmdunit, 'C');
  else
    BIOS (B_PUTC, cmdunit, '-');

  writeln (cmdunit, " )\r\n");
}

/* 
*/

/*
   ============================================================================
	showcr() -- show crash registers
   ============================================================================
*/

showcr ()
{
  register int i;
  register char *cause;
  register UWORD16 srtemp;

  writeln (cmdunit, "BIOS Crash Area Dump\r\n");
  writeln (cmdunit, "       ");

  for (i = 0; i < 8; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      BIOS (B_PUTC, cmdunit, '0' + i);
      writeln (cmdunit, "_______");
    }

  writeln (cmdunit, "\r\nd0..d7 ");

  for (i = 0; i < 8; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      puthn (crshrg[i], 8, cmdunit);
    }

  writeln (cmdunit, "\r\na0..a7 ");

  for (i = 8; i < 16; i++)
    {

      BIOS (B_PUTC, cmdunit, ' ');
      puthn (crshrg[i], 8, cmdunit);
    }

  writeln (cmdunit, "\r\n\nPC =  ");
  puthn ((long) crshpc, 8, cmdunit);

  srtemp = crshsr;
  writeln (cmdunit, ",    SR = ");
  puthn ((long) srtemp & 0xFFFFL, 4, cmdunit);

/* 
*/

  writeln (cmdunit, "  (IPL = ");
  puthn ((long) (srtemp >> 8) & 0x7L, 1, cmdunit);
  writeln (cmdunit, ", ");

  if (srtemp & 0x8000)
    BIOS (B_PUTC, cmdunit, 'T');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x2000)
    BIOS (B_PUTC, cmdunit, 'S');
  else
    BIOS (B_PUTC, cmdunit, '-');

  BIOS (B_PUTC, cmdunit, ' ');

  if (srtemp & 0x10)
    BIOS (B_PUTC, cmdunit, 'X');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x8)
    BIOS (B_PUTC, cmdunit, 'N');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x4)
    BIOS (B_PUTC, cmdunit, 'Z');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x2)
    BIOS (B_PUTC, cmdunit, 'V');
  else
    BIOS (B_PUTC, cmdunit, '-');

  if (srtemp & 0x1)
    BIOS (B_PUTC, cmdunit, 'C');
  else
    BIOS (B_PUTC, cmdunit, '-');

  writeln (cmdunit, " )\r\n");

/* 
*/
  writeln (cmdunit, "TRAP vector number = ");
  putn ((long) crshvc[0], 2, cmdunit);

  cause = "  (no handler for interrupt)";

  switch (crshvc[0] & 0xFF)
    {

    case 2:			/* 2:  bus error */

      cause = "  (Bus error)";
      break;

    case 3:			/* 3:  address error */

      cause = "  (Address error)";
      break;

    case 4:			/* 4:  illegal instruction */

      cause = "  (Illegal instruction)";
      break;

    case 5:			/* 5:  zero divide */

      cause = "  (Zero divide)";
      break;

    case 6:			/* 6:  CHK instruction */

      cause = "  (CHK instruction)";
      break;

    case 7:			/* 7:  TRAPV instruction */

      cause = "  (TRAPV instruction)";
      break;

    case 8:			/* 8:  privilege violation */

      cause = "  (Privilege violation)";
      break;

    case 9:			/* 9:  trace */

      cause = "  (Trace -- not implemented)";
      break;

    case 10:			/* 10:  line 1010 emulator */

      cause = "  (Line 1010 Emulator -- not implemented)";
      break;

    case 11:			/* 11:  line 1111 emulator */

      cause = "  (Line 1111 Emulator -- not implemented";
      break;

    case 15:			/* 15:  uninitialized interrupt vector */

      cause = "  (Uninitialized interrupt)";
      break;

    case 24:			/* 24:  spurious interrupt */

      cause = "  (Spurious interrupt)";
      break;

    case 25:			/* 25:  Autovector Level 1 */

      cause = "  (Level 1 Interrupt -- unimplmented)";
      break;

    case 26:			/* 26:  Autovector Level 2 */

      cause = "  (Level 2 Interrupt -- unimplmented)";
      break;

    case 27:			/* 27:  Autovector Level 3 */

      cause = "  (Level 3 Interrupt -- unimplmented)";
      break;

    case 28:			/* 28:  Autovector Level 4 */

      cause = "  (Level 4 Interrupt -- unimplmented)";
      break;

    case 29:			/* 29:  Autovector Level 5 */

      cause = "  (Level 5 Interrupt -- unimplmented)";
      break;

    case 30:			/* 30:  Autovector Level 6 */

      cause = "  (Level 6 Interrupt -- unimplmented)";
      break;

    case 31:			/* 31:  Autovector Level 7 */

      cause = "  (Level 7 Interrupt -- unimplmented)";
      break;

    }

  writeln (cmdunit, cause);
  writeln (cmdunit, "\r\n");
}

/* 
*/

/*
   ============================================================================
	cx_crsh -- process the crash command
   ============================================================================
*/

int
cx_crsh ()
{
  if (!wzcrsh)
    printf ("** Crash switch ! set **\r\n");

  redo = FALSE;
  showcr ();
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	bphit -- clear breakpoints, see if any were hit
   ============================================================================
*/

int
bphit ()
{
  int rc;

  rc = FALSE;

  if ((char *) p_ba0 == regptr->reg_pc)
    {

      if (*p_ba0 == BPINST)
	{

	  *p_ba0 = p_bv0;
	  p_ba0 = 0L;
	  p_bv0 = BPINST;
	  rc = TRUE;

	}
      else
	{

	  writeln (cmdunit, "** Breakpoint word at ");
	  puthn (p_ba0, 8, cmdunit);
	  writeln (cmdunit, " was ");
	  puthn (0xFFFFL & (long) (*p_ba0), 4, cmdunit);
	  writeln (cmdunit, " instead of ");
	  puthn ((long) BPINST, 4, cmdunit);
	  writeln (cmdunit, " **\r\n\n");
	  rc = TRUE;
	}
    }

/* 
*/

  if ((char *) p_ba1 == regptr->reg_pc)
    {

      if (*p_ba1 == BPINST)
	{

	  *p_ba1 = p_bv1;
	  p_ba1 = 0L;
	  p_bv1 = BPINST;
	  rc = TRUE;

	}
      else
	{

	  writeln (cmdunit, "** Breakpoint word at ");
	  puthn ((long) p_ba0, 8, cmdunit);
	  writeln (cmdunit, " was ");
	  puthn (0xFFFFL & (long) (*p_ba1), 4, cmdunit);
	  writeln (cmdunit, " instead of ");
	  puthn ((long) BPINST, 4, cmdunit);
	  writeln (cmdunit, " **\r\n\n");
	  rc = TRUE;
	}
    }
/* 
*/
  if (p_ba0)
    {

      if (*p_ba0 == BPINST)
	{

	  *p_ba0 = p_bv0;
	  p_ba0 = 0L;
	  p_bv0 = BPINST;
	  rc = TRUE;

	}
      else
	{

	  writeln (cmdunit, "** Breakpoint word at ");
	  puthn (p_ba0, 8, cmdunit);
	  writeln (cmdunit, " was ");
	  puthn (0xFFFFL & (long) (*p_ba0), 4, cmdunit);
	  writeln (cmdunit, " instead of ");
	  puthn ((long) BPINST, 4, cmdunit);
	  writeln (cmdunit, " **\r\n\n");
	  rc = TRUE;
	}
    }

/* 
*/

  if (p_ba1)
    {

      if (*p_ba1 == BPINST)
	{

	  *p_ba1 = p_bv1;
	  p_ba1 = 0L;
	  p_bv1 = BPINST;
	  rc = TRUE;

	}
      else
	{

	  writeln (cmdunit, "** Breakpoint word at ");
	  puthn ((long) p_ba0, 8, cmdunit);
	  writeln (cmdunit, " was ");
	  puthn (0xFFFFL & (long) (*p_ba1), 4, cmdunit);
	  writeln (cmdunit, " instead of ");
	  puthn ((long) BPINST, 4, cmdunit);
	  writeln (cmdunit, " **\r\n\n");
	  rc = TRUE;
	}
    }

  if (rc)
    return (rc);

  writeln (cmdunit, "\r\n** Program invoked ROMP trap **\r\n");
  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	cx_regs() -- process the regs command
   ============================================================================
*/

int
cx_regs ()
{
  showrs (regptr);
  return (TRUE);
}

/* 
*/

/*
   ============================================================================
	rompbp() -- process a breakpoint for ROMP
   ============================================================================
*/

rompbp (d0, d1, d2, d3, d4, d5, d6, d7, a0, a1, a2, a3, a4, a5, a6, a7, sr0,
	sr, pc)
     long d0, d1, d2, d3, d4, d5, d6, d7;
     char *a0, *a1, *a2, *a3, *a4, *a5, *a6, *a7, *pc;
     UWORD16 sr0, sr;
{
  register int i;

  regptr = (struct regs *) &d0;	/* make registers accessable */
  pc -= 2L;			/* adjust pc */

  if (-1 == setipl (iplev))	/* enable interrupts */
    writeln (cmdunit, "\r\n\n***** setipl() failed *****\r\n\n");

  if (first1)
    {				/* initial entry from xtrap15() */

      for (i = 0; i < 8; i++)	/* clear d0..d7 */
	regptr->d_reg[i] = 0L;

      for (i = 0; i < 7; i++)	/* clear a0..a6 */
	regptr->a_reg[i] = (char *) 0L;

      regptr->a_reg[7] = ISTACK;	/* setup initial stack */

      sr = INITSR;		/* setup sr */
      pc += 2L;			/* adjust pc past TRAP 15 */

    }
  else
    {				/* breakpoint */

      writeln (cmdunit, "\r\n\n** ROMP Breakpoint TRAP **\r\n");

      showrs (regptr);		/* show registers */

      if (bphit ()EQ FALSE)	/* fixup breakpoints */
	pc += 2L;		/* and maybe the pc */

    }

  first1 = FALSE;		/* not first time any more */
  exflag = FALSE;		/* clear exit flag */

  do
    {

      do_cmd ();		/* process commands ... */

    }
  while (!exflag);		/* ... until exit flag is set */

  return;			/* return to xtrap15 */
}

/* 
*/

/*
   ============================================================================
	progid() -- identify the program
   ============================================================================
*/

progid ()
{
  register char *pcptr;

  writeln (cmdunit, "\r\n\nBuchla 700 Multi-Tasking BIOS / Debug PROM\r\n");
  writeln (cmdunit, "  Multi-Tasking ROMP Version ");
  writeln (cmdunit, ROMPVER);
  writeln (cmdunit, "\r\n  Multi-Tasking BIOS Version ");
  pcptr = (char *) PRM_VERS;
  putn ((long) *pcptr++, 2, cmdunit);
  BIOS (B_PUTC, cmdunit, '.');
  putn ((long) *pcptr++, 2, cmdunit);
  writeln (cmdunit, promdate);
}

/* 
*/

/*
   ============================================================================
	pclr() -- clear the panel FIFO
   ============================================================================
*/

short
pclr ()
{
  register short i;

  DB_ENTR ("pclr");
  ftimer = FIFOLIM;

  while (XBIOS (X_APICHK))
    {

      afi = XBIOS (X_ANALOG);	/* check panel inputs */
      asig = 0x007F & (afi >> 8);	/* signal number */

      if (0 == asig)
	{

	  /* all keys up */

	  for (i = 0; i < 128; i++)
	    {

	      sigtab[i][0] = 0;
	      sigtab[i][1] = 0;
	    }

	  break;
	}

      if (ftimer-- < 0)
	{

	  DB_EXIT ("pclr - timed out");
	  return (FAILURE);
	}
    }

  DB_EXIT ("pclr - panel clear");
  return (SUCCESS);
}

/* 
*/

/*
   ============================================================================
	pscan() -- scan the panel and maybe even load a program from diskette
   ============================================================================
*/

short
pscan ()
{
  register short i, c;

  DB_ENTR ("pscan");

  if (0 >= ledcntr--)
    {

      if ((baseled + 3) > 23)	/* turn on a LED */
	io_leds = baseled - 21;
      else
	io_leds = baseled + 3;

      io_leds = 0x80 + baseled;	/* turn off a LED */

      if (++baseled > 23)	/* update LED number */
	baseled = 0;

      ledcntr = LED_TIME;
    }

  DB_CMNT ("pscan - checking panel");

  aflag = FALSE;

  if (XBIOS (X_APICHK))
    {				/* check panel inputs */

      afi = XBIOS (X_ANALOG);	/* get the signal */
      asig = 0x007F & (afi >> 8);	/* signal number */
      astat = 0x0001 & (afi >> 7);	/* status */
      aval = 0x007F & afi;	/* value */

      if (asig)
	{			/* active signal */

	  aflag = TRUE;

	  sigtab[asig][0] = aval;
	  sigtab[asig][1] = astat;

	}
      else
	{			/* all keys up */

	  aflag = FALSE;

	  for (i = 0; i < 128; i++)
	    sigtab[i][1] = 0;
	}
    }
/* 
*/
  if (aflag)
    {				/* anything changed ? */

      if (astat && (asig == BOOTKEY))
	{			/* BOOT key */

	  DB_CMNT ("pscan - booting");

	  for (i = 0; i < 24; i++)	/* turn off LEDs */
	    io_leds = 0x80 + i;

	  /* load and run BOOTFILE */

	  B_log_s = FALSE;
	  B_dbg_s = FALSE;

	  hdvini ();
	  _bpbin = FALSE;

	  if (booter (BOOTFILE, 0L))
	    {

	      DB_EXIT ("pscan - boot failed");
	      return (FALSE);

	    }
	  else
	    {

	      DB_CMNT ("pscan - initializing MIDAS TCB");

	      if ((struct _mt_def *) NIL == _MT_)
		_MT_ = (struct _mt_def *) XBIOS (X_MTDEFS);

	      MTSetT (&ipltcb, MTID (), BOOT_PRI, 0x2200, -1L,
		      ISTACK, B_buf_a, _MT_);

	      DB_CMNT ("pscan - swapping to midas");

	      MTSwap ();
	      SMWait (&SemQuit);

	      DB_EXIT ("pscan - returned from MIDAS");
	      return (FALSE);
	    }

	}
      else if (astat && (asig == ROMPKEY))
	{			/* ROMP key */

	  for (i = 0; i < 24; i++)	/* turn off LEDs */
	    io_leds = 0x80 + i;

	  DB_EXIT ("pscan - ROMP selected - panel");
	  return (TRUE);
	}
    }

/* 
*/

  DB_CMNT ("pscan - checking SIO");

  if (BIOS (B_RDAV, CON_DEV))
    {

      c = 0x007F & BIOS (B_GETC, CON_DEV);

      if ((c == 'r') || (c == 'R'))
	{

	  for (i = 0; i < 24; i++)	/* turn off LEDs */
	    io_leds = 0x80 + i;

	  DB_EXIT ("pscan - ROMP selected - SIO");
	  return (TRUE);
	}
    }

  DB_EXIT ("pscan - no inputs");
  return (FALSE);
}

/* 
*/

/*
   ============================================================================
	mtsetup() -- setup for multi-tasking
   ============================================================================
*/

mtsetup ()
{
  DB_ENTR ("mtsetup");

  _MT_ = (struct _mt_def *) XBIOS (X_MTDEFS);	/* intialize _MT_ */

  MTInit ();			/* initialize Multi-Tasker */
  MBInit (&MSG_Vid);		/* initialize video mailbox */
  SMInit (&SemQuit, 0L);	/* initialize termination semaphore */

  /* setup interrupt level 5 (serial I/O) handler */

  MTSetT (MT_ITT[5], MTID (), 0x8005, 0x2500, -1L,
	  &stax[4][STKLEN], _MTInt5, 0L);

  DB_CMNT ("mtsetup - level 5");

  /* setup interrupt level 4 (timer) handler */

  MTSetT (MT_ITT[4], MTID (), 0x8004, 0x2400, -1L,
	  &stax[3][STKLEN], _MTInt4, 0L);

  DB_CMNT ("mtsetup - level 4");

  /* setup interrupt level 3 (panel) handler */

  MTSetT (MT_ITT[3], MTID (), 0x8003, 0x2300, -1L,
	  &stax[2][STKLEN], _MTInt3, 0L);

  DB_CMNT ("mtsetup - level 3");

  /* setup interrupt level 2 (FPU) handler */

  MTSetT (MT_ITT[2], MTID (), 0x8002, 0x2200, -1L,
	  &stax[1][STKLEN], _MTInt2, 0L);

  DB_CMNT ("mtsetup - level 2");

  /* setup interrupt level 1 (Video) handler */

  MTSetT (MT_ITT[1], MTID (), 0x8001, 0x2100, -1L,
	  &stax[0][STKLEN], _MTInt1, 0L);

  DB_CMNT ("mtsetup - level 1");

  DB_EXIT ("mtsetup");
}

/* 
*/

/*
   ============================================================================
	put_ID() -- identify the firmware on the LCD
   ============================================================================
*/

put_ID ()
{
  DB_ENTR ("put_ID");
  GLCinit ();			/* reset LCD display */
  GLCcurs (G_ON);

  GLCtext (0, 1, "Load  GoTo");
  GLCtext (1, 1, "Disk  ROMP");

  GLCtext (3, 10, "Buchla 700 -- System Firmware by D.N. Lynx Crowe");

  sprintf (idbuf, "Multi-Tasking BIOS -- Version %02d.%02d%s",
	   *(char *) PRM_VERS, *(char *) (PRM_VERS + 1), promdate);

  GLCtext (5, 10, idbuf);

  sprintf (idbuf, "Multi-Tasking ROMP -- Version %s", ROMPVER);

  GLCtext (6, 10, idbuf);

  GLCcrc (0, 0);
  GLCcurs (G_OFF);

  DB_EXIT ("put_ID");
}

/* 
*/

/*
   ============================================================================
	ROMP --  main routine
   ============================================================================
*/

main ()
{
  register short i;
  register char *pdptr, *pcptr;

  DB_ENTR ("main");
  baseled = 21;			/* setup LED scan */
  ledcntr = 0;			/* ... */
  io_leds = 0x9F;		/* turn on LCD lamp */

  /* unpack PROM date */

  pcptr = (char *) PRM_DATE;	/* prom date: yyyymmdd */
  pdptr = promdate;		/*  -- yyyy-mm-dd */
  *pdptr++ = ' ';
  *pdptr++ = '-';
  *pdptr++ = '-';
  *pdptr++ = ' ';
  *pdptr++ = ((*pcptr >> 4) & 0x0F) + '0';
  *pdptr++ = (*pcptr++ & 0x0F) + '0';
  *pdptr++ = ((*pcptr >> 4) & 0x0F) + '0';
  *pdptr++ = (*pcptr++ & 0x0F) + '0';
  *pdptr++ = '-';
  *pdptr++ = ((*pcptr >> 4) & 0x0F) + '0';
  *pdptr++ = (*pcptr++ & 0x0F) + '0';
  *pdptr++ = '-';
  *pdptr++ = ((*pcptr >> 4) & 0x0F) + '0';
  *pdptr++ = (*pcptr++ & 0x0F) + '0';
  *pdptr++ = '\0';

  /* initialize variables */

  sprintf (hs_mtst, "[[start=$%lX],[end=$%lx]]  (or $8..$%lX)",
	   USER_RAM, RAM_TOP, (USER_RAM - 2L));

  cmdunit = CON_DEV;

  ilast = 0;
  inext = 0;
  iplev = DEFIPL;

  dflag = FALSE;
  exflag = FALSE;
  redo = FALSE;
  goflag = FALSE;
  b0flag = FALSE;
  b1flag = FALSE;

  p_goto = (char *) ROMADDR;
  p_len = 0L;
  p_width = 16L;
  p_value = 0L;

  p_ba0 = 0L;
  p_bv0 = BPINST;

  p_ba1 = 0L;
  p_bv1 = BPINST;

  tba0 = 0L;
  tba1 = 0L;

  inext = 0;

  put_ID ();

  BIOS (B_SETV, 47, trap15);	/* set ROMP trap vec */

  for (i = 0; i < 128; i++)
    {				/* clear analog I/O status */

      sigtab[i][0] = 0;
      sigtab[i][1] = 0;
    }

  mtsetup ();			/* setup multi-tasking */

  DB_CMNT ("main - TCBs setup - starting multitasker");
  MTSwap ();

  DB_CMNT ("main - clearing panel FIFO");
  XBIOS (X_CLRAFI);		/* clear the panel FIFO */

  DB_CMNT ("main - turning on ints");
  setipl (KB_EI);		/* enable interrupts */

  DB_CMNT ("main - emptying panel FIFO");
  pclr ();			/* empty the panel FIFO */

  DB_CMNT ("main - clearing panel FIFO");
  XBIOS (X_CLRAFI);		/* clear the panel FIFO */

  DB_CMNT ("main - scanning panel");
  while (FALSE == pscan ());	/* do the panel scan */

/* 
*/
  progid ();			/* identify the program */

  writeln (cmdunit, "\r\n\n");

  /* process commands */

  first1 = TRUE;		/* set break init flag */

  while (TRUE)
    {

      xtrap15 ();		/* trap into ROMP bp processor */
      writeln (cmdunit, "\r\n** xtrap15() returned to ROMP **\r\n\n");
    }
}
