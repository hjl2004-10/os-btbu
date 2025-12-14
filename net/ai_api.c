/* ch9: 网络协议栈 - AI API客户端实现 */
#include "platform.h"
#include "http.h"
#include "ai_api.h"

/* 全局配置 */
static struct ai_api_config g_config;
static int g_initialized = 0;

/* 字符串追加 */
static int
ai_strappend(char *buf, int pos, int bufsize, const char *s)
{
    while (*s && pos < bufsize - 1) {
        buf[pos++] = *s++;
    }
    buf[pos] = 0;
    return pos;
}

/* 查找字符串 */
static char *
ai_strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

/* JSON转义字符串 */
static int
json_escape_string(char *buf, int pos, int bufsize, const char *s)
{
    while (*s && pos < bufsize - 2) {
        char c = *s++;
        if (c == '"' || c == '\\') {
            buf[pos++] = '\\';
            buf[pos++] = c;
        } else if (c == '\n') {
            buf[pos++] = '\\';
            buf[pos++] = 'n';
        } else if (c == '\r') {
            buf[pos++] = '\\';
            buf[pos++] = 'r';
        } else if (c == '\t') {
            buf[pos++] = '\\';
            buf[pos++] = 't';
        } else {
            buf[pos++] = c;
        }
    }
    buf[pos] = 0;
    return pos;
}

/* 构建聊天请求JSON */
static int
build_chat_request(const char *prompt, char *buf, int bufsize)
{
    int len = 0;

    /* {"model":"xxx","messages":[{"role":"user","content":"xxx"}]} */
    len = ai_strappend(buf, len, bufsize, "{\"model\":\"");
    len = ai_strappend(buf, len, bufsize, g_config.model);
    len = ai_strappend(buf, len, bufsize, "\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    len = json_escape_string(buf, len, bufsize, prompt);
    len = ai_strappend(buf, len, bufsize, "\"}]}");

    return len;
}

/* ch9: 解析十六进制字符 */
static int
hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ch9: 解析\uXXXX为Unicode码点 */
static int
parse_unicode_escape(const char *p, uint32 *codepoint)
{
    int i;
    uint32 val = 0;
    for (i = 0; i < 4; i++) {
        int d = hex_digit(p[i]);
        if (d < 0) return -1;
        val = (val << 4) | d;
    }
    *codepoint = val;
    return 0;
}

/* ch9: 将Unicode码点编码为UTF-8，返回写入的字节数 */
static int
encode_utf8(uint32 codepoint, char *buf, int bufsize)
{
    if (codepoint < 0x80) {
        /* 1字节: 0xxxxxxx */
        if (bufsize < 1) return 0;
        buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        /* 2字节: 110xxxxx 10xxxxxx */
        if (bufsize < 2) return 0;
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        /* 3字节: 1110xxxx 10xxxxxx 10xxxxxx */
        if (bufsize < 3) return 0;
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        /* 4字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        if (bufsize < 4) return 0;
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* 从JSON响应中提取content字段 */
static int
extract_content(const char *json, char *content, int content_size)
{
    const char *p;
    int i = 0;
    int escape = 0;

    /* 查找 "content":" */
    p = ai_strstr(json, "\"content\":");
    if (!p) return -1;

    p += 10; /* 跳过 "content": */

    /* 跳过空白 */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    /* 期望引号开始 */
    if (*p != '"') return -1;
    p++;

    /* 提取内容直到结束引号 */
    while (*p && i < content_size - 4) {  /* 预留UTF-8最大4字节 */
        if (escape) {
            if (*p == 'n') content[i++] = '\n';
            else if (*p == 'r') content[i++] = '\r';
            else if (*p == 't') content[i++] = '\t';
            else if (*p == '"') content[i++] = '"';
            else if (*p == '\\') content[i++] = '\\';
            else if (*p == 'u') {
                /* ch9: 处理\uXXXX Unicode转义 */
                uint32 codepoint;
                if (parse_unicode_escape(p + 1, &codepoint) == 0) {
                    /* 检查是否为UTF-16代理对的高位 (0xD800-0xDBFF) */
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        /* 需要读取低位代理 \uDCxx-\uDFxx */
                        if (p[5] == '\\' && p[6] == 'u') {
                            uint32 low;
                            if (parse_unicode_escape(p + 7, &low) == 0 &&
                                low >= 0xDC00 && low <= 0xDFFF) {
                                /* 组合成完整码点 */
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                p += 6; /* 跳过额外的\uXXXX */
                            }
                        }
                    }
                    int utf8_len = encode_utf8(codepoint, content + i, content_size - i);
                    i += utf8_len;
                    p += 4; /* 跳过XXXX */
                } else {
                    content[i++] = 'u'; /* 解析失败，保留原字符 */
                }
            }
            else content[i++] = *p;
            escape = 0;
        } else if (*p == '\\') {
            escape = 1;
        } else if (*p == '"') {
            break;
        } else {
            content[i++] = *p;
        }
        p++;
    }
    content[i] = 0;

    return i;
}

/* 初始化AI API */
int
ai_api_init(struct ai_api_config *config)
{
    int i;

    if (!config) return -1;

    /* 复制配置 */
    for (i = 0; config->host[i] && i < (int)sizeof(g_config.host) - 1; i++) {
        g_config.host[i] = config->host[i];
    }
    g_config.host[i] = 0;

    g_config.port = config->port;

    for (i = 0; config->api_key[i] && i < (int)sizeof(g_config.api_key) - 1; i++) {
        g_config.api_key[i] = config->api_key[i];
    }
    g_config.api_key[i] = 0;

    for (i = 0; config->model[i] && i < (int)sizeof(g_config.model) - 1; i++) {
        g_config.model[i] = config->model[i];
    }
    g_config.model[i] = 0;

    g_initialized = 1;
    infof("ai_api: initialized, host=%s, port=%d, model=%s",
          g_config.host, g_config.port, g_config.model);

    return 0;
}

/* 发送聊天请求 */
int
ai_chat(const char *prompt, struct ai_chat_response *resp)
{
    char *url;
    char *body;
    char *auth_header;
    struct http_response http_resp;
    int body_len;
    int ret = -1;
    int len;

    if (!g_initialized) {
        errorf("ai_api: not initialized");
        return -1;
    }

    if (!prompt || !resp) {
        errorf("ai_api: invalid parameters");
        return -1;
    }

    /* 初始化响应 */
    resp->success = 0;
    resp->content = 0;
    resp->content_len = 0;
    resp->error_code = 0;

    /* 分配缓冲区 */
    url = memory_alloc(256);
    body = memory_alloc(2048);
    auth_header = memory_alloc(256);
    if (!url || !body || !auth_header) {
        errorf("ai_api: memory_alloc failed");
        goto cleanup;
    }

    /* 构建URL: http://host:port/v1/chat/completions */
    len = 0;
    len = ai_strappend(url, len, 256, "http://");
    len = ai_strappend(url, len, 256, g_config.host);
    len = ai_strappend(url, len, 256, ":");
    /* 简单的数字转字符串 */
    {
        char portbuf[8];
        int port = g_config.port;
        int pi = 0;
        char tmp[8];
        int ti = 0;
        if (port == 0) {
            portbuf[pi++] = '0';
        } else {
            while (port > 0) {
                tmp[ti++] = '0' + (port % 10);
                port /= 10;
            }
            while (ti > 0) {
                portbuf[pi++] = tmp[--ti];
            }
        }
        portbuf[pi] = 0;
        len = ai_strappend(url, len, 256, portbuf);
    }
    len = ai_strappend(url, len, 256, "/v1/chat/completions");

    debugf("ai_api: url=%s", url);

    /* 构建请求体 */
    body_len = build_chat_request(prompt, body, 2048);
    debugf("ai_api: body=%s", body);

    /* 构建Authorization头: Bearer <api_key> */
    len = 0;
    len = ai_strappend(auth_header, len, 256, "Bearer ");
    len = ai_strappend(auth_header, len, 256, g_config.api_key);

    memset(&http_resp, 0, sizeof(http_resp));
    ret = http_post_with_auth(url, "application/json", (uint8 *)body, body_len,
                              auth_header, &http_resp);

    if (ret < 0) {
        errorf("ai_api: http_post failed");
        resp->error_code = -1;
        goto cleanup;
    }

    debugf("ai_api: status=%d, body_len=%d", http_resp.status_code, http_resp.body_len);

    if (http_resp.status_code != 200) {
        errorf("ai_api: HTTP error %d", http_resp.status_code);
        resp->error_code = http_resp.status_code;
        http_response_free(&http_resp);
        goto cleanup;
    }

    /* 从响应中提取content */
    if (http_resp.body && http_resp.body_len > 0) {
        resp->content = memory_alloc(AI_API_MAX_RESPONSE);
        if (resp->content) {
            int clen = extract_content(http_resp.body, resp->content, AI_API_MAX_RESPONSE);
            if (clen > 0) {
                resp->content_len = clen;
                resp->success = 1;
                ret = 0;
            } else {
                errorf("ai_api: failed to extract content");
                memory_free(resp->content);
                resp->content = 0;
                resp->error_code = -2;
            }
        }
    }

    http_response_free(&http_resp);

cleanup:
    if (url) memory_free(url);
    if (body) memory_free(body);
    if (auth_header) memory_free(auth_header);
    return ret;
}

/* 释放响应资源 */
void
ai_chat_response_free(struct ai_chat_response *resp)
{
    if (resp && resp->content) {
        memory_free(resp->content);
        resp->content = 0;
    }
}

/* 测试API连接 */
int
ai_api_test(void)
{
    struct ai_chat_response resp;
    int ret;

    infof("ai_api: testing connection...");

    ret = ai_chat("hello", &resp);

    if (ret == 0 && resp.success) {
        infof("ai_api: test successful, response: %s", resp.content);
        ai_chat_response_free(&resp);
        return 0;
    } else {
        errorf("ai_api: test failed, error_code=%d", resp.error_code);
        ai_chat_response_free(&resp);
        return -1;
    }
}
