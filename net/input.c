#include "ns.h"
#include "kern/e1000.h"

extern union Nsipc nsipcbuf;

void
sleep(int msec)
{
	// 利用实验刚开始设置的时钟中断获得当前时间
	unsigned now = sys_time_msec();
	unsigned end = now + msec;

	if ((int)now < 0 && (int)now > -MAXERROR)
		panic("sys_time_msec: %e", (int)now);

	while (sys_time_msec() < end) sys_yield();
}

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	size_t len;
	// 保存从缓冲区中得到的数据包
	char buf[RX_PKT_SIZE];

	// 循环取出接收缓冲区中的数据包
	while (1) {
		// 如果没有数据包就继续循环
		if (sys_pkt_recv(buf, &len) < 0) continue;

		memcpy(nsipcbuf.pkt.jp_data, buf, len);  // 保存数据包到jp_data
		nsipcbuf.pkt.jp_len = len;

		// 发送数据包给core network server
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P|PTE_U|PTE_W);
		// 等待50毫秒，保证数据包被core network server完整接收
		sleep(50);
	}
}
