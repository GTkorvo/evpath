#include "config.h"
#include "ltdl.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#ifdef HAVE_WINDOWS_H
#include <winsock.h>
#define __ANSI_CPP__
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <ffs.h>
#include <atl.h>
#include "evpath.h"
#include "gen_thread.h"
#include "cercs_env.h"
#include "cm_internal.h"

extern atom_t CM_REBWM_RLEN;
extern atom_t CM_REBWM_REPT;
extern atom_t CM_BW_MEASURE_INTERVAL;
extern atom_t CM_BW_MEASURE_TASK;
extern atom_t CM_BW_MEASURED_VALUE;
extern atom_t CM_BW_MEASURED_COF;
extern atom_t CM_BW_MEASURE_SIZE;
extern atom_t CM_BW_MEASURE_SIZEINC;

#define CMPerfProbe (unsigned int) 0xf0
#define CMPerfProbeResponse (unsigned int) 0xf1
#define CMPerfBandwidthInit (unsigned int) 0xf2
#define CMPerfBandwidthBody (unsigned int) 0xf3
#define CMPerfBandwidthEnd  (unsigned int) 0xf4
#define CMPerfBandwidthResult  (unsigned int) 0xf5

#define CMRegressivePerfBandwidthInit (unsigned int) 0xf6
#define CMRegressivePerfBandwidthBody (unsigned int) 0xf7
#define CMRegressivePerfBandwidthEnd  (unsigned int) 0xf8
#define CMRegressivePerfBandwidthResult  (unsigned int) 0xf9

void
CMdo_performance_response(CMConnection conn, int length, int func,
			  int byte_swap, char *buffer)
{
    /* part of length was read already */
    length += 8;
    switch(func) {
    case CMPerfProbe:
	/* first half of latency probe arriving */
	{
	    struct FFSEncodeVec tmp_vec[2];
	    int header[2];
	    int actual;
	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = length | (CMPerfProbeResponse << 24);
	    tmp_vec[1].iov_len = length - 8;
	    tmp_vec[1].iov_base = buffer;

	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - responding to latency probe of %d bytes\n", length);
	    actual = INT_CMwrite_raw(conn, tmp_vec, tmp_vec + 1, 2, length, NULL, 0, 0);
	    if (actual != 2) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMPerfProbeResponse:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    chr_time *timer = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - latency probe response, condition %d\n", cond);
	    chr_timer_stop(timer);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
    case CMPerfBandwidthInit:
	/* initiate bandwidth measure */
	chr_timer_start(&conn->bandwidth_start_time);
	break;
    case CMPerfBandwidthBody:
	/* no activity for inner packets */
	break;
    case CMPerfBandwidthEnd:
	/* first half of latency probe arriving */
	{
	    int header[4];
	    int actual;
	    struct FFSEncodeVec tmp_vec[1];
	    chr_timer_stop(&conn->bandwidth_start_time);

	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = sizeof(header) | (CMPerfBandwidthResult << 24);
	    header[2] = *(int*)buffer;  /* first entry should be condition */
	    header[3] = (int) 
		chr_time_to_microsecs(&conn->bandwidth_start_time);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completing bandwidth probe - %d microseconds to receive\n", header[2]);
	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, sizeof(header), NULL, 0, 0);

	    if (actual != 1) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMPerfBandwidthResult:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    int time;
	    char *chr_time, tmp;
	    int *result_p = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    time = ((int*)buffer)[1];/* second entry should be condition */
	    if (byte_swap) {
		chr_time = (char*)&time;
		tmp = chr_time[0];
		chr_time[0] = chr_time[3];
		chr_time[3] = tmp;
		tmp = chr_time[1];
		chr_time[1] = chr_time[2];
		chr_time[2] = tmp;
	    }
	    *result_p = time;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - bandwidth probe response, condition %d\n", cond);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
    case CMRegressivePerfBandwidthInit:
	/* initiate bandwidth measure */
        CMtrace_out(conn->cm, CMConnectionVerbose, "CM - received CM bw measure initiate\n");
	chr_timer_start(&conn->regressive_bandwidth_start_time);
	break;
    case CMRegressivePerfBandwidthBody:
	/* no activity for inner packets */
	break;
    case CMRegressivePerfBandwidthEnd:
	/* first half of latency probe arriving */
	{
	    int header[4];
	    int actual;
	    struct FFSEncodeVec tmp_vec[1];
	    chr_timer_stop(&conn->regressive_bandwidth_start_time);

	    header[0] = 0x434d5000;  /* CMP\0 */
	    header[1] = sizeof(header) | (CMRegressivePerfBandwidthResult << 24);
	    header[2] = *(int*)buffer;  /* first entry should be condition */
	    header[3] = (int) 
		chr_time_to_microsecs(&conn->regressive_bandwidth_start_time);
            CMtrace_out(conn->cm, CMConnectionVerbose, "CM - received CM bw measure end, condition %d\n", *(int*)buffer);
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completing bandwidth probe - %d microseconds to receive\n", header[2]);

	    tmp_vec[0].iov_base = &header;
	    tmp_vec[0].iov_len = sizeof(header);
	    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, sizeof(header), NULL, 0, 0);
	    if (actual != 1) {
		printf("perf write failed\n");
	    }
	}
	break;
    case CMRegressivePerfBandwidthResult:
	/* last half of latency probe arriving, probe completion*/
	{
	    int cond = *(int*)buffer;  /* first entry should be condition */
	    int time;
	    char *chr_time, tmp;
	    int *result_p = INT_CMCondition_get_client_data(conn->cm, cond);
	    
	    time = ((int*)buffer)[1];/* second entry should be condition */
	    if (byte_swap) {
		chr_time = (char*)&time;
		tmp = chr_time[0];
		chr_time[0] = chr_time[3];
		chr_time[3] = tmp;
		tmp = chr_time[1];
		chr_time[1] = chr_time[2];
		chr_time[2] = tmp;
	    }
	    *result_p = time;
	    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - bandwidth probe response, condition %d\n", cond);
	    INT_CMCondition_signal(conn->cm, cond);
	}
	break;
	
    }
}

static long
do_single_probe(CMConnection conn, int size, attr_list attrs)
{
    int cond;
    static int max_block_size = 0;
    static char *block = NULL;
    chr_time round_trip_time;
    int actual;
    struct FFSEncodeVec tmp_vec[1];

    (void)attrs;
    cond = INT_CMCondition_get(conn->cm, conn);

    if (size < 12) size = 12;
    if (max_block_size == 0) {
	char *new_block = malloc(size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    } else if (size > max_block_size) {
	char *new_block = realloc(block, size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    }
    
    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */
    ((int*)block)[1] = size | (CMPerfProbe<<24);
    ((int*)block)[2] = cond;   /* condition value in third entry */
    
    INT_CMCondition_set_client_data( conn->cm, cond, &round_trip_time);

    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating latency probe of %d bytes\n", size);
    chr_timer_start(&round_trip_time);

    tmp_vec[0].iov_base = &block[0];
    tmp_vec[0].iov_len = size;
    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);

    if (actual != 1) return -1;

    INT_CMCondition_wait(conn->cm, cond);
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completed latency probe - result %g microseconds\n", chr_time_to_microsecs(&round_trip_time));
    return (long) chr_time_to_microsecs(&round_trip_time);
}

/* return units are microseconds */
extern long
INT_CMprobe_latency(CMConnection conn, int size, attr_list attrs)
{
    int i;
    long result = 0;
    int repeat_count = 5;
    for (i=0; i < 2; i++) {
	(void) do_single_probe(conn, size, attrs);
    }
    for (i=0; i < repeat_count; i++) {
	result += do_single_probe(conn, size, attrs);
    }
    result /= repeat_count;
    return result;
}

/* return units are Kbytes/sec */
extern long
INT_CMprobe_bandwidth(CMConnection conn, int size, attr_list attrs)
{
    int i;
    int cond;
    int repeat_count = 100000/size;  /* send about 100K */
    static int max_block_size = 0;
    static char *block = NULL;
    int microsecs_to_receive;
    int actual;
    double bandwidth;
    struct FFSEncodeVec tmp_vec[1];

    (void)attrs;
    cond = INT_CMCondition_get(conn->cm, conn);

    if (size < 16) size = 16;
    if (repeat_count == 0) repeat_count = 1;
    if (max_block_size == 0) {
	char *new_block = malloc(size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    } else if (size > max_block_size) {
	char *new_block = realloc(block, size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = size;
	memset(block, 0xef, size);
    }
    
    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */
    ((int*)block)[1] = size | (CMPerfBandwidthInit<<24);
    ((int*)block)[2] = cond;   /* condition value in third entry */
    
    INT_CMCondition_set_client_data( conn->cm, cond, &microsecs_to_receive);

    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating bandwidth probe of %d bytes, %d messages\n", size, repeat_count);
    tmp_vec[0].iov_base = &block[0];
    tmp_vec[0].iov_len = size;
    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);
    if (actual != 1) { 
	return -1;
    }

    ((int*)block)[1] = size | (CMPerfBandwidthBody<<24);
    for (i=0; i <(repeat_count-1); i++) {
	actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);
	if (actual != 1) {
	    return -1;
	}
    }
    ((int*)block)[1] = size | (CMPerfBandwidthEnd <<24);

    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);
    if (actual != 1) {
	return -1;
    }

    INT_CMCondition_wait(conn->cm, cond);
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Completed bandwidth probe - result %d microseconds\n", microsecs_to_receive);
    bandwidth = ((double) size * (double)repeat_count * 1000.0) / 
	(double)microsecs_to_receive;
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Estimated bandwidth - %g Mbytes/sec\n", bandwidth / 1000.0);
    return (long) bandwidth;
}



/* matrix manipulation function for regression */

/***********************************************************
Name:     AtA
Passed:   **A, a matrix of dim m by n 
Returns:  **R, a matrix of dim m by p
***********************************************************/
static void 
AtA(double **A, int m, int n, double **R)
{
    int i, j, k;

    for (i = 0; i < n; i++)
	for (j = 0; j < n; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < m; k++)
		R[i][j] += A[k][i] * A[k][j];
	}
}

/***********************************************************
Name:     Atf
Passed:   **A, a matrix of dim n by p
*f, a vector of dim n
Returns:  **R, a matrix of dim p by 1
***********************************************************/
static void 
Atf(double **A, double *f, int n, int p, double **R)
{
    int i, j, k;

    for (i = 0; i < p; i++)
	for (j = 0; j < 1; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < n; k++)
		R[i][j] += A[k][i] * f[k];
	}
}
/*********************************************************************
 * dp_inv: inverse matrix A
 *              matrix A is replaced by its inverse
 *		returns ks, an integer
 *    A - square matrix of size n by n
 *    ks equals n when A is invertable
 * example usage:
	  if(dp_inv(N,u) != u){
            printf("Error inverting N\n");
            exit(-1);}
***********************************************************/
static int 
dp_inv(double **A, int n)
{
    int i, j, k, ks;

    ks = 0;
    for (k = 0; k < n; k++) {
	if (A[k][k] != 0.0) {
	    ks = ks + 1;
	    for (j = 0; j < n; j++) {
		if (j != k)
		    A[k][j] = A[k][j] / A[k][k];
	    }			/* end for j */
	    A[k][k] = 1.0 / A[k][k];
	}			/* end if */
	for (i = 0; i < n; i++) {
	    if (i != k) {
		for (j = 0; j < n; j++) {
		    if (j != k)
			A[i][j] = A[i][j] - A[i][k] * A[k][j];
		}		/* end for j */
		A[i][k] = -A[i][k] * A[k][k];
	    }			/* end if */
	}			/* end for i */
    }				/* end for k */
    return ks;
}				/* end MtxInverse() */


/***********************************************************
Name:     AtB
Passed:   **A, a matrix of dim m by n 
**B, a matrix of dim m by p
Returns:  **R, a matrix of dim n by p
***********************************************************/
static void 
AtB(double **A, double **B, int m, int n, int p, double **R)
{
    int i, j, k;

    for (i = 0; i < n; i++)
	for (j = 0; j < p; j++) {
	    R[i][j] = 0.0;
	    for (k = 0; k < m; k++)
		R[i][j] += A[k][i] * B[k][j];
	}
}

/************************************************************************/
/* Frees a double pointer array mtx[row][] */
/************************************************************************/
static void 
dub_dp_free(double **mtx, int row)
{
    int tmp_row;
    for (tmp_row = 0; tmp_row < row; tmp_row++)
	free(mtx[tmp_row]);
    free(mtx);
}

/************************************************************************/
/* Allocate memory for a double pointer array mtx[row][col], */
/* return double pointer  */
/************************************************************************/
static double **
dub_dp_mtxall(int row, int col)
{
    double **mtx;
    int tmp_row;

    /* Set Up Row of Pointers */
    mtx = (double **) malloc((unsigned) (row) * sizeof(double *));
    if (mtx == NULL)
	return NULL;

    /* Set Up Columns in Matrix */
    for (tmp_row = 0; tmp_row < row; tmp_row++) {
	mtx[tmp_row] = (double *) malloc((unsigned) (col) * sizeof(double));
	/* If could not Allocate All Free Memory */
	if (mtx[tmp_row] == NULL) {
	    dub_dp_free(mtx, row);
	    return NULL;
	}			/* Return Null Pointer */
    }
    return mtx;			/* Return Pointer to Matrix */
}


static double 
Regression(CMConnection conn, DelaySizeMtx *inputmtx)
{

    /* CMatrixUtils mtxutl; */
    double *Y, **X, **a;
    double **XtX, **XtY;
    int j;			/* , i, k; */
    /* char plinkid[9]; */
    int numofsizes;
    (void)conn;
    numofsizes = inputmtx->MsgNum;

    Y = (double *) malloc(sizeof(double) * inputmtx->MsgNum);
    X = dub_dp_mtxall(inputmtx->MsgNum, 2);
    a = dub_dp_mtxall(2, 1);
    XtX = dub_dp_mtxall(2, 2);
    XtY = dub_dp_mtxall(2, 1);

    /* regression estimate using least square method */
    for (j = 0; j < numofsizes; j++) {
	/* convert to one way delay in milliseconds */
	Y[j] = inputmtx->AveRTDelay[j] / 2 * 1000;
	/* convert to Kbytes */
	X[j][0] = inputmtx->MsgSize[j] / 1024;
	X[j][1] = 1;
    }
    AtA(X, numofsizes, 2, XtX);
    Atf(X, Y, numofsizes, 2, XtY);
    if (!dp_inv(XtX, 2)) {
	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regression()- Matrix XtX is not invertible\n");
	return -1;
    }
    AtB(XtX, XtY, 2, 2, 1, a);
    CMtrace_out(conn->cm, CMLowLevelVerbose,"CM - Regression():\nslope = %f (Bandwidth = %f Mbps), intercept = %f\n", a[0][0], 8 / a[0][0], a[1][0]);

    dub_dp_free(X, numofsizes);
    return 8 / a[0][0];
}

/* return units are Mbps */
extern double
INT_CMregressive_probe_bandwidth(CMConnection conn, int size, attr_list attrs)
{
    int i, j;
    int cond;
    int N = 9; /* send out N tcp streams with varied length*/
    int repeat_count = 3;
    static int max_block_size = 0;
    static char *block = NULL;
    int microsecs_to_receive;
    int actual;
    double bandwidth;
    int biggest_size;
    DelaySizeMtx dsm;
    double ave_delay=0.0, var_delay=0.0;
    double ave_size=0.0, var_size=0.0;
    double EXY=0.0;
    double covXY=0.0, cofXY=0.0;
    struct FFSEncodeVec tmp_vec[1];

    if (size < 16) size = 16;

    if (attrs != NULL) {
	get_int_attr(attrs, CM_REBWM_RLEN, &N);
	
	get_int_attr(attrs, CM_REBWM_REPT, &repeat_count);
	CMtrace_out(conn->cm, CMLowLevelVerbose, "INT_CMregressive_probe_bandwidth: get from attr, N: %d, repeat_count: %d\n", N, repeat_count);
	if(N<6) N=6;
	if(repeat_count<3) repeat_count=3;
    } else {
	N=9;
	repeat_count=3;
    }
    CMtrace_out(conn->cm, CMConnectionVerbose, "CM - INITIATE BW MEASURE on CONN %p\n", conn);
    biggest_size=size*(N+1);

    if (max_block_size == 0) {
	char *new_block = malloc(biggest_size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = biggest_size;
	memset(block, 0xef, biggest_size);
    } else if (biggest_size > max_block_size) {
	char *new_block = realloc(block, biggest_size);
	if (new_block == NULL) return -1;
	block = new_block;
	max_block_size = biggest_size;
	memset(block, 0xef, biggest_size);
    }

    /* CMP\0 in first entry for CMPerformance message */
    ((int*)block)[0] = 0x434d5000;
    /* size in second entry, high byte gives CMPerf operation */

    dsm.MsgNum=N;
    dsm.AveRTDelay=malloc(sizeof(double)*N);
    dsm.MsgSize=malloc(sizeof(int)*N);
    
    for(i =0; i<N; i++){
	cond = INT_CMCondition_get(conn->cm, conn);
	((int*)block)[2] = cond;   /* condition value in third entry */
	INT_CMCondition_set_client_data( conn->cm, cond, &microsecs_to_receive);

	/* size in second entry, high byte gives CMPerf operation */
	((int*)block)[1] = size | (CMRegressivePerfBandwidthInit<<24);
	((int*)block)[3] = size;

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Initiating bandwidth probe of %d bytes, %d messages\n", size, repeat_count);
	tmp_vec[0].iov_base = &block[0];
	tmp_vec[0].iov_len = size;
	actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);

	if (actual != 1) {
	    return -1;
	}

	((int*)block)[1] = size | (CMRegressivePerfBandwidthBody<<24);
	for (j=0; j <(repeat_count-1); j++) {
	    actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);
	    if (actual != 1) {
		return -1;
	    }
	}
	((int*)block)[1] = size | (CMRegressivePerfBandwidthEnd <<24);
	actual = INT_CMwrite_raw(conn, tmp_vec, NULL, 1, size, NULL, 0, 0);
	if (actual != 1) {
	    return -1;
	}

	if (INT_CMCondition_wait(conn->cm, cond) == 0) {
	    return 0.0;
	}
	bandwidth = ((double) size * (double)repeat_count * 1000.0) / 
	    (double)microsecs_to_receive;

	dsm.AveRTDelay[i]=(double)microsecs_to_receive*2.0/(double)repeat_count/1000000.0;
	dsm.MsgSize[i]=size;
	/*change size for the next round of bw measurment. */
	size+=biggest_size/(N+1);
	ave_delay+=dsm.AveRTDelay[i]*1000.0;
	ave_size+=dsm.MsgSize[i];
	EXY+=dsm.AveRTDelay[i]*1000.0*dsm.MsgSize[i];

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Partial Estimated bandwidth- %f Mbps, size: %d, delay: %d, ave_delay+=%f\n", bandwidth*8.0 / 1000.0, size-biggest_size/(N+1), microsecs_to_receive, dsm.AveRTDelay[i]*1000.0);


    }
    bandwidth=Regression( conn, &dsm);

    ave_delay /= (double)N;
    ave_size /= (double)N;
    EXY /= (double)N;
    for(i=0;i<N; i++){
      var_delay += (dsm.AveRTDelay[i]*1000.0-ave_delay)*(dsm.AveRTDelay[i]*1000.0-ave_delay);
      var_size += (dsm.MsgSize[i]-ave_size)*(dsm.MsgSize[i]-ave_size);
    }
    var_delay /= (double)N;
    var_size /= (double)N;

    covXY=EXY-ave_delay*ave_size;
    cofXY=covXY/(sqrt(var_delay)*sqrt(var_size));
    
     CMtrace_out(conn->cm, CMLowLevelVerbose,"INT_CMregressive_probe_bandwidth: ave_delay: %f, ave_size: %f, var_delay: %f, var_size: %f, EXY: %f, covXY: %f, cofXY: %f\n", ave_delay, ave_size, var_delay, var_size, EXY, covXY, cofXY);
    
    CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regressive Estimated bandwidth- %f Mbps, size: %d\n", bandwidth, size);

    free(dsm.AveRTDelay);
    free(dsm.MsgSize);
  
    if(cofXY<0.97 && cofXY>-0.97)
	if(bandwidth>0) bandwidth*=-1; /*if the result is not reliable, return negative bandwidth*/
  
    if (conn->attrs == NULL) conn->attrs = CMcreate_attr_list(conn->cm);

    {
	int ibandwidth, icof;
	ibandwidth = (int) (bandwidth * 1000.0);
	icof = (int) (cofXY * 1000.0);

	CMtrace_out(conn->cm, CMLowLevelVerbose, "CM - Regressive Setting measures to BW %d kbps, COF %d\n", ibandwidth, icof);
	set_int_attr(conn->attrs, CM_BW_MEASURED_VALUE, ibandwidth);
	set_int_attr(conn->attrs, CM_BW_MEASURED_COF, icof);
     
    }
    return  bandwidth;

}

