/*
 *   Copyright (C) 2016 Intel Corporation.
 *   See COPYRIGHT notice in top-level directory.
 *    
 */

#ifdef HAVE_CONFIG_H
# include <darshan-runtime-config.h>
#endif

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check for LDMS libraries if Darshan is built --with-ldms */
#ifdef HAVE_LDMS
#include <ldms/ldms.h>
#include <ldms/ldmsd_stream.h>
#include <ovis_util/util.h>
#include "ovis_json/ovis_json.h"
#include "darshan-ldms.h"

struct darshanConnector dC = {
     .ldms_darsh = NULL,
     .ldms_lib = 0,
     };

ldms_t ldms_g;
static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
        switch (e->type) {
        case LDMS_XPRT_EVENT_CONNECTED:
                sem_post(&dC.conn_sem);
                dC.conn_status = 0;
                break;
        case LDMS_XPRT_EVENT_REJECTED:
                ldms_xprt_put(x);
                dC.conn_status = ECONNREFUSED;
                break;
        case LDMS_XPRT_EVENT_DISCONNECTED:
                ldms_xprt_put(x);
                dC.conn_status = ENOTCONN;
                break;
        case LDMS_XPRT_EVENT_ERROR:
                dC.conn_status = ECONNREFUSED;
                break;
        case LDMS_XPRT_EVENT_RECV:
                sem_post(&dC.recv_sem);
                break;
        case LDMS_XPRT_EVENT_SEND_COMPLETE:
                break;
        default:
                printf("Received invalid event type %d\n", e->type);
        }
}

#define SLURM_NOTIFY_TIMEOUT 5
ldms_t setup_connection(const char *xprt, const char *host,
                        const char *port, const char *auth)
{
        char hostname[PATH_MAX];
        const char *timeout = "5";
        int rc;
        struct timespec ts;

        if (!host) {
                if (0 == gethostname(hostname, sizeof(hostname)))
                        host = hostname;
        }
        if (!timeout) {
                ts.tv_sec = time(NULL) + 5;
                ts.tv_nsec = 0;
        } else {
                int to = atoi(timeout);
                if (to <= 0)
                        to = 5;
                ts.tv_sec = time(NULL) + to;
                ts.tv_nsec = 0;
        }

        ldms_g = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
        if (!ldms_g) {
                printf("Error %d creating the '%s' transport\n",
                       errno, xprt);
                return NULL;
        }

        sem_init(&dC.recv_sem, 1, 0);
        sem_init(&dC.conn_sem, 1, 0);

        rc = ldms_xprt_connect_by_name(ldms_g, host, port, event_cb, NULL);
        if (rc) {
                printf("Error %d connecting to %s:%s\n",
                       rc, host, port);
                return NULL;
        }
        sem_timedwait(&dC.conn_sem, &ts);
        if (dC.conn_status)
                return NULL;
        return ldms_g;
}

void darshan_ldms_connector_initialize()
{
    if (!getenv("DARSHAN_LDMS_STREAM"))
        dC.env_ldms_stream = "darshanConnector";

    /* Set flags for various LDMS environment variables */
    if (getenv("DXT_ENABLE_LDMS"))
        dC.dxt_enable_ldms = 0;
    else
        dC.dxt_enable_ldms =1;

    if (getenv("POSIX_ENABLE_LDMS"))
        dC.posix_enable_ldms = 0;
    else
        dC.posix_enable_ldms = 1;

    if (getenv("MPIIO_ENABLE_LDMS"))
        dC.mpiio_enable_ldms = 0;
    else
        dC.mpiio_enable_ldms = 1;

    if (getenv("STDIO_ENABLE_LDMS"))
        dC.stdio_enable_ldms = 0;
    else
        dC.stdio_enable_ldms = 1;

    if (getenv("HDF5_ENABLE_LDMS"))
        dC.hdf5_enable_ldms = 0;
    else
        dC.hdf5_enable_ldms = 1;

    if (getenv("MDHIM_ENABLE_LDMS"))
        dC.mdhim_enable_ldms = 0;
    else
        dC.mdhim_enable_ldms = 1;

    const char* env_ldms_xprt    = getenv("DARSHAN_LDMS_XPRT");
    const char* env_ldms_host    = getenv("DARSHAN_LDMS_HOST");
    const char* env_ldms_port    = getenv("DARSHAN_LDMS_PORT");
    const char* env_ldms_auth    = getenv("DARSHAN_LDMS_AUTH");

    /* Check/set LDMS transport type */
    if (!env_ldms_xprt || !env_ldms_host || !env_ldms_port || !env_ldms_auth){
        printf("Either the transport, host, port or authentication is not given\n");
        return;
    }

    pthread_mutex_lock(&dC.ln_lock);
    dC.ldms_darsh = setup_connection(env_ldms_xprt, env_ldms_host, env_ldms_port, env_ldms_auth);
        if (dC.conn_status != 0) {
            printf("Error setting up connection to LDMS streams daemon: %i -- exiting\n", dC.conn_status);
            pthread_mutex_unlock(&dC.ln_lock);
            return;
        }
        else if (dC.ldms_darsh->disconnected){
            printf("Disconnected from LDMS streams daemon -- exiting\n");
            pthread_mutex_unlock(&dC.ln_lock);
            return;
        }
    pthread_mutex_unlock(&dC.ln_lock);
    return;
}

void darshan_ldms_set_meta(const char *filename, const char *data_set, uint64_t record_id, int64_t rank)
{
    dC.rank = rank;
    dC.filename = filename;
    dC.data_set = data_set;
    dC.record_id = record_id;
    return;

}

void darshan_ldms_connector_send(int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, struct timespec tspec_start, struct timespec tspec_end, double total_time, char *mod_name, char *data_type)
{
    char jb11[1024];
    int rc, ret, i, size, exists;
    uint64_t micro_s = tspec_end.tv_nsec/1.0e3;
    dC.env_ldms_stream  = getenv("DARSHAN_LDMS_STREAM");

    pthread_mutex_lock(&dC.ln_lock);
    if (dC.ldms_darsh != NULL)
        exists = 1;
    else
        exists = 0;
    pthread_mutex_unlock(&dC.ln_lock);

    if (!exists){
        return;
    }


    if (strcmp(rwo, "open") == 0)
        dC.open_count = record_count;

    /* set record count to number of opens since we are closing the same file we opened.*/
    if (strcmp(rwo, "close") == 0)
        record_count = dC.open_count;

    if (strcmp(mod_name, "H5D") != 0){
        size = sizeof(dC.hdf5_data)/sizeof(dC.hdf5_data[0]);
        dC.data_set = "N/A";
        for (i=0; i < size; i++)
            dC.hdf5_data[i] = -1;
    }

    if (strcmp(data_type, "MOD") == 0)
    {
        dC.filename = "N/A";
        dC.exename = "N/A";
    }

    sprintf(jb11,"{ \"uid\":%ld, \"exe\":\"%s\",\"job_id\":%ld,\"rank\":%ld,\"ProducerName\":\"%s\",\"file\":\"%s\",\"record_id\":%"PRIu64",\"module\":\"%s\",\"type\":\"%s\",\"max_byte\":%ld,\"switches\":%ld,\"flushes\":%ld,\"cnt\":%ld,\"op\":\"%s\",\"seg\":[{\"data_set\":\"%s\",\"pt_sel\":%ld,\"irreg_hslab\":%ld,\"reg_hslab\":%ld,\"ndims\":%ld,\"npoints\":%ld,\"off\":%ld,\"len\":%ld,\"start\":%0.6f,\"dur\":%0.6f,\"total\":%.6f,\"timestamp\":%lu.%.6lu}]}", dC.uid, dC.exename, dC.jobid, dC.rank, dC.hname, dC.filename, dC.record_id, mod_name, data_type, max_byte, rw_switch, flushes, record_count, rwo, dC.data_set, dC.hdf5_data[0], dC.hdf5_data[1], dC.hdf5_data[2], dC.hdf5_data[3], dC.hdf5_data[4], offset, length, start_time, end_time-start_time, total_time, tspec_end.tv_sec, micro_s);
    //printf("this is in jb11 %s \n", jb11);
    
    rc = ldmsd_stream_publish(dC.ldms_darsh, dC.env_ldms_stream, LDMSD_STREAM_JSON, jb11, strlen(jb11) + 1);
	if (rc)
		printf("Error %d publishing data.\n", rc);
    
	out_1:
         return;
}
#else

struct darshanConnector dC = {
     .ldms_lib = 1
     };

void darshan_ldms_connector_initialize()
{
    return;
}

void darshan_ldms_set_meta(const char *filename, const char *data_set, uint64_t record_id, int64_t rank)
{
    return;
}

void darshan_ldms_connector_send(int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, struct timespec tspec_start, struct timespec tspec_end, double total_time, char *mod_name, char *data_type)
{
    return;
}
#endif
