#ifndef VT_CTL_H
#define VT_CTL_H

#define VT_CTL_CLIENTS 4
#define VT_CTL_LINE 8192
#define VT_CTL_JOB_OUT 65536
#define VT_CTL_READ_N 8
#define VT_CTL_RG_HITS 64
#define VT_CTL_RG_OUT 8192

typedef struct {
	PEAK_HANDLE fd;
	PEAK_HANDLE pass;
	u32 n;
	char buf[VT_CTL_LINE];
} VtCtlClient;

typedef struct {
	const char *id;
	int id_n;
	const char *op;
	int op_n;
	const char *data;
	int data_n;
	const char *cmd;
	int cmd_n;
	const char *path;
	int path_n;
	int y;
	int n;
	int has_y;
	int has_n;
} VtCtlReq;

typedef struct {
	int pid;
	PEAK_HANDLE fd;
	int client;
	int id_n;
	int code;
	u32 seq;
	u32 out_n;
	bool trunc;
	bool dead;
	char id[96];
	char out[VT_CTL_JOB_OUT];
} VtCtlJob;

#endif
