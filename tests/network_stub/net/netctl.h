#pragma once
#define NET_CTL_STATE_IPObtained 3
#define NET_CTL_STATE_Disconnected 0
#define NET_CTL_ERROR_NOT_TERMINATED 0x80130102u
#define NET_CTL_INFO_IP_ADDRESS 14
union net_ctl_info {char ip_address[16];};
int netCtlInit(void);
void netCtlTerm(void);
int netCtlGetState(int *state);
int netCtlGetInfo(int code,union net_ctl_info *info);
