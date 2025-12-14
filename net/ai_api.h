/* ch9: 网络协议栈 - AI API客户端头文件 */
#ifndef AI_API_H
#define AI_API_H

#include "platform.h"

#define AI_API_MAX_RESPONSE 4096

/* AI API配置 */
struct ai_api_config {
    char host[64];          /* API服务器地址 */
    uint16 port;            /* 端口 */
    char api_key[128];      /* API密钥 */
    char model[32];         /* 模型名称 */
};

/* AI聊天响应 */
struct ai_chat_response {
    int success;            /* 是否成功 */
    char *content;          /* 响应内容 */
    int content_len;        /* 内容长度 */
    int error_code;         /* 错误码 */
};

/* 初始化AI API */
int ai_api_init(struct ai_api_config *config);

/* 发送聊天请求 */
int ai_chat(const char *prompt, struct ai_chat_response *resp);

/* 释放响应资源 */
void ai_chat_response_free(struct ai_chat_response *resp);

/* 测试API连接 */
int ai_api_test(void);

#endif /* AI_API_H */
