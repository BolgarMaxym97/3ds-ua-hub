/*
 * One operation on a thread of its own, while a screen shows its progress: installing or
 * removing a translation, or checking a game picked by hand against a patch's check.txt.
 */
#ifndef JOB_H
#define JOB_H

#include <3ds.h>
#include <stdbool.h>

#include "catalog.h"
#include "core/patch.h"
#include "core/record.h"
#include "errors.h"
#include "net.h"

typedef enum { JOB_INSTALL, JOB_REMOVE, JOB_CHECK } JobKind;
/* JS_DATA, JS_DATA_COPY: a CIA that keeps its game on the SD card fetches and unpacks that after
 * the CIA itself (CatVariant.data); removing one takes the files off in JS_DATA_COPY. */
typedef enum { JS_CHECK, JS_DOWNLOAD, JS_VERIFY, JS_INSTALL, JS_DATA, JS_DATA_COPY, JS_LUMA, JS_DONE } JobStage;

#define LUMA_MIN_GAMES SYSTEM_VERSION(8, 0, 0) /* LayeredFS for games came with Luma3DS v8.0 */

typedef struct {
    /* what to do - set before job_start() */
    JobKind kind;
    Kind item_kind;
    char slug[64], title[96], version[16];
    CatVariant variant;     /* the file and its region */
    u64 target_tid;         /* the game it goes to: the variant's, or one picked by hand */
    u8 media;               /* of that game, for the check */
    bool manual, risky;
    Record removing;        /* JOB_REMOVE: the record of what goes; JOB_INSTALL with `replacing`: the
                             * same translation on another game, taken off first (its record would
                             * otherwise be overwritten and its files left behind for good) */
    bool replacing;

    /* while it runs */
    volatile JobStage stage;
    NetProgress net;
    PatchProgress patch;
    volatile bool running;
    u64 started, finished;  /* osGetTime() */

    /* the outcome */
    ErrorCode error;
    char detail[128];       /* for the error text's %s */
    /* "Деталі для звіту" (Y on the error screen) and the report: the technical side */
    Result sys_result;      /* the system call that failed (AM, FS), 0 if none */
    char where[160];        /* the file or address it failed on */
    char tech[96];          /* anything else that tells what happened (a hash, a result) */
    bool cancelled;
    JobStage failed_at;     /* where an error happened (the error screen and the report) */
    bool needs_reboot;      /* game patching was off in Luma: config.ini now has it on, a restart applies it */
    bool luma_by_hand;      /* ...and config.ini could not be edited: the person turns it on in Luma's menu */
    int check_matched, check_total;
    bool check_has;         /* the archive has a check.txt */

    Thread thread;
} Job;

void job_start(Job *job);
/* True once the job has finished (and its thread is gone). */
bool job_poll(Job *job);
void job_cancel(Job *job);
/* Drops what a broken-off download left in downloads/ (G2 "Скасувати встановлення"): the patch,
 * or a CIA's data. */
void job_discard_download(const Job *job);

/* Turns enable_game_patching on in luma/config.ini (exactly that byte). False when the file is
 * not one this can edit: then the person does it in Luma's menu. */
bool job_enable_game_patching(void);
/* Luma's version, or 0 without Luma. */
u32 job_luma_version(void);
bool job_game_patching_on(void);
/* Azahar (and Citra before it) answer svcGetSystemInfo(0x20000, 0) with a non-zero: no Luma there,
 * and none needed to look at the screens. */
bool job_is_emulator(void);
/* Free space on the SD card. */
u64 job_sd_free(void);

#endif
