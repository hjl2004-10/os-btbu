/* ch9: 网络协议栈 - HTTP客户端头文件 */
#ifndef HTTP_H
#define HTTP_H

#include "platform.h"

#define HTTP_METHOD_GET  0
#define HTTP_METHOD_POST 1

#define HTTP_MAX_HEADER_SIZE 1024
#define HTTP_MAX_BODY_SIZE   4096
#define HTTP_MAX_URL_SIZE    256

/* HTTP响应结构 */
struct http_response {
    int status_code;        /* HTTP状态码 */
    char content_type[64];  /* Content-Type */
    int content_length;     /* Content-Length */
    char *body;             /* 响应体 */
    int body_len;           /* 响应体长度 */
};

/* HTTP请求结构 */
struct http_request {
    int method;             /* GET/POST */
    char host[64];          /* 主机名 */
    uint16 port;            /* 端口 */
    char path[128];         /* 路径 */
    char *body;             /* 请求体 */
    int body_len;           /* 请求体长度 */
    char content_type[64];  /* Content-Type */
};

/*
 * HTTP API
 */

/* 初始化HTTP模块 */
int http_init(void);

/* 创建HTTP GET请求 */
int http_get(const char *url, struct http_response *resp);

/* 创建HTTP POST请求 */
int http_post(const char *url, const char *content_type,
              const uint8 *body, int body_len, struct http_response *resp);

/* 释放HTTP响应资源 */
void http_response_free(struct http_response *resp);

/* 解析URL */
int http_parse_url(const char *url, char *host, int host_size,
                   uint16 *port, char *path, int path_size);

#endif /* HTTP_H */
