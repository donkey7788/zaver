#include "http.h"
#include "rio.h"
#include "dbg.h"

#define CR '\r'
#define LF '\n'
#define CRLFCRLF "\r\n\r\n"

#define MAXLINE 8192
#define SHORTLINE 512
#define root "/home/zjs/macHome/lifeofzjs/public"

#define zv_str3_cmp(m, c0, c1, c2, c3)                                       \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)
#define zv_str3Ocmp(m, c0, c1, c2, c3)                                       \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)

#define zv_str4cmp(m, c0, c1, c2, c3)                                        \
    *(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0)

mime_type_t zaver_mime[] = 
{
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css"},
    {NULL ,"text/plain"}
};

int zv_http_parse_request_line(zv_http_request_t *r) {
    u_char c, ch, *p, *m;
    enum {
        sw_start = 0,
        sw_method,
        sw_spaces_before_uri,
        sw_after_slash_in_uri,
        sw_http,
        sw_http_H,
        sw_http_HT,
        sw_http_HTT,
        sw_http_HTTP,
        sw_first_major_digit,
        sw_major_digit,
        sw_first_minor_digit,
        sw_minor_digit,
        sw_spaces_after_digit,
        sw_almost_done
    } state;

    state = r->state;
    check(state == 0, "state should be 0");

    log_info("ready to parese request line, start = %d, last= %d", r->pos, r->last);
    for (p = r->pos; p < r->last; p++) {
        ch = *p;

        switch (state) {

        /* HTTP methods: GET, HEAD, POST */
        case sw_start:
            log_info("in sw_start");
            r->request_start = p;

            if (ch == CR || ch == LF) {
                break;
            }

            if ((ch < 'A' || ch > 'Z') && ch != '_') {
                return ZV_HTTP_PARSE_INVALID_METHOD;
            }

            state = sw_method;
            break;

        case sw_method:
            if (ch == ' ') {
                r->method_end = p;
                m = r->request_start;

                switch (p - m) {

                case 3:
                    if (zv_str3_cmp(m, 'G', 'E', 'T', ' ')) {
                        r->method = ZV_HTTP_GET;
                        break;
                    }

                    break;

                case 4:
                    if (zv_str3Ocmp(m, 'P', 'O', 'S', 'T')) {
                        r->method = ZV_HTTP_POST;
                        break;
                    }

                    if (zv_str4cmp(m, 'H', 'E', 'A', 'D')) {
                        r->method = ZV_HTTP_HEAD;
                        break;
                    }

                    break;
                default:
                    r->method = ZV_HTTP_UNKNOWN;
                    break;
                }
                state = sw_spaces_before_uri;
                break;
            }

            if ((ch < 'A' || ch > 'Z') && ch != '_') {
                return ZV_HTTP_PARSE_INVALID_METHOD;
            }

            break;

        /* space* before URI */
        case sw_spaces_before_uri:

            if (ch == '/') {
                r->uri_start = p;
                state = sw_after_slash_in_uri;
                break;
            }

            switch (ch) {
                case ' ':
                    break;
                default:
                    return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_after_slash_in_uri:

            switch (ch) {
            case ' ':
                r->uri_end = p;
                state = sw_http;
                break;
            default:
                break;
            }
            break;

        /* space+ after URI */
        case sw_http:
            switch (ch) {
            case ' ':
                break;
            case 'H':
                state = sw_http_H;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_H:
            switch (ch) {
            case 'T':
                state = sw_http_HT;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HT:
            switch (ch) {
            case 'T':
                state = sw_http_HTT;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HTT:
            switch (ch) {
            case 'P':
                state = sw_http_HTTP;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HTTP:
            switch (ch) {
            case '/':
                state = sw_first_major_digit;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        /* first digit of major HTTP version */
        case sw_first_major_digit:
            if (ch < '1' || ch > '9') {
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_major = ch - '0';
            state = sw_major_digit;
            break;

        /* major HTTP version or dot */
        case sw_major_digit:
            if (ch == '.') {
                state = sw_first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_major = r->http_major * 10 + ch - '0';
            break;

        /* first digit of minor HTTP version */
        case sw_first_minor_digit:
            if (ch < '0' || ch > '9') {
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_minor = ch - '0';
            state = sw_minor_digit;
            break;

        /* minor HTTP version or end of request line */
        case sw_minor_digit:
            if (ch == CR) {
                state = sw_almost_done;
                break;
            }

            if (ch == LF) {
                goto done;
            }

            if (ch == ' ') {
                state = sw_spaces_after_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_minor = r->http_minor * 10 + ch - '0';
            break;

        case sw_spaces_after_digit:
            switch (ch) {
            case ' ':
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                goto done;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        /* end of request line */
        case sw_almost_done:
            r->request_end = p - 1;
            switch (ch) {
            case LF:
                goto done;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
        }
    }

    r->pos = p;
    r->state = state;

    return ZV_AGAIN;

done:

    r->pos = p + 1;

    if (r->request_end == NULL) {
        r->request_end = p;
    }

    r->state = sw_start;

    return ZV_OK;
}

int zv_http_parse_request_body(zv_http_request_t *r) {
    u_char c, ch, *p, *m;
    enum {
        sw_start = 0,
        sw_cr,
        sw_crlf,
        sw_crlfcr
    } state;

    state = r->state;
    check(state == 0, "state should be 0");

    log_info("ready to parese request body, start = %d, last= %d", r->pos, r->last);
    for (p = r->pos; p < r->last; p++) {
        ch = *p;

        switch (state) {

        case sw_start:
            switch (ch) {
            case CR:
                state = sw_cr;
                break;
            default:
                break;
            }
            break;

        case sw_cr:
            switch (ch) {
            case LF:
                state = sw_crlf;
                break;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_crlf:
            switch (ch) {
            case CR:
                state = sw_crlfcr;
                break;
            default:
                state = sw_start;
                break;
            }
            break;

        case sw_crlfcr:
            switch (ch) {
            case LF:
                goto done;
            default:
                return ZV_HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        }   
    }

    r->pos = p;
    r->state = state;

    return ZV_AGAIN;

done:
    r->pos = p + 1;

    r->state = sw_start;

    return ZV_OK;
}


int zv_init_request_t(zv_http_request_t *r, int fd) {
    r->fd = fd;
    r->pos = r->last = r->buf;
    r->state = 0;

    return ZV_OK;
}

void* do_request(void *ptr) {
    zv_http_request_t *r = (zv_http_request_t *)ptr;
    int fd = r->fd;
    int rc;
    rio_t rio;
    char method[SHORTLINE], uri[SHORTLINE], version[SHORTLINE];
    char buf[MAXLINE];
    char filename[SHORTLINE];
    struct stat sbuf;
    int n;

    log_info("in fd %d, ready to inf for", fd);
    for(;;) {
    
        /*
        rc = rio_readlineb(&rio, buf, MAXLINE);
        check((rc >= 0 || rc == -EAGAIN), "read request line, rc should > 0");
        if (rc == -EAGAIN) {
            break;
        }

        sscanf(buf,"%s %s %s", method, uri, version);
        log_info("request %s from fd %d", uri, fd);
       
        if(strcasecmp(method, "GET")) {
            log_err("req line = %s", buf);
            do_error(fd, method, "501", "Not Implemented", "zaver doesn't support");
            continue;
        }

        rc = read_request_body(&rio);
        if (rc == 0) {
            log_info("ready to close fd %d", fd);
            close(fd);
            break;
        }
        */
        
        n = read(fd, r->last, (uint64_t)r->buf + MAX_BUF - (uint64_t)r->last);
        log_info("buffer remaining: %d", (uint64_t)r->buf + MAX_BUF - (uint64_t)r->last);
        if (n == 0) {   // EOF
            log_info("read return 0, ready to close fd %d", fd);
            goto err;
        }

        if (n < 0) {
            if (errno != EAGAIN) {
                log_err("read err");
                goto err;
            }
            break;
        }

        r->last += n;
        check(r->last <= r->buf + MAX_BUF, "r->last <= MAX_BUF");
        
        log_info("ready to parse request line"); 
        rc = zv_http_parse_request_line(r);
        if (rc == ZV_AGAIN) {
            continue;
        } else if (rc != ZV_OK) {
            log_err("rc != ZV_OK");
            goto err;
        }

        log_info("method == %.*s",r->method_end - r->request_start, r->request_start);
        log_info("uri == %.*s", r->uri_end - r->uri_start, r->uri_start);

        log_info("ready to parse request body");
        rc  = zv_http_parse_request_body(r);
        if (rc == ZV_AGAIN) {
            continue;
        } else if (rc != ZV_OK) {
            log_err("rc != ZV_OK");
            goto err;
        }

        //log_info("read_request_body %s suc: fd %d", uri, fd);
        parse_uri(r->uri_start, r->uri_end - r->uri_start, filename, NULL);

        if(stat(filename, &sbuf) < 0) {
            do_error(fd, filename, "404", "Not Found", "zaver can't find the file");
            continue;
        }

        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            do_error(fd, filename, "403", "Forbidden",
                    "zaver can't read the file");
            continue;
        }
        
        serve_static(fd, filename, sbuf.st_size);
        log_info("serve_static suc");

        fflush(stdout);
    }

err:
    close(fd);
}

int read_request_body(rio_t *rio) {
    check(rio != NULL, "rio == NULL");
    
    int rc;
    char buf[MAXLINE];
    rc = rio_readlineb(rio, buf, MAXLINE); 
    if (rc == 0) return rc;
    check(rc > 0, "rc > 0 while rc == %d", rc);
    while(strcmp(buf, "\r\n")) {
        log_info("in the read_request_body and buf == %s, len = %d", buf, strlen(buf));
        rc = rio_readlineb(rio, buf, MAXLINE); 
        check(rc > 0, "rc > 0");
    }

    return 1; 
}

void parse_uri(char *uri, int uri_length, char *filename, char *querystring) {
    strcpy(filename, root);
    strncat(filename, uri, uri_length);

    char *last_comp = rindex(filename, '/');
    char *last_dot = rindex(last_comp, '.');
    if (last_dot == NULL && filename[strlen(filename)-1] != '/') {
        strcat(filename, "/");
    }
    
    if(uri[strlen(uri)-1] == '/' || filename[strlen(filename)-1] == '/') {
        strcat(filename, "index.html");
    }

    log_info("filename = %s", filename);
    return;
}

void do_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char header[MAXLINE], body[MAXLINE];

    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n</p>", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny web server</em>\r\n", body);

    sprintf(header, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    sprintf(header, "%sServer: Zaver\r\n", header);
    sprintf(header, "%sContent-type: text/html\r\n", header);
    sprintf(header, "%sConnection: keep-alive\r\n", header);
    sprintf(header, "%sContent-length: %d\r\n\r\n", header, (int)strlen(body));
    //log_info("header  = \n %s\n", header);
    rio_writen(fd, header, strlen(header));
    rio_writen(fd, body, strlen(body));
    log_info("leave clienterror\n");
    return;
}

void serve_static(int fd, char *filename, int filesize) {
    log_info("filename = %s", filename);
    char header[MAXLINE];
    int n;
    
    const char *file_type;
    const char *dot_pos = rindex(filename, '.');
    file_type = get_file_type(dot_pos);

    sprintf(header, "HTTP/1.1 200 OK\r\n");
    sprintf(header, "%sServer: Zaver\r\n", header);
    sprintf(header, "%sContent-length: %d\r\n", header, filesize);
    sprintf(header, "%sConnection: keep-alive\r\n", header);
    sprintf(header, "%sContent-type: %s\r\n\r\n", header, file_type);
//    sprintf(header, "%sConnection: close\r\n\r\n", header);

    n = rio_writen(fd, header, strlen(header));
    check(n == strlen(header), "rio_writen error");

    int srcfd = open(filename, O_RDONLY, 0);
    check(srcfd > 2, "open error");
    // can use sendfile
    char *srcaddr = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    check(srcaddr > 0, "mmap error");
    close(srcfd);

    n = rio_writen(fd, srcaddr, filesize);
    check(n == filesize, "rio_writen error");

    munmap(srcaddr, filesize);
    return;
}

const char* get_file_type(const char *type)
{
    if (type == NULL) {
        return "text/plain";
    }

    int i;
    for (i = 0; zaver_mime[i].type != NULL; ++i) {
        if (strcmp(type, zaver_mime[i].type) == 0)
            return zaver_mime[i].value;
    }
    return zaver_mime[i].value;
}
