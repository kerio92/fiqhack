/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "dlb.h"
#ifdef SHORT_FILENAMES
#include "patchlev.h"
#else
#include "patchlevel.h"
#endif

/*
 * Updating in place can leave junk at the end of the file in some
 * circumstances (if it shrinks and the O.S. doesn't have a straightforward
 * way to truncate it).  The trailing junk is harmless and the code
 * which reads the scores will ignore it.
 */
#ifdef UPDATE_RECORD_IN_PLACE
static long final_fpos;
#endif

#define done_stopprint program_state.stopprint

#define newttentry() malloc(sizeof(struct toptenentry))
#define dealloc_ttentry(ttent) free((ttent))
#define NAMSZ	10
#define DTHSZ	100
#define ROLESZ   3
#define PERSMAX	 3		/* entries per name/uid per char. allowed */
#define POINTSMIN	1	/* must be > 0 */
#define ENTRYMAX	100	/* must be >= 10 */

#if !defined(WIN32)
#define PERS_IS_UID		/* delete for PERSMAX per name; now per uid */
#endif
struct toptenentry {
	struct toptenentry *tt_next;
#ifdef UPDATE_RECORD_IN_PLACE
	long fpos;
#endif
	long points;
	int deathdnum, deathlev;
	int maxlvl, hp, maxhp, deaths;
	int ver_major, ver_minor, patchlevel;
	long deathdate, birthdate;
	int uid;
	char plrole[ROLESZ+1];
	char plrace[ROLESZ+1];
	char plgend[ROLESZ+1];
	char plalign[ROLESZ+1];
	char name[NAMSZ+1];
	char death[DTHSZ+1];
} *tt_head;

static void topten_print(const char *);
static void topten_print_bold(const char *);
static xchar observable_depth(d_level *);
static void outheader(void);
static void outentry(int,struct toptenentry *,boolean);
static void readentry(FILE *,struct toptenentry *);
static void writeentry(FILE *,struct toptenentry *);
static void free_ttlist(struct toptenentry *);
static int classmon(char *,boolean);
static int score_wanted(boolean, int,struct toptenentry *,
			int,const char **,int);

/* must fit with end.c; used in rip.c */
const char * const killed_by_prefix[] = {
	"killed by ", "choked on ", "poisoned by ", "died of ", "drowned in ",
	"burned by ", "dissolved in ", "crushed to death by ", "petrified by ",
	"turned to slime by ", "killed by ", "", "", "", "", ""
};

static struct menulist ttmenu = {NULL, 0, 0};

static void topten_print(const char *x)
{
	if (ttmenu.size == 0)
	    raw_print(x);
	else
	    add_menutext(&ttmenu, x);
}

static void topten_print_bold(const char *x)
{
	if (ttmenu.size == 0)
	    raw_print_bold(x);
	else
	    add_menuheading(&ttmenu, x);
}

static xchar observable_depth(d_level *lev)
{
	return depth(lev);
}

static void readentry(FILE *rfile, struct toptenentry *tt)
{
	static const char fmt[] = "%d.%d.%d %ld %d %d %d %d %d %d %ld %ld %d ";
	static const char fmt32[] = "%c%c %[^,],%[^\n]%*c";
	static const char fmt33[] = "%s %s %s %s %[^,],%[^\n]%*c";

#ifdef UPDATE_RECORD_IN_PLACE
	/* note: fscanf() below must read the record's terminating newline */
	final_fpos = tt->fpos = ftell(rfile);
#endif
#define TTFIELDS 13
	if(fscanf(rfile, fmt,
			&tt->ver_major, &tt->ver_minor, &tt->patchlevel,
			&tt->points, &tt->deathdnum, &tt->deathlev,
			&tt->maxlvl, &tt->hp, &tt->maxhp, &tt->deaths,
			&tt->deathdate, &tt->birthdate,
			&tt->uid) != TTFIELDS)
#undef TTFIELDS
		tt->points = 0;
	else {
		/* Check for backwards compatibility */
		if (tt->ver_major < 3 ||
				(tt->ver_major == 3 && tt->ver_minor < 3)) {
			int i;

		    if (fscanf(rfile, fmt32,
				tt->plrole, tt->plgend,
				tt->name, tt->death) != 4)
			tt->points = 0;
		    tt->plrole[1] = '\0';
		    if ((i = nh_str2role(tt->plrole)) >= 0)
			strcpy(tt->plrole, roles[i].filecode);
		    strcpy(tt->plrace, "?");
		    strcpy(tt->plgend, (tt->plgend[0] == 'M') ? "Mal" : "Fem");
		    strcpy(tt->plalign, "?");
		} else if (fscanf(rfile, fmt33,
				tt->plrole, tt->plrace, tt->plgend,
				tt->plalign, tt->name, tt->death) != 6)
			tt->points = 0;
	}

	/* check old score entries for Y2K problem and fix whenever found */
	if (tt->points > 0) {
		if (tt->birthdate < 19000000L) tt->birthdate += 19000000L;
		if (tt->deathdate < 19000000L) tt->deathdate += 19000000L;
	}
}

static void writeentry(FILE *rfile, struct toptenentry *tt)
{
	fprintf(rfile,"%d.%d.%d %ld %d %d %d %d %d %d %ld %ld %d ",
		tt->ver_major, tt->ver_minor, tt->patchlevel,
		tt->points, tt->deathdnum, tt->deathlev,
		tt->maxlvl, tt->hp, tt->maxhp, tt->deaths,
		tt->deathdate, tt->birthdate, tt->uid);
	if (tt->ver_major < 3 ||
			(tt->ver_major == 3 && tt->ver_minor < 3))
		fprintf(rfile,"%c%c %s,%s\n",
			tt->plrole[0], tt->plgend[0],
			onlyspace(tt->name) ? "_" : tt->name, tt->death);
	else
		fprintf(rfile,"%s %s %s %s %s,%s\n",
			tt->plrole, tt->plrace, tt->plgend, tt->plalign,
			onlyspace(tt->name) ? "_" : tt->name, tt->death);
}

static void free_ttlist(struct toptenentry *tt)
{
	struct toptenentry *ttnext;

	while (tt->points > 0) {
		ttnext = tt->tt_next;
		dealloc_ttentry(tt);
		tt = ttnext;
	}
	dealloc_ttentry(tt);
}

void topten(int how)
{
	int uid = getuid();
	int rank, rank0 = -1, rank1 = 0;
	int occ_cnt = PERSMAX;
	struct toptenentry *t0, *tprev;
	struct toptenentry *t1;
	FILE *rfile;
	int flg = 0;
	boolean t0_used;
#ifdef LOGFILE
	FILE *lfile;
#endif /* LOGFILE */

/* If we are in the midst of a panic, cut out topten entirely.
 * topten uses alloc() several times, which will lead to
 * problems if the panic was the result of an alloc() failure.
 */
	if (program_state.panicking)
		return;

	if (flags.toptenwin) {
	    init_menulist(&ttmenu);
	}

#if defined(UNIX)
#define HUP	if (!program_state.done_hup)
#else
#define HUP
#endif
	/* create a new 'topten' entry */
	t0_used = FALSE;
	t0 = newttentry();
	/* deepest_lev_reached() is in terms of depth(), and reporting the
	 * deepest level reached in the dungeon death occurred in doesn't
	 * seem right, so we have to report the death level in depth() terms
	 * as well (which also seems reasonable since that's all the player
	 * sees on the screen anyway)
	 */
	t0->ver_major = VERSION_MAJOR;
	t0->ver_minor = VERSION_MINOR;
	t0->patchlevel = PATCHLEVEL;
	t0->points = u.urexp;
	t0->deathdnum = u.uz.dnum;
	t0->deathlev = observable_depth(&u.uz);
	t0->maxlvl = deepest_lev_reached(TRUE);
	t0->hp = u.uhp;
	t0->maxhp = u.uhpmax;
	t0->deaths = u.umortality;
	t0->uid = uid;
	strncpy(t0->plrole, urole.filecode, ROLESZ);
	t0->plrole[ROLESZ] = '\0';
	strncpy(t0->plrace, urace.filecode, ROLESZ);
	t0->plrace[ROLESZ] = '\0';
	strncpy(t0->plgend, genders[flags.female].filecode, ROLESZ);
	t0->plgend[ROLESZ] = '\0';
	strncpy(t0->plalign, aligns[1-u.ualign.type].filecode, ROLESZ);
	t0->plalign[ROLESZ] = '\0';
	strncpy(t0->name, plname, NAMSZ);
	t0->name[NAMSZ] = '\0';
	t0->death[0] = '\0';
	switch (killer_format) {
		default: impossible("bad killer format?");
		case KILLED_BY_AN:
			strcat(t0->death, killed_by_prefix[how]);
			strncat(t0->death, an(killer),
						DTHSZ-strlen(t0->death));
			break;
		case KILLED_BY:
			strcat(t0->death, killed_by_prefix[how]);
			strncat(t0->death, killer,
						DTHSZ-strlen(t0->death));
			break;
		case NO_KILLER_PREFIX:
			strncat(t0->death, killer, DTHSZ);
			break;
	}
	t0->birthdate = yyyymmdd(u.ubirthday);
	t0->deathdate = yyyymmdd((time_t)0L);
	t0->tt_next = 0;
#ifdef UPDATE_RECORD_IN_PLACE
	t0->fpos = -1L;
#endif

#ifdef LOGFILE		/* used for debugging (who dies of what, where) */
	if (lock_file(LOGFILE, SCOREPREFIX, 10)) {
	    if(!(lfile = fopen_datafile(LOGFILE, "a", SCOREPREFIX))) {
		HUP raw_print("Cannot open log file!");
	    } else {
		writeentry(lfile, t0);
		fclose(lfile);
	    }
	    unlock_file(LOGFILE);
	}
#endif /* LOGFILE */

	if (wizard || discover) {
	    if (how != PANICKED) HUP {
		char pbuf[BUFSZ];
		topten_print("");
		sprintf(pbuf,
	      "Since you were in %s mode, the score list will not be checked.",
		    wizard ? "wizard" : "discover");
		topten_print(pbuf);
	    }
	    goto showwin;
	}

	if (!lock_file(RECORD, SCOREPREFIX, 60))
		goto destroywin;

#ifdef UPDATE_RECORD_IN_PLACE
	rfile = fopen_datafile(RECORD, "r+", SCOREPREFIX);
#else
	rfile = fopen_datafile(RECORD, "r", SCOREPREFIX);
#endif

	if (!rfile) {
		HUP raw_print("Cannot open record file!");
		unlock_file(RECORD);
		goto destroywin;
	}

	HUP topten_print("");

	/* assure minimum number of points */
	if(t0->points < POINTSMIN) t0->points = 0;

	t1 = tt_head = newttentry();
	tprev = 0;
	/* rank0: -1 undefined, 0 not_on_list, n n_th on list */
	for(rank = 1; ; ) {
	    readentry(rfile, t1);
	    if (t1->points < POINTSMIN) t1->points = 0;
	    if(rank0 < 0 && t1->points < t0->points) {
		rank0 = rank++;
		if(tprev == 0)
			tt_head = t0;
		else
			tprev->tt_next = t0;
		t0->tt_next = t1;
#ifdef UPDATE_RECORD_IN_PLACE
		t0->fpos = t1->fpos;	/* insert here */
#endif
		t0_used = TRUE;
		occ_cnt--;
		flg++;		/* ask for a rewrite */
	    } else tprev = t1;

	    if(t1->points == 0) break;
	    if(
#ifdef PERS_IS_UID
		t1->uid == t0->uid &&
#else
		strncmp(t1->name, t0->name, NAMSZ) == 0 &&
#endif
		!strncmp(t1->plrole, t0->plrole, ROLESZ) &&
		--occ_cnt <= 0) {
		    if(rank0 < 0) {
			rank0 = 0;
			rank1 = rank;
			HUP {
			    char pbuf[BUFSZ];
			    sprintf(pbuf,
			  "You didn't beat your previous score of %ld points.",
				    t1->points);
			    topten_print(pbuf);
			    topten_print("");
			}
		    }
		    if(occ_cnt < 0) {
			flg++;
			continue;
		    }
		}
	    if(rank <= ENTRYMAX) {
		t1->tt_next = newttentry();
		t1 = t1->tt_next;
		rank++;
	    }
	    if(rank > ENTRYMAX) {
		t1->points = 0;
		break;
	    }
	}
	if(flg) {	/* rewrite record file */
#ifdef UPDATE_RECORD_IN_PLACE
		fseek(rfile, (t0->fpos >= 0 ?
				     t0->fpos : final_fpos), SEEK_SET);
#else
		fclose(rfile);
		if(!(rfile = fopen_datafile(RECORD, "w", SCOREPREFIX))){
			HUP raw_print("Cannot write record file");
			unlock_file(RECORD);
			free_ttlist(tt_head);
			goto destroywin;
		}
#endif	/* UPDATE_RECORD_IN_PLACE */
		if(!done_stopprint) if(rank0 > 0){
		    if(rank0 <= 10)
			topten_print("You made the top ten list!");
		    else {
			char pbuf[BUFSZ];
			sprintf(pbuf,
			  "You reached the %d%s place on the top %d list.",
				rank0, ordin(rank0), ENTRYMAX);
			topten_print(pbuf);
		    }
		    topten_print("");
		}
	}
	if(rank0 == 0) rank0 = rank1;
	if(rank0 <= 0) rank0 = rank;
	if(!done_stopprint) outheader();
	t1 = tt_head;
	for(rank = 1; t1->points != 0; rank++, t1 = t1->tt_next) {
	    if(flg
#ifdef UPDATE_RECORD_IN_PLACE
		    && rank >= rank0
#endif
		) writeentry(rfile, t1);
	    if (done_stopprint) continue;
	    if (rank > flags.end_top &&
		    (rank < rank0 - flags.end_around ||
		     rank > rank0 + flags.end_around) &&
		    (!flags.end_own ||
#ifdef PERS_IS_UID
					t1->uid != t0->uid
#else
					strncmp(t1->name, t0->name, NAMSZ)
#endif
		)) continue;
	    if (rank == rank0 - flags.end_around &&
		    rank0 > flags.end_top + flags.end_around + 1 &&
		    !flags.end_own)
		topten_print("");
	    if(rank != rank0)
		outentry(rank, t1, FALSE);
	    else if(!rank1)
		outentry(rank, t1, TRUE);
	    else {
		outentry(rank, t1, TRUE);
		outentry(0, t0, TRUE);
	    }
	}
	if(rank0 >= rank) if(!done_stopprint)
		outentry(0, t0, TRUE);
#ifdef UPDATE_RECORD_IN_PLACE
	if (flg) {
# ifdef TRUNCATE_FILE
	    /* if a reasonable way to truncate a file exists, use it */
	    truncate_file(rfile);
# else
	    /* use sentinel record rather than relying on truncation */
	    t1->points = 0L;	/* terminates file when read back in */
	    t1->ver_major = t1->ver_minor = t1->patchlevel = 0;
	    t1->uid = t1->deathdnum = t1->deathlev = 0;
	    t1->maxlvl = t1->hp = t1->maxhp = t1->deaths = 0;
	    t1->plrole[0] = t1->plrace[0] = t1->plgend[0] = t1->plalign[0] = '-';
	    t1->plrole[1] = t1->plrace[1] = t1->plgend[1] = t1->plalign[1] = 0;
	    t1->birthdate = t1->deathdate = yyyymmdd((time_t)0L);
	    strcpy(t1->name, "@");
	    strcpy(t1->death, "<eod>\n");
	    writeentry(rfile, t1);
	    fflush(rfile);
# endif	/* TRUNCATE_FILE */
	}
#endif	/* UPDATE_RECORD_IN_PLACE */
	fclose(rfile);
	unlock_file(RECORD);
	free_ttlist(tt_head);

  showwin:
	if (flags.toptenwin && !done_stopprint)
	    display_menu(ttmenu.items, ttmenu.icount, NULL, PICK_NONE, NULL);
  destroywin:
	if (!t0_used) dealloc_ttentry(t0);
	if (flags.toptenwin) {
	    free(ttmenu.items);
	    ttmenu.items = NULL;
	    ttmenu.icount = ttmenu.size = 0;
	}
}

static void outheader(void)
{
	char linebuf[BUFSZ];
	char *bp;

	strcpy(linebuf, " No  Points     Name");
	bp = eos(linebuf);
	while(bp < linebuf + COLNO - 9) *bp++ = ' ';
	strcpy(bp, "Hp [max]");
	topten_print(linebuf);
}

/* so>0: standout line; so=0: ordinary line */
static void outentry(int rank, struct toptenentry *t1, boolean so)
{
	boolean second_line = TRUE;
	char linebuf[BUFSZ];
	char *bp, hpbuf[24], linebuf3[BUFSZ];
	int hppos, lngr;


	linebuf[0] = '\0';
	if (rank) sprintf(eos(linebuf), "%3d", rank);
	else strcat(linebuf, "   ");

	sprintf(eos(linebuf), " %10ld  %.10s", t1->points, t1->name);
	sprintf(eos(linebuf), "-%s", t1->plrole);
	if (t1->plrace[0] != '?')
		sprintf(eos(linebuf), "-%s", t1->plrace);
	/* Printing of gender and alignment is intentional.  It has been
	 * part of the NetHack Geek Code, and illustrates a proper way to
	 * specify a character from the command line.
	 */
	sprintf(eos(linebuf), "-%s", t1->plgend);
	if (t1->plalign[0] != '?')
		sprintf(eos(linebuf), "-%s ", t1->plalign);
	else
		strcat(linebuf, " ");
	if (!strncmp("escaped", t1->death, 7)) {
	    sprintf(eos(linebuf), "escaped the dungeon %s[max level %d]",
		    !strncmp(" (", t1->death + 7, 2) ? t1->death + 7 + 2 : "",
		    t1->maxlvl);
	    /* fixup for closing paren in "escaped... with...Amulet)[max..." */
	    if ((bp = index(linebuf, ')')) != 0)
		*bp = (t1->deathdnum == astral_level.dnum) ? '\0' : ' ';
	    second_line = FALSE;
	} else if (!strncmp("ascended", t1->death, 8)) {
	    sprintf(eos(linebuf), "ascended to demigod%s-hood",
		    (t1->plgend[0] == 'F') ? "dess" : "");
	    second_line = FALSE;
	} else {
	    if (!strncmp(t1->death, "quit", 4)) {
		strcat(linebuf, "quit");
		second_line = FALSE;
	    } else if (!strncmp(t1->death, "died of st", 10)) {
		strcat(linebuf, "starved to death");
		second_line = FALSE;
	    } else if (!strncmp(t1->death, "choked", 6)) {
		sprintf(eos(linebuf), "choked on h%s food",
			(t1->plgend[0] == 'F') ? "er" : "is");
	    } else if (!strncmp(t1->death, "poisoned", 8)) {
		strcat(linebuf, "was poisoned");
	    } else if (!strncmp(t1->death, "crushed", 7)) {
		strcat(linebuf, "was crushed to death");
	    } else if (!strncmp(t1->death, "petrified by ", 13)) {
		strcat(linebuf, "turned to stone");
	    } else strcat(linebuf, "died");

	    if (t1->deathdnum == astral_level.dnum) {
		const char *arg, *fmt = " on the Plane of %s";

		switch (t1->deathlev) {
		case -5:
			fmt = " on the %s Plane";
			arg = "Astral";	break;
		case -4:
			arg = "Water";	break;
		case -3:
			arg = "Fire";	break;
		case -2:
			arg = "Air";	break;
		case -1:
			arg = "Earth";	break;
		default:
			arg = "Void";	break;
		}
		sprintf(eos(linebuf), fmt, arg);
	    } else {
		sprintf(eos(linebuf), " in %s", dungeons[t1->deathdnum].dname);
		if (t1->deathdnum != knox_level.dnum)
		    sprintf(eos(linebuf), " on level %d", t1->deathlev);
		if (t1->deathlev != t1->maxlvl)
		    sprintf(eos(linebuf), " [max %d]", t1->maxlvl);
	    }

	    /* kludge for "quit while already on Charon's boat" */
	    if (!strncmp(t1->death, "quit ", 5))
		strcat(linebuf, t1->death + 4);
	}
	strcat(linebuf, ".");

	/* Quit, starved, ascended, and escaped contain no second line */
	if (second_line)
	    sprintf(eos(linebuf), "  %c%s.", highc(*(t1->death)), t1->death+1);

	lngr = (int)strlen(linebuf);
	if (t1->hp <= 0) hpbuf[0] = '-', hpbuf[1] = '\0';
	else sprintf(hpbuf, "%d", t1->hp);
	/* beginning of hp column after padding (not actually padded yet) */
	hppos = COLNO - (sizeof("  Hp [max]")-1); /* sizeof(str) includes \0 */
	while (lngr >= hppos) {
	    for(bp = eos(linebuf);
		    !(*bp == ' ' && (bp-linebuf < hppos));
		    bp--)
		;
	    /* special case: if about to wrap in the middle of maximum
	       dungeon depth reached, wrap in front of it instead */
	    if (bp > linebuf + 5 && !strncmp(bp - 5, " [max", 5)) bp -= 5;
	    strcpy(linebuf3, bp+1);
	    *bp = 0;
	    if (so) {
		while (bp < linebuf + (COLNO-1)) *bp++ = ' ';
		*bp = 0;
		topten_print_bold(linebuf);
	    } else
		topten_print(linebuf);
	    sprintf(linebuf, "%15s %s", "", linebuf3);
	    lngr = strlen(linebuf);
	}
	/* beginning of hp column not including padding */
	hppos = COLNO - 7 - (int)strlen(hpbuf);
	bp = eos(linebuf);

	if (bp <= linebuf + hppos) {
	    /* pad any necessary blanks to the hit point entry */
	    while (bp < linebuf + hppos) *bp++ = ' ';
	    strcpy(bp, hpbuf);
	    sprintf(eos(bp), " %s[%d]",
		    (t1->maxhp < 10) ? "  " : (t1->maxhp < 100) ? " " : "",
		    t1->maxhp);
	}

	if (so) {
	    bp = eos(linebuf);
	    if (so >= COLNO) so = COLNO-1;
	    while (bp < linebuf + so) *bp++ = ' ';
	    *bp = 0;
	    topten_print_bold(linebuf);
	} else
	    topten_print(linebuf);
}

static int score_wanted(boolean current_ver, int rank, struct toptenentry *t1,
			int playerct, const char **players, int uid)
{
	int i;

	if (current_ver && (t1->ver_major != VERSION_MAJOR ||
			    t1->ver_minor != VERSION_MINOR ||
			    t1->patchlevel != PATCHLEVEL))
		return 0;

#ifdef PERS_IS_UID
	if (!playerct && t1->uid == uid)
		return 1;
#endif

	for (i = 0; i < playerct; i++) {
	    if (players[i][0] == '-' && index("pr", players[i][1]) &&
                players[i][2] == 0 && i + 1 < playerct) {
		char *arg = (char *)players[i + 1];
		if ((players[i][1] == 'p' &&
		     nh_str2role(arg) == nh_str2role(t1->plrole)) ||
		    (players[i][1] == 'r' &&
		     nh_str2race(arg) == nh_str2race(t1->plrace)))
		    return 1;
		i++;
	    } else if (strcmp(players[i], "all") == 0 ||
		    strncmp(t1->name, players[i], NAMSZ) == 0 ||
		    (players[i][0] == '-' &&
		     players[i][1] == t1->plrole[0] &&
		     players[i][2] == 0) ||
		    (digit(players[i][0]) && rank <= atoi(players[i])))
		return 1;
	}
	return 0;
}

/*
 * print selected parts of score list.
 * argc >= 2, with argv[0] untrustworthy (directory names, et al.),
 * and argv[1] starting with "-s".
 */
void prscore(char *hname, int argc, char **argv)
{
	const char **players;
	int playerct, rank;
	boolean current_ver = TRUE, init_done = FALSE;
	struct toptenentry *t1;
	FILE *rfile;
	boolean match_found = FALSE;
	int i;
	char pbuf[BUFSZ];
	int uid = -1;
#ifndef PERS_IS_UID
	const char *player0;
#endif

	if (argc < 2 || strncmp(argv[1], "-s", 2)) {
		raw_printf("prscore: bad arguments (%d)", argc);
		return;
	}

	rfile = fopen_datafile(RECORD, "r", SCOREPREFIX);
	if (!rfile) {
		raw_print("Cannot open record file!");
		return;
	}

	/* If the score list isn't after a game, we never went through
	 * initialization. */
	if (wiz1_level.dlevel == 0) {
		dlb_init();
		init_dungeons();
		init_done = TRUE;
	}

	if (!argv[1][2]){	/* plain "-s" */
		argc--;
		argv++;
	} else	argv[1] += 2;

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		current_ver = FALSE;
		argc--;
		argv++;
	}

	if (argc <= 1) {
#ifdef PERS_IS_UID
		uid = getuid();
		playerct = 0;
		players = NULL;
#else
		player0 = plname;
		if (!*player0)
			player0 = "hackplayer";
		playerct = 1;
		players = &player0;
#endif
	} else {
		playerct = --argc;
		players = (const char **)++argv;
	}
	raw_print("");

	t1 = tt_head = newttentry();
	for (rank = 1; ; rank++) {
	    readentry(rfile, t1);
	    if (t1->points == 0) break;
	    if (!match_found &&
		    score_wanted(current_ver, rank, t1, playerct, players, uid))
		match_found = TRUE;
	    t1->tt_next = newttentry();
	    t1 = t1->tt_next;
	}

	fclose(rfile);
	if (init_done) {
	    free_dungeons();
	    dlb_cleanup();
	}

	if (match_found) {
	    outheader();
	    t1 = tt_head;
	    for (rank = 1; t1->points != 0; rank++, t1 = t1->tt_next) {
		if (score_wanted(current_ver, rank, t1, playerct, players, uid))
		    outentry(rank, t1, 0);
	    }
	} else {
	    sprintf(pbuf, "Cannot find any %sentries for ",
				current_ver ? "current " : "");
	    if (playerct < 1) strcat(pbuf, "you.");
	    else {
		if (playerct > 1) strcat(pbuf, "any of ");
		for (i = 0; i < playerct; i++) {
		    /* stop printing players if there are too many to fit */
		    if (strlen(pbuf) + strlen(players[i]) + 2 >= BUFSZ) {
			if (strlen(pbuf) < BUFSZ-4) strcat(pbuf, "...");
			else strcpy(pbuf+strlen(pbuf)-4, "...");
			break;
		    }
		    strcat(pbuf, players[i]);
		    if (i < playerct-1) {
			if (players[i][0] == '-' &&
			    index("pr", players[i][1]) && players[i][2] == 0)
			    strcat(pbuf, " ");
			else strcat(pbuf, ":");
		    }
		}
	    }
	    raw_print(pbuf);
	    raw_printf("Usage: %s -s [-v] <playertypes> [maxrank] [playernames]",
			 hname);
	    raw_printf("Player types are: [-p role] [-r race]");
	}
	free_ttlist(tt_head);
}

static int classmon(char *plch, boolean fem)
{
	int i;

	/* Look for this role in the role table */
	for (i = 0; roles[i].name.m; i++)
	    if (!strncmp(plch, roles[i].filecode, ROLESZ)) {
		if (fem && roles[i].femalenum != NON_PM)
		    return roles[i].femalenum;
		else if (roles[i].malenum != NON_PM)
		    return roles[i].malenum;
		else
		    return PM_HUMAN;
	    }
	/* this might be from a 3.2.x score for former Elf class */
	if (!strcmp(plch, "E")) return PM_RANGER;

	impossible("What weird role is this? (%s)", plch);
	return PM_HUMAN_MUMMY;
}

/*
 * Get a random player name and class from the high score list,
 * and attach them to an object (for statues or morgue corpses).
 */
struct obj *tt_oname(struct obj *otmp)
{
	int rank;
	int i;
	struct toptenentry *tt;
	FILE *rfile;
	struct toptenentry tt_buf;

	if (!otmp) return NULL;

	rfile = fopen_datafile(RECORD, "r", SCOREPREFIX);
	if (!rfile) {
		impossible("Cannot open record file!");
		return NULL;
	}

	tt = &tt_buf;
	rank = rnd(10);
pickentry:
	for(i = rank; i; i--) {
	    readentry(rfile, tt);
	    if(tt->points == 0) break;
	}

	if(tt->points == 0) {
		if(rank > 1) {
			rank = 1;
			rewind(rfile);
			goto pickentry;
		}
		otmp = NULL;
	} else {
		/* reset timer in case corpse started out as lizard or troll */
		if (otmp->otyp == CORPSE) obj_stop_timers(otmp);
		otmp->corpsenm = classmon(tt->plrole, (tt->plgend[0] == 'F'));
		otmp->owt = weight(otmp);
		otmp = oname(otmp, tt->name);
		if (otmp->otyp == CORPSE) start_corpse_timeout(otmp);
	}

	fclose(rfile);
	return otmp;
}

/*topten.c*/
