#include "console.h"
#include "defs.h"
#include "loader.h"
#include "plic.h"
#include "timer.h"
#include "trap.h"
#include "virtio.h"

/* ch9: 网络协议栈函数声明 */
extern void net_platform_init(void);
extern int net_init(void);
extern int net_run(void);
extern void virtio_net_init(void);

void clean_bss()
{
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

void main()
{
	clean_bss();
	printf("hello world!\n");
	proc_init();
	kinit();
	kvm_init();
	trap_init();
	plicinit();
	virtio_disk_init();
	binit();
	fsinit();
	timer_init();

	/* ch9: 初始化网络协议栈 */
	net_platform_init();
	net_init();
	virtio_net_init();
	net_run();
	infof("network stack initialized");

	load_init_app();
	infof("start scheduler!");
	show_all_files();
	scheduler();
}