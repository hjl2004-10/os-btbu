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

/* ch9: AI API函数声明 */
struct ai_api_config {
    char host[64];
    uint16 port;
    char api_key[128];
    char model[32];
};
struct ai_chat_response {
    int success;
    char *content;
    int content_len;
    int error_code;
};
extern int ai_api_init(struct ai_api_config *config);
extern int ai_chat(const char *prompt, struct ai_chat_response *resp);
extern void ai_chat_response_free(struct ai_chat_response *resp);

void clean_bss()
{
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

/* ch9: 测试AI API */
static void test_ai_api(void)
{
	struct ai_api_config config;
	struct ai_chat_response resp;
	int i;

	/* 配置API */
	const char *host = "119.3.217.132";
	const char *api_key = "sk_c5n8Lh5kDrYHE8iBAGygAcb42mRo2dIaHJi3Vwvlc6o";
	const char *model = "lingxi-1";

	for (i = 0; host[i] && i < 63; i++) config.host[i] = host[i];
	config.host[i] = 0;
	config.port = 8000;
	for (i = 0; api_key[i] && i < 127; i++) config.api_key[i] = api_key[i];
	config.api_key[i] = 0;
	for (i = 0; model[i] && i < 31; i++) config.model[i] = model[i];
	config.model[i] = 0;

	printf("ch9: Testing AI API...\n");

	if (ai_api_init(&config) < 0) {
		printf("ch9: ai_api_init failed\n");
		return;
	}

	if (ai_chat("hello", &resp) == 0 && resp.success) {
		printf("ch9: AI response: %s\n", resp.content);
		ai_chat_response_free(&resp);
	} else {
		printf("ch9: ai_chat failed, error_code=%d\n", resp.error_code);
	}
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

	/* ch9: 测试AI API */
	test_ai_api();

	load_init_app();
	infof("start scheduler!");
	show_all_files();
	scheduler();
}