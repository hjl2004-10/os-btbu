/* ch9: 网络协议栈 - HTTP客户端实现 */
#include "platform.h"
#include "util.h"
#include "ip.h"
#include "tcp.h"
#include "http.h"

/* 简单的字符串比较（不区分大小写） */
static int
strcasecmp_n(const char *s1, const char *s2, int n)
{
    for (int i = 0; i < n; i++) {
        char c1 = s1[i];
        char c2 = s2[i];
        /* 转小写 */
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) break;
    }
    return 0;
}

/* 数字转字符串 */
static int
itoa_simple(int num, char *buf, int bufsize)
{
    char tmp[16];
    int i = 0, j = 0;
    int neg = 0;

    if (num < 0) {
        neg = 1;
        num = -num;
    }
    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num > 0) {
            tmp[i++] = '0' + (num % 10);
            num /= 10;
        }
    }
    if (neg && j < bufsize - 1) buf[j++] = '-';
    while (i > 0 && j < bufsize - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = 0;
    return j;
}

/* 字符串追加 */
static int
strappend(char *buf, int pos, int bufsize, const char *s)
{
    while (*s && pos < bufsize - 1) {
        buf[pos++] = *s++;
    }
    buf[pos] = 0;
    return pos;
}

/* 数字转字符串并追加 */
static int
appendnum(char *buf, int pos, int bufsize, int num)
{
    char tmp[16];
    itoa_simple(num, tmp, sizeof(tmp));
    return strappend(buf, pos, bufsize, tmp);
}

/* 解析URL */
int
http_parse_url(const char *url, char *host, int host_size,
               uint16 *port, char *path, int path_size)
{
    const char *p = url;
    int i;

    /* 跳过 http:// */
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p') {
        p += 4;
        if (*p == 's') p++; /* https */
        if (*p == ':') p++;
        if (*p == '/') p++;
        if (*p == '/') p++;
    }

    /* 提取主机名 */
    i = 0;
    while (*p && *p != ':' && *p != '/' && i < host_size - 1) {
        host[i++] = *p++;
    }
    host[i] = 0;

    /* 提取端口 */
    *port = 80; /* 默认端口 */
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    /* 提取路径 */
    if (*p == '/') {
        i = 0;
        while (*p && i < path_size - 1) {
            path[i++] = *p++;
        }
        path[i] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }

    return 0;
}

/* 解析IP地址字符串 - 返回网络字节序（与ip_addr_pton一致） */
static ip_addr_t
parse_ip(const char *s)
{
    ip_addr_t addr;
    uint8 *p = (uint8 *)&addr;
    const char *sp = s;
    int i;

    for (i = 0; i < 4; i++) {
        int val = 0;
        while (*sp >= '0' && *sp <= '9') {
            val = val * 10 + (*sp - '0');
            sp++;
        }
        p[i] = (uint8)val;
        if (*sp == '.') sp++;
    }
    return addr;
}

/* 构建HTTP请求 */
static int
http_build_request(struct http_request *req, char *buf, int bufsize)
{
    int len = 0;
    const char *method_str = (req->method == HTTP_METHOD_POST) ? "POST" : "GET";

    /* 请求行: METHOD PATH HTTP/1.1\r\n */
    len = strappend(buf, len, bufsize, method_str);
    len = strappend(buf, len, bufsize, " ");
    len = strappend(buf, len, bufsize, req->path);
    len = strappend(buf, len, bufsize, " HTTP/1.1\r\n");

    /* Host头 */
    len = strappend(buf, len, bufsize, "Host: ");
    len = strappend(buf, len, bufsize, req->host);
    len = strappend(buf, len, bufsize, "\r\n");

    /* User-Agent */
    len = strappend(buf, len, bufsize, "User-Agent: uCore-HTTP/1.0\r\n");

    /* Connection */
    len = strappend(buf, len, bufsize, "Connection: close\r\n");

    /* Content-Type和Content-Length (POST) */
    if (req->method == HTTP_METHOD_POST && req->body_len > 0) {
        len = strappend(buf, len, bufsize, "Content-Type: ");
        len = strappend(buf, len, bufsize,
                       req->content_type[0] ? req->content_type : "application/json");
        len = strappend(buf, len, bufsize, "\r\n");

        len = strappend(buf, len, bufsize, "Content-Length: ");
        len = appendnum(buf, len, bufsize, req->body_len);
        len = strappend(buf, len, bufsize, "\r\n");
    }

    /* ch9: Authorization头 */
    if (req->authorization[0]) {
        len = strappend(buf, len, bufsize, "Authorization: ");
        len = strappend(buf, len, bufsize, req->authorization);
        len = strappend(buf, len, bufsize, "\r\n");
    }

    /* 空行 */
    len = strappend(buf, len, bufsize, "\r\n");

    /* 请求体 */
    if (req->method == HTTP_METHOD_POST && req->body && req->body_len > 0) {
        if (len + req->body_len < bufsize) {
            memmove(buf + len, req->body, req->body_len);
            len += req->body_len;
        }
    }

    return len;
}

/* 解析HTTP响应 */
static int
http_parse_response(const char *data, int datalen, struct http_response *resp)
{
    const char *p = data;
    const char *end = data + datalen;
    const char *line_end;

    /* 初始化响应 */
    resp->status_code = 0;
    resp->content_length = 0;
    resp->body = 0;
    resp->body_len = 0;
    resp->content_type[0] = 0;

    /* 解析状态行: HTTP/1.x XXX ... */
    if (datalen < 12) return -1;

    /* 跳过HTTP版本 */
    while (p < end && *p != ' ') p++;
    if (p >= end) return -1;
    p++;

    /* 解析状态码 */
    resp->status_code = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        resp->status_code = resp->status_code * 10 + (*p - '0');
        p++;
    }

    /* 找到行尾 */
    while (p < end && *p != '\n') p++;
    if (p >= end) return -1;
    p++;

    /* 解析头部 */
    while (p < end) {
        line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        /* 空行表示头部结束 */
        if (line_end - p <= 1 || (line_end - p == 2 && p[0] == '\r')) {
            p = line_end + 1;
            break;
        }

        /* Content-Length */
        if (strcasecmp_n(p, "Content-Length:", 15) == 0) {
            const char *v = p + 15;
            while (*v == ' ') v++;
            resp->content_length = 0;
            while (*v >= '0' && *v <= '9') {
                resp->content_length = resp->content_length * 10 + (*v - '0');
                v++;
            }
        }

        /* Content-Type */
        if (strcasecmp_n(p, "Content-Type:", 13) == 0) {
            const char *v = p + 13;
            int i = 0;
            while (*v == ' ') v++;
            while (*v && *v != '\r' && *v != '\n' && *v != ';' &&
                   i < (int)sizeof(resp->content_type) - 1) {
                resp->content_type[i++] = *v++;
            }
            resp->content_type[i] = 0;
        }

        p = line_end + 1;
    }

    /* 响应体 */
    if (p < end) {
        resp->body_len = end - p;
        resp->body = memory_alloc(resp->body_len + 1);
        if (resp->body) {
            memmove(resp->body, p, resp->body_len);
            resp->body[resp->body_len] = 0;
        }
    }

    return 0;
}

/* 执行HTTP请求 */
static int
http_execute(struct http_request *req, struct http_response *resp)
{
    int sock;
    struct ip_endpoint foreign;
    char *sendbuf, *recvbuf;
    int sendlen, recvlen, total;
    int ret = -1;

    /* 分配缓冲区 */
    sendbuf = memory_alloc(PGSIZE);
    recvbuf = memory_alloc(PGSIZE);
    if (!sendbuf || !recvbuf) {
        errorf("http: memory_alloc failed");
        goto cleanup;
    }

    /* 解析主机名为IP（目前只支持直接IP地址） */
    foreign.addr = parse_ip(req->host);  /* parse_ip已返回网络字节序 */
    foreign.port = hton16(req->port);

    /* 创建TCP连接 */
    sock = tcp_open();
    if (sock < 0) {
        errorf("http: tcp_open failed");
        goto cleanup;
    }

    debugf("http: connecting to %s:%d", req->host, req->port);
    if (tcp_connect(sock, &foreign) < 0) {
        errorf("http: tcp_connect failed");
        tcp_close(sock);
        goto cleanup;
    }

    /* 构建并发送请求 */
    sendlen = http_build_request(req, sendbuf, PGSIZE);
    debugf("http: sending %d bytes", sendlen);

    if (tcp_send(sock, (uint8 *)sendbuf, sendlen) < 0) {
        errorf("http: tcp_send failed");
        tcp_close(sock);
        goto cleanup;
    }

    /* 接收响应 */
    total = 0;
    while (total < PGSIZE - 1) {
        recvlen = tcp_receive(sock, (uint8 *)recvbuf + total, PGSIZE - 1 - total);
        if (recvlen <= 0) break;
        total += recvlen;
        debugf("http: received %d bytes (total %d)", recvlen, total);
    }
    recvbuf[total] = 0;

    /* 关闭连接 */
    tcp_close(sock);

    /* 解析响应 */
    if (total > 0) {
        ret = http_parse_response(recvbuf, total, resp);
        debugf("http: status=%d, content_length=%d",
               resp->status_code, resp->content_length);
    }

cleanup:
    if (sendbuf) memory_free(sendbuf);
    if (recvbuf) memory_free(recvbuf);
    return ret;
}

/* HTTP GET请求 */
int
http_get(const char *url, struct http_response *resp)
{
    struct http_request req;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_METHOD_GET;

    if (http_parse_url(url, req.host, sizeof(req.host),
                       &req.port, req.path, sizeof(req.path)) < 0) {
        errorf("http: invalid URL: %s", url);
        return -1;
    }

    debugf("http GET: host=%s, port=%d, path=%s", req.host, req.port, req.path);
    return http_execute(&req, resp);
}

/* HTTP POST请求 */
int
http_post(const char *url, const char *content_type,
          const uint8 *body, int body_len, struct http_response *resp)
{
    struct http_request req;
    int i;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_METHOD_POST;
    req.body = (char *)body;
    req.body_len = body_len;

    if (content_type) {
        for (i = 0; content_type[i] && i < (int)sizeof(req.content_type) - 1; i++) {
            req.content_type[i] = content_type[i];
        }
        req.content_type[i] = 0;
    }

    if (http_parse_url(url, req.host, sizeof(req.host),
                       &req.port, req.path, sizeof(req.path)) < 0) {
        errorf("http: invalid URL: %s", url);
        return -1;
    }

    debugf("http POST: host=%s, port=%d, path=%s", req.host, req.port, req.path);
    return http_execute(&req, resp);
}

/* ch9: 带Authorization的HTTP POST请求 */
int
http_post_with_auth(const char *url, const char *content_type,
                    const uint8 *body, int body_len,
                    const char *auth, struct http_response *resp)
{
    struct http_request req;
    int i;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_METHOD_POST;
    req.body = (char *)body;
    req.body_len = body_len;

    if (content_type) {
        for (i = 0; content_type[i] && i < (int)sizeof(req.content_type) - 1; i++) {
            req.content_type[i] = content_type[i];
        }
        req.content_type[i] = 0;
    }

    if (auth) {
        for (i = 0; auth[i] && i < (int)sizeof(req.authorization) - 1; i++) {
            req.authorization[i] = auth[i];
        }
        req.authorization[i] = 0;
    }

    if (http_parse_url(url, req.host, sizeof(req.host),
                       &req.port, req.path, sizeof(req.path)) < 0) {
        errorf("http: invalid URL: %s", url);
        return -1;
    }

    debugf("http POST (auth): host=%s, port=%d, path=%s", req.host, req.port, req.path);
    return http_execute(&req, resp);
}

/* 释放响应资源 */
void
http_response_free(struct http_response *resp)
{
    if (resp->body) {
        memory_free(resp->body);
        resp->body = 0;
    }
}

/* HTTP模块初始化 */
int
http_init(void)
{
    infof("http: initialized");
    return 0;
}
