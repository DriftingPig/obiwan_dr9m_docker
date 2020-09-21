/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2007 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2013 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * Copyright (c) 2007      Evergrid, Inc. All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2011-2012 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

/**
 * @file
 * OPAL Restart command
 *
 * This command will restart a single process from 
 * the checkpoint generated by the opal-checkpoint 
 * command.
 */
#include "opal_config.h"

#include <stdio.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif  /*  HAVE_STDLIB_H */
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif  /* HAVE_FCNTL_H */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif  /* HAVE_STRING_H */

#include "opal/constants.h"

#include "opal/util/cmd_line.h"
#include "opal/util/argv.h"
#include "opal/util/show_help.h"
#include "opal/util/output.h"
#include "opal/util/opal_environ.h"
#include "opal/util/error.h"
#include "opal/util/basename.h"
#include "opal/mca/base/base.h"

#include "opal/runtime/opal.h"
#include "opal/runtime/opal_cr.h"

#include "opal/mca/crs/crs.h"
#include "opal/mca/crs/base/base.h"

#include "opal/mca/compress/compress.h"
#include "opal/mca/compress/base/base.h"

/******************
 * Local Functions
 ******************/
static int initialize(int argc, char *argv[]);
static int finalize(void);
static int parse_args(int argc, char *argv[]);
static int check_file(void);
static int post_env_vars(int prev_pid, opal_crs_base_snapshot_t *snapshot);

/*****************************************
 * Global Vars for Command line Arguments
 *****************************************/
static char *expected_crs_comp = NULL;

typedef struct {
    bool help;
    bool verbose;
    char *snapshot_ref;
    char *snapshot_loc;
    char *snapshot_metadata;
    char *snapshot_cache;
    char *snapshot_compress;
    char *snapshot_compress_postfix;
    int  output;
} opal_restart_globals_t;

opal_restart_globals_t opal_restart_globals;

opal_cmd_line_init_t cmd_line_opts[] = {
    { NULL,
      'h', NULL, "help", 
      0,
      &opal_restart_globals.help, OPAL_CMD_LINE_TYPE_BOOL,
      "This help message" },

    { NULL,
      'v', NULL, "verbose", 
      0,
      &opal_restart_globals.verbose, OPAL_CMD_LINE_TYPE_BOOL,
      "Be Verbose" },

    { NULL,
      'l', NULL, "location",
      1,
      &opal_restart_globals.snapshot_loc, OPAL_CMD_LINE_TYPE_STRING,
      "Full path to the location of the local snapshot."},

    { NULL,
      'm', NULL, "metadata",
      1,
      &opal_restart_globals.snapshot_metadata, OPAL_CMD_LINE_TYPE_STRING,
      "Relative path (with respect to --location) to the metadata file."},

    { NULL,
      'r', NULL, "reference",
      1,
      &opal_restart_globals.snapshot_ref, OPAL_CMD_LINE_TYPE_STRING,
      "Local snapshot reference."},

    { NULL,
      'c', NULL, "cache",
      1,
      &opal_restart_globals.snapshot_cache, OPAL_CMD_LINE_TYPE_STRING,
      "Possible local cache of the snapshot reference."},

    { NULL,
      'd', NULL, "decompress",
      1,
      &opal_restart_globals.snapshot_compress, OPAL_CMD_LINE_TYPE_STRING,
      "Decompression component to use."},

    { NULL,
      'p', NULL, "decompress_postfix",
      1,
      &opal_restart_globals.snapshot_compress_postfix, OPAL_CMD_LINE_TYPE_STRING,
      "Decompression component postfix."},

    /* End of list */
    { NULL,
      '\0', NULL, NULL, 
      0,
      NULL, OPAL_CMD_LINE_TYPE_NULL,
      NULL }
};

int
main(int argc, char *argv[])
{
    int ret, exit_status = OPAL_SUCCESS;
    int child_pid;
    int prev_pid = 0;
    opal_crs_base_snapshot_t *snapshot = NULL;
    char * tmp_env_var = NULL;

    /***************
     * Initialize
     ***************/
    if (OPAL_SUCCESS != (ret = initialize(argc, argv))) {
        exit_status = ret;
        goto cleanup;
    }
    
    /* 
     * Check for existence of the file, or program in the case of self
     */
    if( OPAL_SUCCESS != (ret = check_file() )) {
        opal_show_help("help-opal-restart.txt", "invalid_filename", true,
                       opal_restart_globals.snapshot_ref);
        exit_status = ret;
        goto cleanup;
    }

    /* Re-enable the selection of the CRS component, so we can choose the right one */
    (void) mca_base_var_env_name("crs_base_do_not_select", &tmp_env_var);
    opal_setenv(tmp_env_var,
                "0", /* turn on the selection */
                true, &environ);
    free(tmp_env_var);
    tmp_env_var = NULL;

    /*
     * Make sure we are using the correct checkpointer
     */
    if(NULL == expected_crs_comp) {
        char * full_metadata_path = NULL;
        FILE * metadata = NULL;

        asprintf(&full_metadata_path, "%s/%s/%s",
                 opal_restart_globals.snapshot_loc,
                 opal_restart_globals.snapshot_ref,
                 opal_restart_globals.snapshot_metadata);
        if( NULL == (metadata = fopen(full_metadata_path, "r")) ) {
            opal_show_help("help-opal-restart.txt", "invalid_metadata", true,
                           opal_restart_globals.snapshot_metadata,
                           full_metadata_path);
            exit_status = OPAL_ERROR;
            goto cleanup;
        }
        if( OPAL_SUCCESS != (ret = opal_crs_base_extract_expected_component(metadata,
                                                                            &expected_crs_comp,
                                                                            &prev_pid)) ) {
            opal_show_help("help-opal-restart.txt", "invalid_metadata", true,
                           opal_restart_globals.snapshot_metadata,
                           full_metadata_path);
            exit_status = ret;
            goto cleanup;
        }

        free(full_metadata_path);
        full_metadata_path = NULL;

        fclose(metadata);
        metadata = NULL;
    }
    
    opal_output_verbose(10, opal_restart_globals.output,
                        "Restart Expects checkpointer: (%s)",
                        expected_crs_comp);

    (void) mca_base_var_env_name("crs", &tmp_env_var);
    opal_setenv(tmp_env_var,
                expected_crs_comp,
                true, &environ);
    free(tmp_env_var);
    tmp_env_var = NULL;
    
    /* Select this component or don't continue.
     * If the selection of this component fails, then we can't 
     * restart on this node because it doesn't have the proper checkpointer
     * available. 
     */
    if( OPAL_SUCCESS != (ret = opal_crs_base_open()) ) {
        opal_show_help("help-opal-restart.txt", "comp_select_failure", true,
                       "crs", ret);
        exit_status = ret;
        goto cleanup;
    }

    if( OPAL_SUCCESS != (ret = opal_crs_base_select()) ) {
        opal_show_help("help-opal-restart.txt", "comp_select_failure", true,
                       expected_crs_comp, ret);
        exit_status = ret;
        goto cleanup;
    }
    
    /*
     * Make sure we have selected the proper component
     */
    if(NULL == expected_crs_comp ||
       0 != strncmp(expected_crs_comp, 
                    opal_crs_base_selected_component.base_version.mca_component_name, 
                    strlen(expected_crs_comp)) ) {
        opal_show_help("help-opal-restart.txt", "comp_select_mismatch", 
                       true,
                       expected_crs_comp, 
                       opal_crs_base_selected_component.base_version.mca_component_name,
                       ret);
        exit_status = ret;
        goto cleanup;
    }

    /******************************
     * Restart in this process
     ******************************/
    opal_output_verbose(10, opal_restart_globals.output,
                        "Restarting from file (%s)\n",
                        opal_restart_globals.snapshot_ref);

    snapshot = OBJ_NEW(opal_crs_base_snapshot_t);
    snapshot->cold_start         = true;
    asprintf(&(snapshot->snapshot_directory), "%s/%s",
             opal_restart_globals.snapshot_loc,
             opal_restart_globals.snapshot_ref);
    asprintf(&(snapshot->metadata_filename), "%s/%s",
             snapshot->snapshot_directory,
             opal_restart_globals.snapshot_metadata);

    /* Since some checkpoint/restart systems don't pass along env vars to the
     * restarted app, we need to take care of that.
     *
     * Included here is the creation of any files or directories that need to be
     * created before the process is restarted.
     */
    if(OPAL_SUCCESS != (ret = post_env_vars(prev_pid, snapshot) ) ) {
        exit_status = ret;
        goto cleanup;
    }

    /*
     * Do the actual restart
     */
    ret = opal_crs.crs_restart(snapshot, 
                               false,
                               &child_pid);

    if (OPAL_SUCCESS != ret) {
        opal_show_help("help-opal-restart.txt", "restart_cmd_failure", true,
                       opal_restart_globals.snapshot_ref,
                       ret,
                       opal_crs_base_selected_component.base_version.mca_component_name);
        exit_status = ret;
        goto cleanup;
    }
    /* Should never get here, since crs_restart calls exec */

    /***************
     * Cleanup
     ***************/
 cleanup:
    if (OPAL_SUCCESS != (ret = finalize())) {
        return ret;
    }

    if(NULL != snapshot )
        OBJ_DESTRUCT(snapshot);

    return exit_status;
}

static int initialize(int argc, char *argv[])
{
    int ret, exit_status = OPAL_SUCCESS;
    char * tmp_env_var = NULL;

    /*
     * Make sure to init util before parse_args
     * to ensure installdirs is setup properly
     * before calling mca_base_open();
     */
    if( OPAL_SUCCESS != (ret = opal_init_util(&argc, &argv)) ) {
        return ret;
    }

    /*
     * Parse Command line arguments
     */
    if (OPAL_SUCCESS != (ret = parse_args(argc, argv))) {
        exit_status = ret;
        goto cleanup;
    }

    /*
     * Setup OPAL Output handle from the verbose argument
     */
    if( opal_restart_globals.verbose ) {
        opal_restart_globals.output = opal_output_open(NULL);
        opal_output_set_verbosity(opal_restart_globals.output, 10);
    } else {
        opal_restart_globals.output = 0; /* Default=STDOUT */
    }

    /* 
     * Turn off the selection of the CRS component,
     * we need to do that later
     */
    (void) mca_base_var_env_name("crs_base_do_not_select", &tmp_env_var);
    opal_setenv(tmp_env_var,
                "1", /* turn off the selection */
                true, &environ);
    free(tmp_env_var);
    tmp_env_var = NULL;

    /*
     * Make sure we select the proper compress component.
     */
    if( NULL != opal_restart_globals.snapshot_compress ) {
        (void) mca_base_var_env_name("compress", &tmp_env_var);
        opal_setenv(tmp_env_var,
                    opal_restart_globals.snapshot_compress,
                    true, &environ);
        free(tmp_env_var);
        tmp_env_var = NULL;
    }

    /*
     * Initialize the OPAL layer
     */
    if (OPAL_SUCCESS != (ret = opal_init(&argc, &argv))) {
        exit_status = ret;
        goto cleanup;
    }

    /*
     * If the checkpoint was compressed, then decompress it before continuing
     */
    if( NULL != opal_restart_globals.snapshot_compress ) {
        char * zip_dir = NULL;
        char * tmp_str = NULL;

        /* Make sure to clear the selection for the restart,
         * this way the user can swich compression mechanism
         * across restart
         */
        (void) mca_base_var_env_name("compress", &tmp_env_var);
        opal_unsetenv(tmp_env_var, &environ);
        free(tmp_env_var);
        tmp_env_var = NULL;

        asprintf(&zip_dir, "%s/%s%s",
                 opal_restart_globals.snapshot_loc,
                 opal_restart_globals.snapshot_ref,
                 opal_restart_globals.snapshot_compress_postfix);

        if (0 >  (ret = access(zip_dir, F_OK)) ) {
            opal_output(opal_restart_globals.output,
                        "Error: Unable to access the file [%s]!",
                        zip_dir);
            exit_status = OPAL_ERROR;
            goto cleanup;
        }

        opal_output_verbose(10, opal_restart_globals.output,
                            "Decompressing (%s)",
                            zip_dir);

        opal_compress.decompress(zip_dir, &tmp_str);

        if( NULL != zip_dir ) {
            free(zip_dir);
            zip_dir = NULL;
        }
        if( NULL != tmp_str ) {
            free(tmp_str);
            tmp_str = NULL;
        }
    }

    /*
     * If a cache directory has been suggested, see if it exists
     */
    if( NULL != opal_restart_globals.snapshot_cache ) {
        if(0 == (ret = access(opal_restart_globals.snapshot_cache, F_OK)) ) {
            opal_output_verbose(10, opal_restart_globals.output,
                                "Using the cached snapshot (%s) instead of (%s)",
                                opal_restart_globals.snapshot_cache,
                                opal_restart_globals.snapshot_loc);
            if( NULL != opal_restart_globals.snapshot_loc ) {
                free(opal_restart_globals.snapshot_loc);
                opal_restart_globals.snapshot_loc = NULL;
            }
            opal_restart_globals.snapshot_loc = opal_dirname(opal_restart_globals.snapshot_cache);
        } else {
            opal_show_help("help-opal-restart.txt", "cache_not_avail", true,
                           opal_restart_globals.snapshot_cache,
                           opal_restart_globals.snapshot_loc);
        }
    }

    /*
     * Mark this process as a tool
     */
    opal_cr_is_tool = true;

 cleanup:
    return exit_status;
}

static int finalize(void)
{
#if 0
    int ret;

    /*
     * JJH: Comment this out for now. It should only be called
     * when exec fails, and opal-restart is shutting down.
     * Currently BLCR is calling opal_even_fini() in the restart
     * functionality, so calling it twice is causing a segv.
     * Since we do not really need to do this, just comment it out
     * for now.
     */
    if (OPAL_SUCCESS != (ret = opal_finalize())) {
        return ret;
    }
#endif

    return OPAL_SUCCESS;
}

static int parse_args(int argc, char *argv[])
{
    int i, ret, len;
    opal_cmd_line_t cmd_line;
    char **app_env = NULL, **global_env = NULL;

    opal_restart_globals.help = false;
    opal_restart_globals.verbose = false;
    opal_restart_globals.snapshot_ref = NULL;
    opal_restart_globals.snapshot_loc = NULL;
    opal_restart_globals.snapshot_metadata = NULL;
    opal_restart_globals.snapshot_cache = NULL;
    opal_restart_globals.snapshot_compress = NULL;
    opal_restart_globals.snapshot_compress_postfix = NULL;
    opal_restart_globals.output = 0;

    /* Parse the command line options */
    opal_cmd_line_create(&cmd_line, cmd_line_opts);
    
    mca_base_open();
    mca_base_cmd_line_setup(&cmd_line);
    ret = opal_cmd_line_parse(&cmd_line, false, argc, argv);
    if (OPAL_SUCCESS != ret) {
        if (OPAL_ERR_SILENT != ret) {
            fprintf(stderr, "%s: command line error (%s)\n", argv[0],
                    opal_strerror(ret));
        }
        return 1;
    }
    if (opal_restart_globals.help ) {
        char *str, *args = NULL;
        args = opal_cmd_line_get_usage_msg(&cmd_line);
        str = opal_show_help_string("help-opal-restart.txt", "usage", true,
                                    args);
        if (NULL != str) {
            printf("%s", str);
            free(str);
        }
        free(args);
        /* If we show the help message, that should be all we do */
        exit(0);
    }
    
    /** 
     * Put all of the MCA arguments in the environment 
     */
    mca_base_cmd_line_process_args(&cmd_line, &app_env, &global_env);
    
    len = opal_argv_count(app_env);
    for(i = 0; i < len; ++i) {
        putenv(app_env[i]);
    }

    len = opal_argv_count(global_env);
    for(i = 0; i < len; ++i) {
        putenv(global_env[i]);
    }

    /**
     * Now start parsing our specific arguments
     */
    /* get the remaining bits */
    opal_cmd_line_get_tail(&cmd_line, &argc, &argv);

    if ( NULL == opal_restart_globals.snapshot_ref || 
         0 >= strlen(opal_restart_globals.snapshot_ref) ) {
        opal_show_help("help-opal-restart.txt", "invalid_filename", true,
                       "<none provided>");
        return OPAL_ERROR;
    }

    /* If we have arguments after the command, then assume they
     * need to be grouped together.
     * Useful in the 'mca crs self' instance.
     */
    if(argc > 0) {
        opal_restart_globals.snapshot_ref = strdup(opal_argv_join(argv, ' '));
    }

    return OPAL_SUCCESS;
}

static int check_file(void)
{
    int exit_status = OPAL_SUCCESS;
    int ret;
    char * path_to_check = NULL;

    if(NULL == opal_restart_globals.snapshot_ref) {
        opal_output(opal_restart_globals.output,
                    "Error: No filename provided!");
        exit_status = OPAL_ERROR;
        goto cleanup;
    }
    
    /*
     * Check for the existance of the snapshot handle in the snapshot directory
     */
    asprintf(&path_to_check, "%s/%s",
             opal_restart_globals.snapshot_loc,
             opal_restart_globals.snapshot_ref);

    opal_output_verbose(10, opal_restart_globals.output,
                        "Checking for the existence of (%s)",
                        path_to_check);

    if (0 >  (ret = access(path_to_check, F_OK)) ) {
        exit_status = OPAL_ERROR;
        goto cleanup;
    }

 cleanup:
    if( NULL != path_to_check) {
        free(path_to_check);
        path_to_check = NULL;
    }

    return exit_status;
}

static int post_env_vars(int prev_pid, opal_crs_base_snapshot_t *snapshot)
{
    int ret, exit_status = OPAL_SUCCESS;
    char *command = NULL;
    char *proc_file = NULL;
    char **loc_touch = NULL;
    char **loc_mkdir = NULL;
    int argc, i;

    if( 0 > prev_pid ) {
        opal_output(opal_restart_globals.output,
                    "Invalid PID (%d)\n",
                    prev_pid);
        exit_status = OPAL_ERROR;
        goto cleanup;
    }

    /*
     * This is needed so we can pass the previous environment to the restarted 
     * application process.
     */
    asprintf(&proc_file, "%s/%s-%d", opal_tmp_directory(), OPAL_CR_BASE_ENV_NAME, prev_pid);
    asprintf(&command, "env | grep OMPI_ > %s", proc_file);

    opal_output_verbose(5, opal_restart_globals.output,
                        "post_env_vars: Execute: <%s>", command);

    ret = system(command);
    if( 0 > ret) {
        exit_status = ret;
        goto cleanup;
    }

    /*
     * Any directories that need to be created
     */
    if( NULL == (snapshot->metadata = fopen(snapshot->metadata_filename, "r")) ) {
        opal_show_help("help-opal-restart.txt", "invalid_metadata", true,
                       opal_restart_globals.snapshot_metadata,
                       snapshot->metadata_filename);
        exit_status = OPAL_ERROR;
        goto cleanup;
    }
    opal_crs_base_metadata_read_token(snapshot->metadata, CRS_METADATA_MKDIR, &loc_mkdir);
    argc = opal_argv_count(loc_mkdir);
    for( i = 0; i < argc; ++i ) {
        if( NULL != command ) {
            free(command);
            command = NULL;
        }
        asprintf(&command, "mkdir -p %s", loc_mkdir[i]);

        opal_output_verbose(5, opal_restart_globals.output,
                            "post_env_vars: Execute: <%s>", command);

        ret = system(command);
        if( 0 > ret) {
            exit_status = ret;
            goto cleanup;
        }
    }
    if( 0 < argc ) {
        system("sync ; sync");
    }

    /*
     * Any files that need to exist
     */
    opal_crs_base_metadata_read_token(snapshot->metadata, CRS_METADATA_TOUCH, &loc_touch);
    argc = opal_argv_count(loc_touch);
    for( i = 0; i < argc; ++i ) {
        if( NULL != command ) {
            free(command);
            command = NULL;
        }
        asprintf(&command, "touch %s", loc_touch[i]);

        opal_output_verbose(5, opal_restart_globals.output,
                            "post_env_vars: Execute: <%s>", command);

        ret = system(command);
        if( 0 > ret) {
            exit_status = ret;
            goto cleanup;
        }
    }
    if( 0 < argc ) {
        system("sync ; sync");
    }

 cleanup:
    if( NULL != command) {
        free(command);
        command = NULL;
    }
    if( NULL != proc_file) {
        free(proc_file);
        proc_file = NULL;
    }
    if( NULL != loc_mkdir ) {
        opal_argv_free(loc_mkdir);
        loc_mkdir = NULL;
    }
    if( NULL != loc_touch ) {
        opal_argv_free(loc_touch);
        loc_touch = NULL;
    }

    if( NULL != snapshot->metadata ) {
        fclose(snapshot->metadata);
        snapshot->metadata = NULL;
    }

    return exit_status;
}
