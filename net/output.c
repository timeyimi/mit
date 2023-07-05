#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	uint32_t whom;
	int perm;
	int32_t req;

	while (1) {
		//查询是否存在发送数据包请求  NSREQ_OUTPUT
        //若存在，内核会将数据报的信息保存在nsipcbuf（传出参数）
        //read a packet from the network server
		req = ipc_recv((envid_t *)&whom, &nsipcbuf,  &perm);     //接收核心网络进程发来的请求
		if (req != NSREQ_OUTPUT) {
			cprintf("not a nsreq output\n");
			continue;
		}

    	struct jif_pkt *pkt = &(nsipcbuf.pkt);
		//调用之前定义好的系统调用发送数据包
        //send the packet to the device driver
    	while (sys_pkt_send(pkt->jp_data, pkt->jp_len) < 0) {        //通过系统调用发送数据包
       		//若当前环境无用，取消当前环境的调度，并选择一个不同的环境来运行。
			sys_yield();
    	}	
	}
}
