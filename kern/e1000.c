#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>

// LAB 6: Your driver code here

volatile void *bar_va;
struct e1000_tdh *tdh;  // 头指针寄存器
struct e1000_tdt *tdt;  // 尾指针寄存器
struct e1000_tx_desc tx_desc_array[TXDESCS];  // 发送描述符数组
char tx_buffer_array[TXDESCS][TX_PKT_SIZE];  // 发送队列缓冲区

struct e1000_rdh *rdh;
struct e1000_rdt *rdt;
struct e1000_rx_desc rx_desc_array[RXDESCS];  // 接收描述符数组
char rx_buffer_array[RXDESCS][RX_PKT_SIZE];  // 接收队列缓冲区

// 网卡的MAC地址
uint32_t E1000_MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

int
e1000_attachfn(struct pci_func *pcif){
    pci_func_enable(pcif);
    bar_va = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

    // E1000_STATUS定义在头文件，是状态寄存器在寄存器空间的偏移量
    uint32_t *status_reg = E1000REG(E1000_STATUS);  
    assert(*status_reg == 0x80080783);  // 验证状态寄存器是否在这个地址

    e1000_transmit_init();  // 初始化传送队列
    // 测试代码
    // char *data="transmit test";
    // e1000_transmit(data,13);
    e1000_receive_init();  // 初始化接收队列
    return 0;
}

static void
e1000_transmit_init(){
    int i;

    //1.初始化发送描述符队列，根据基址bar_va和宏定义中的偏移，为TDLEN、TDBAL、TDBAH指针分配空间
    for(i = 0; i < TXDESCS; ++i){
        tx_desc_array[i].addr = PADDR(tx_buffer_array[i]);  // 分配地址空间
        tx_desc_array[i].cmd = 0;  // 命令字段初始化
        tx_desc_array[i].status |= E1000_TXD_STAT_DD;  // 表示描述符已经完成
    }

    // TDLEN寄存器
    struct e1000_tdlen *tdlen = (struct e1000_tdlen *)E1000REG(E1000_TDLEN);
    tdlen->len = TXDESCS;

    // TDBAL寄存器
    uint32_t *tdbal = (uint32_t *)E1000REG(E1000_TDBAL);
    *tdbal = PADDR(tx_desc_array);

    // TDBAH 寄存器
    uint32_t *tdbah = (uint32_t *)E1000REG(E1000_TDBAH);
    *tdbah = 0;

    // TDH 寄存器
    tdh = (struct e1000_tdh *)E1000REG(E1000_TDH);
    tdh->tdh = 0;

    // TDT 寄存器
    tdt = (struct e1000_tdt *)E1000REG(E1000_TDT);
    tdt->tdt = 0;

    // TCTL 寄存器：传输控制寄存器
    struct e1000_tctl *tctl = (struct e1000_tctl *)E1000REG(E1000_TCTL);
    tctl->en = 1;
    tctl->psp = 1;
    tctl->ct = 0x10;
    tctl->cold = 0x40;

    // TIPG 寄存器：传输报文间间隔Transmit Inter-packet Gap
    struct e1000_tipg *tipg = (struct e1000_tipg *)E1000REG(E1000_TIPG);
    tipg->ipgt = 10;
    tipg->ipgr1 = 4;
    tipg->ipgr2 = 6;
}

int
e1000_transmit(void *data, size_t len){
    uint32_t current = tdt->tdt;

    if(!(tx_desc_array[current].status & E1000_TXD_STAT_DD)) return -E_TRANSMIT_RETRY;

    tx_desc_array[current].length = len;
    tx_desc_array[current].status &= ~E1000_TXD_STAT_DD;
    tx_desc_array[current].cmd |= (E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS);
    memcpy(tx_buffer_array[current], data, len);
    uint32_t next = (current + 1) % TXDESCS;
    tdt->tdt = next;
    return 0;
}

static void
get_ra_address(uint32_t mac[], uint32_t *ral, uint32_t *rah){
    uint32_t low = 0, high = 0;
    int i;

    for (i = 0; i < 4; i++) low |= mac[i] << (8 * i);

    for (i = 4; i < 6; i++) high |= mac[i] << (8 * i);

    *ral = low;
    *rah = high | E1000_RAH_AV;
}

static void
e1000_receive_init(){			 
    //RDBAL and RDBAH register
    uint32_t *rdbal = (uint32_t *)E1000REG(E1000_RDBAL);
    uint32_t *rdbah = (uint32_t *)E1000REG(E1000_RDBAH);

    //物理地址初始化
    *rdbal = PADDR(rx_desc_array);
    *rdbah = 0;

    //为接收描述符队列的addr字段指定接收缓冲区的物理地址  
    //一个描述符对应一个缓冲区
    int i;
    for (i = 0; i < RXDESCS; i++) rx_desc_array[i].addr = PADDR(rx_buffer_array[i]);
    
    //RDLEN register
    struct e1000_rdlen *rdlen = (struct e1000_rdlen *)E1000REG(E1000_RDLEN);
    rdlen->len = RXDESCS;

    //RDH and RDT register
    rdh = (struct e1000_rdh *)E1000REG(E1000_RDH);
    rdt = (struct e1000_rdt *)E1000REG(E1000_RDT);
    rdh->rdh = 0;
    rdt->rdt = RXDESCS-1;  // 注意RDT是数组的最后一个元素的位置，因为当RDT=RDH时表示队列已经满了

    // 接收控制寄存器
    uint32_t *rctl = (uint32_t *)E1000REG(E1000_RCTL);
    // EN(Receiver Enable)：使receiver开始工作
    // BAM(Broadcast Accept Mode):设置广播模式——设置：接收所有广播数据包，不设置：只有匹配才接收
    // SECRC(Strip Ethernet CRC from incoming packe):JOS测试需要，剥离接受包中的CRC(循环校验)
    *rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;

    // 为RAL和RAH设置MAC地址
    uint32_t *ra = (uint32_t *)E1000REG(E1000_RA);
    uint32_t ral, rah;
    get_ra_address(E1000_MAC, &ral, &rah);
    ra[0] = ral;
    ra[1] = rah;
}

int
e1000_receive(void *addr, size_t *len){
    // 使用static变量（初始化一次）
    static int32_t next = 0;
    // 如果第一个描述符队列都没有设置DD，那么说明极有可能没有数据，重试
    if(!(rx_desc_array[next].status & E1000_RXD_STAT_DD)) return -E_RECEIVE_RETRY;

    // 接收了错误的数据包：重试
    if(rx_desc_array[next].errors) {
            cprintf("receive errors\n");
            return -E_RECEIVE_RETRY;
    }
    
    *len = rx_desc_array[next].length;
    memcpy(addr, rx_buffer_array[next], *len);

    rdt->rdt = (rdt->rdt + 1) % RXDESCS;
    next = (next + 1) % RXDESCS;
    return 0;
}