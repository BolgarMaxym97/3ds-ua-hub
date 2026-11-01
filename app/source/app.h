/* The Hub's state and its screens (design: https://claude.ai/artifact/FE7cw2U38XgMqKk3mu7ftn). */
#ifndef APP_H
#define APP_H

#include <3ds.h>
#include <stdbool.h>

#include "core/record.h"
#include "errors.h"
#include "hub.h"
#include "hublog.h"
#include "job.h"
#include "nav.h"
#include "resolve.h"
#include "stats.h"
#include "view.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

/* The letters are the design's boards: A start, B catalog, C a translation's page, D installing a
 * patch, E a CIA, F removing, G errors, H help. */
typedef enum {
    SCREEN_SPLASH,   /* A1 */
    SCREEN_CHECK,    /* A2: the console check, only when something is wrong */
    SCREEN_MESSAGE,  /* A3, A4 and a broken catalog: a full-screen explanation */
    SCREEN_CATALOG,  /* B1-B4 */
    SCREEN_FILTER,   /* B5 */
    SCREEN_DETAILS,  /* C1-C6 */
    SCREEN_PROGRESS, /* D1, D2, E1: a job under way */
    SCREEN_DONE,     /* D3, E2, F3 */
    SCREEN_NOTFOUND, /* D4 */
    SCREEN_WHICH,    /* "Для якої гри?": games of several regions on the console */
    SCREEN_PICK,     /* D5-D8: the game picked by hand */
    SCREEN_REMOVE,   /* F1, F2 */
    SCREEN_ERROR,    /* G1-G3, and INTERRUPTED at start */
    SCREEN_HELP,     /* H1 */
    SCREEN_LOG,      /* H2 */
    SCREEN_GALLERY,  /* I2, I3: a translation's screenshots */
    SCREEN_NOGAME,   /* J1: a patch put on with no game, for which of its games */
    SCREEN_FALLBACK, /* J4: the patch's game from the catalog, the link checked first */
    SCREEN_QR,       /* J8: the game's page as a QR code, when the Hub cannot fetch the game */
    SCREEN_COUNT,
} ScreenId;

typedef struct {
    u32 down;           /* keys pressed this frame, A already turned into a tap */
    u32 held;           /* keys held down */
    bool tapped;        /* the stylus left the bottom screen without dragging, or A pressed the focus... */
    int tap_x, tap_y;   /* ...here */
    bool touching;      /* the stylus is down... */
    int touch_x, touch_y;
    int drag_dy;        /* ...and moved this many pixels down since the last frame */
} Input;

/* One line of "Перед встановленням" (C1-C6). */
typedef enum { MARK_OK, MARK_WARN, MARK_BAD } Mark;
typedef struct {
    Mark mark;
    char label[48];
    char value[96];
} CheckRow;

#define CHECK_ROWS 5

/* What a translation's page knows about it on this console: worked out when the page opens and
 * again after anything changes (the games scanned, a job finished, "Перевірити знову"). */
typedef struct {
    const Record *rec;      /* what the Hub installed of it, or NULL */
    ItemState state;
    MatchResult match;      /* a patch: its game on the console */
    Match m;
    int variant;            /* what Install would install, -1: nothing fits */
    int title;              /* its game in hub.titles, -1: none (a CIA, or not found) */
    CheckRow rows[CHECK_ROWS];
    int n_rows;
    ErrorCode blocked;      /* ERR_NONE: Install can go ahead */
    char blocked_detail[96];
    bool scanning;          /* the games are still being looked for */
    bool chosen;            /* the game is the one picked on "Для якої гри?" */
    bool waiting;           /* a patch installed whose game is not on the console (J3) */
    int fallback;           /* the variant whose game the catalog can give (D4, J4), -1: none */
    int qr;                 /* the variant whose game page the catalog names (D4, J8), -1: none */
} Checks;

typedef struct {
    ScreenId screen;
    ScreenId back;      /* where B leads from Help, Filter and Log */
    int focus;          /* the control the D-pad stands on */
    bool keys_nav;      /* the D-pad is in use: show where the focus is */
    bool quit;
    u64 entered_at;     /* osGetTime() when this screen opened */

    Hub hub;
    ViewOpts opts;
    ItemState *states;  /* one per catalog entry */
    int *list;          /* catalog indices the current tab shows */
    int list_n;
    int sel;            /* the highlighted row in `list` */
    int scroll;         /* the first row on screen */
    int page;           /* the catalog entry SCREEN_DETAILS shows */
    int shot;           /* its screenshot SCREEN_GALLERY shows... */
    int shot_turn;      /* ...turned this many quarters clockwise (Y), for a console held on its side */

    Record *records;    /* what the Hub has installed (installed/<slug>.txt) */
    int n_records;
    Checks checks;      /* of `page` */

    Job job;            /* the last job, its outcome kept for the screens after it */
    bool job_active;    /* started and not yet taken over by app_update() */
    struct {            /* the last app_install(), for "Спробувати знову" */
        int variant, title;
        bool manual, risky, nogame, fallback;
    } last;
    bool remove_game;   /* F1: the game the Hub installed for the patch goes too */
    int nogame_sel;     /* J1: the variant picked */
    char qr_why[96];    /* J8 opened because the direct link failed: what was wrong, empty otherwise */
    bool job_update;    /* it replaced an older version... */
    char job_was[16];   /* ...this one */
    char job_title[96];
    u32 freed;          /* a removal: the bytes it gave back */
    bool had_backup;    /* another mod was set aside for it (installed) or put back (removed) */
    u64 hold_from;      /* F1: when the hold on "Видалити" began, 0: not held */
    bool hold_ready;    /* F1: X and A were let go since the screen opened (the X that opened it is no hold) */

    ErrorCode error;    /* SCREEN_ERROR */
    char error_detail[128];
    const char *error_stage;
    ScreenId error_back;
    bool error_of_job;  /* the job's details (Y on the error screen) belong to this error */
    char report[48];    /* "вимкнено в Допомозі"... - app_report_text() says it */
    bool report_sent;   /* the report went off: its fate is the stats thread's */

    /* "Для якої гри?": the variant picked for a translation whose games of several regions are here */
    char which_slug[64];
    int which_variant;
    int which_sel;

    /* D5-D8 */
    int *picks;         /* indices into hub.titles the list shows */
    int n_picks;
    int pick_sel, pick_scroll;
    char query[32];
    int checked;        /* the title the last check was for, -1: none yet */

    LogEntry *log;      /* H2 */
    int log_n, log_sel, log_scroll;

    Stats stats;
    RecoverResult recovered;    /* a power cut stopped the last job, and what was done about it: */
    char recovered_slug[64];    /* said once the catalog is up (it knows the title) */
    bool recovered_told;
} App;

/* One screen: what it draws on each display, what it does with input, the controls the D-pad
 * walks (the first is the one A presses without moving), and, for lists, its own D-pad. */
typedef struct {
    void (*enter)(App *app);
    void (*update)(App *app, const Input *in);
    void (*draw_top)(const App *app);
    void (*draw_bottom)(const App *app);
    int (*targets)(const App *app, Target *out);
    bool (*dpad)(App *app, NavDir dir); /* true: handled, the generic focus move is skipped */
} Screen;

/* A2: anything on the console that stops the Hub working (Luma, the SD card, AM, the battery)? */
bool console_check_needed(App *app);
/* The catalog after start (A1, A2), with the notice of a power cut first if there was one. */
void app_open_catalog(App *app);

extern const Screen screen_splash, screen_check, screen_message, screen_catalog, screen_filter, screen_details, screen_progress,
    screen_done, screen_notfound, screen_which, screen_pick, screen_remove, screen_error, screen_help, screen_log, screen_gallery,
    screen_nogame, screen_fallback, screen_qr;

void app_init(App *app);
void app_exit(App *app);
void app_go(App *app, ScreenId screen);
/* Rebuilds the list after the catalog, the tab or the filters changed; keeps the selection
 * on the same entry when it is still there. */
void app_rebuild_list(App *app);
/* Raw input from main.c; turns the D-pad and A into focus moves and taps. */
void app_update(App *app, const Input *raw);
void app_draw_top(const App *app);
void app_draw_bottom(const App *app);

/* The record of a catalog entry, or NULL when the Hub has not installed it. */
const Record *app_record(const App *app, const char *slug);
/* Works out app->checks for the page open now. */
void app_check(App *app);
/* Starts a job on the page's entry and opens the progress screen. `title` is the game in
 * hub.titles a patch goes to (-1 for a CIA). */
void app_install(App *app, int variant, int title, bool manual, bool risky);
/* A patch with no game on the console (J1): the files go to luma/titles/<the variant's TID> and wait. */
void app_install_nogame(App *app, int variant);
/* A patch whose game the catalog gives (J4, J5): the game first, then the patch. */
void app_install_fallback(App *app, int variant);
/* JOB_PROBE: does the link to `variant`'s game work? (J4) */
void app_probe(App *app, int variant);
/* Is this patch's game missing from the console (J3)? */
bool app_waiting(const App *app, const Record *rec);
void app_remove(App *app);
/* JOB_CHECK for a game picked by hand: the archive of `variant` against `title`'s files. */
void app_check_game(App *app, int variant, int title);
/* Opens SCREEN_ERROR for `code` that happened outside a job (G1 before installing). */
/* The job's error with its technical side, one line: the report's detail. */
/* "надіслано · ID K7QM-29XD", "надсилаю…", "вимкнено в Допомозі": the last report, as it stands now. */
const char *app_report_text(const App *app, char *out, size_t size);
void app_job_tech(const Job *job, char *out, size_t size);
void app_error(App *app, ErrorCode code, const char *detail);
/* The last job again, after "Спробувати знову" / "Продовжити". */
void app_retry(App *app);
/* Closes the app and starts the game. */
void app_launch(App *app, u64 title_id, u8 media);
/* Restarts the console (Luma reads its config.ini again), once the report has gone. */
void app_reboot(App *app);
/* The statistics' not_found: which translations people have no game for (D4). */
void app_report_not_found(App *app);
/* "New 3DS XL", or NULL. */
const char *app_model_name(void);
/* Looks for the games on the console again (D4 "Шукати знову"). */
void app_rescan(App *app);

#endif
