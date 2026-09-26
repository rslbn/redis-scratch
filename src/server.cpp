#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include "../include/utils.h"
#include "../include/server.h"
#include "../include/hashtable.h"
#include "../include/enums.h"
#include "../include/common.h"
#include "../include/zset.h"


static const size_t K_MAX_MSG = 32 << 20;
static const size_t K_MAX_ARGS = 200 * 1000;
typedef std::vector<uint8_t> Buffer;

static struct {
    HMap db; // top-level hashtable
} g_data;

// KV pair for the top level hashtable
struct Entry {
    struct HNode node; // hashtable node
    std::string key;
    uint32_t type = 0;
    std::string str;
    ZSet zset;
};

static Entry *entry_new(uint32_t type) {
    Entry *ent = new Entry();
    ent->type = type;
    return ent;
}

static void entry_del(Entry *ent) {
    if (ent->type == T_ZSET) {
        zset_clear(&ent->zset);
    }
    delete ent;
}

struct LookupKey {
    struct HNode node; // hastable node
    std::string key;
};

static bool entry_eq(HNode *node, HNode *key) {
    struct Entry *le = container_of(node, struct Entry, node);
    struct LookupKey *keydata = container_of(key, struct LookupKey, node);
    return le->key == keydata->key;
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

static void buf_append_u8(Buffer &buf, uint8_t data) {
    buf.push_back(data);
}

static void buf_append_u32(Buffer &buf, uint32_t data) {
    buf_append(buf, (const uint8_t *)&data, 4); // assume little-endian
}

static void buf_append_dbl(Buffer &buf, double data) {
    buf_append(buf, (const uint8_t *)&data, 8);
}

static void out_nil(Buffer &out) {
    buf_append_u8(out, TAG_NIL);
}

static void out_str(Buffer &out, const char *s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t) size);
    buf_append(out, (const uint8_t *)s, size);
}

static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_u32(out, val);
}

static void out_arr(Buffer &out, uint32_t n) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

static size_t out_begin_arr(Buffer &out) {
    out.push_back(TAG_ARR);
    buf_append_u32(out, 0);
    return out.size() - 4;
}

static void out_end_arr(Buffer &out, size_t ctx, uint32_t n) {
    assert(out[ctx - 1] == TAG_ARR);
    memcpy(&out[ctx], &n, 4);
}

static void out_dbl(Buffer &out, double val) {
    buf_append_u8(out, TAG_DBL);
    buf_append_dbl(out, val);
}

static void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());
    buf_append(out, (const uint8_t *)msg.data(), msg.size());
}

static bool cb_keys(HNode *node, void *arg) {
    Buffer &out = *(Buffer *)arg;
    const std::string &key = container_of(node, Entry, node)->key;
    out_str(out, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, Buffer &out) {
    out_arr(out, (uint32_t )hm_size(&g_data.db));
    hm_foreach(&g_data.db, &cb_keys, (void *)&out);
}

static bool str2dbl(const std::string &s, double &out) {
    char *endp = NULL;
    out = strtod(s.c_str(), &endp);
    return endp == s.c_str() + s.size() && !isnan(out);
}

static bool str2int(const std::string &s, int64_t &out) {
    char *endp = NULL;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str() + s.size();
}

static void do_zadd(std::vector<std::string> &cmd, Buffer &out) {
    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expect float");
    }

    LookupKey key;
    key.key.swap(cmd[1]); 
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);

    Entry *ent = NULL;
    if (!hnode) {
        ent = entry_new(T_ZSET);
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        hm_insert(&g_data.db, &ent->node);
    } else {
        ent = container_of(hnode, Entry, node);
        if (ent->type != T_ZSET) {
            return out_err(out, ERR_BAD_TYP, "expect zset");
        }
    }

    // add or update the tuple
    const std::string &name = cmd[3];
    bool added = zset_insert(&ent->zset, name.data(), name.size(), score);
    return out_int(out, (int64_t)added);
}

static const ZSet k_empty_zset;

static ZSet *expect_zset(std::string &s) {
    LookupKey key;
    key.key.swap(s);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!hnode) {
        return (ZSet *)&k_empty_zset; 
    }
    Entry *ent = container_of(hnode, Entry, node);
    return ent->type == T_ZSET ? &ent->zset : NULL;
}

static void do_zrem(std::vector<std::string> &cmd, Buffer &out) {
    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYP, "expect zset");
    }

    const std::string &name = cmd[2];
    ZNode *znode = zset_lookup(zset, name.data(), name.size());
    if (znode) {
        zset_delete(zset, znode);
    }
    return out_int(out, znode ? 1 : 0);
}

static void do_zscore(std::vector<std::string> &cmd, Buffer &out) {
    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYP, "expect zset");
    }

    const std::string &name = cmd[2];
    ZNode *znode = zset_lookup(zset, name.data(), name.size());
    return znode ? out_dbl(out, znode->score) : out_nil(out);
}

static void do_zquery(std::vector<std::string> &cmd, Buffer &out) {
    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expect fp number");
    }
    const std::string &name = cmd[3];
    int64_t offset = 0, limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit)) {
        return out_err(out, ERR_BAD_ARG, "expect int");
    }

    ZSet *zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYP, "expect zset");
    }

    if (limit <= 0) {
        return out_arr(out, 0);
    }

    ZNode *znode = zset_seekge(zset, score, name.data(), name.size());
    znode = znode_offset(znode, offset);

    // output
    size_t ctx = out_begin_arr(out);
    int64_t n = 0;
    while (znode && n < limit) {
        out_str(out, znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
        n += 2;
    }
    out_end_arr(out, ctx, (uint32_t)n);
}

static void do_get(std::vector<std::string> &cmd, Buffer &out) {
    // a dummy `entry` just for the lookup
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    // Hashtable lookup
    if (!node) {
        return out_nil(out);
    }

    // copy the value
    Entry *ent = container_of (node, Entry, node);
    if (ent->type != T_STR) {
        return out_err(out, ERR_BAD_TYP, "not a string value");
    }
    return out_str(out, ent->str.data(), ent->str.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &out) {
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    // hashtable lookup
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node) {
        // found, update the value
        Entry *ent = container_of(node, Entry, node);
        if (ent->type != T_STR) {
            return out_err(out, ERR_BAD_TYP, "a non-string value exists");
        }
        ent->str.swap(cmd[2]);
    } else {
        // not found, allocate and isnert a new pair
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->str.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, Buffer &out) {
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    // hashtable delete
    HNode *node = hm_delete(&g_data.db, &key.node, &entry_eq);
    if (node) { // deallocate the pair
        entry_del(container_of(node, Entry, node));
    }
    return out_int(out, node ? 1 : 0);
}

void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);

    if (errno) {
        die("fcntl error");
        return;
    }
    
    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

void do_request(std::vector<std::string> &cmd, Buffer &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        return do_get(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        return do_set(cmd, out);
    } else if (cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, out);
    } else if (cmd.size() == 1 && cmd[0] == "keys") {
        return do_keys(cmd, out);
    } else if (cmd.size() == 4 && cmd[0] == "zadd") {
        return do_zadd(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zrem") {
        return do_zrem(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zscore") {
        return do_zscore(cmd, out);
    } else if (cmd.size() == 6 && cmd[0] == "zquery") {
        return do_zscore(cmd, out);
    } else {
        out_err(out, ERR_UNKNOWN, "uknown command.");
    }
}

static void response_begin(Buffer &out, size_t *header) {
    *header = out.size(); // message header position
    buf_append_u32(out, 0); // reserve space
}

static size_t response_size(Buffer &out, size_t header) {
    return out.size() - header - 4;
}

static void response_end(Buffer &out, size_t header) {
    size_t msg_size = response_size(out, header);
    if (msg_size > K_MAX_MSG) {
        out.resize(header + 4);
        out_err(out, ERR_TO_BIG, "response is too big.");
        msg_size = response_size(out, header);
    }

    // msg header
    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

static bool try_one_request(Conn *conn) {
    if (conn->incoming.size() < 4) return false;

    
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > K_MAX_MSG) {
        msg("too long");
        conn->want_close = true;
        return false; // want close
    }
    
    // message body
    if (4 + len > conn->incoming.size()) {
        return false; // want read
    }
    const uint8_t *request = &conn->incoming[4];
    
    std::vector<std::string> cmd;
    if (parse_req(request, len, cmd)) {
        msg("bad request ");
        conn->want_close = true;
        return false;
    }
    // got one request, do some application logic
    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    // application logic done, remove the request message
    buf_consume(conn->incoming, 4 + len);
    return true;
}


int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(data, end, nstr)) {
        return -1;
    }

    if (nstr > K_MAX_ARGS) {
        return -1;
    }

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) {
            return -1;
        }
        out.push_back(std::string());
        if (!read_str(data, end ,len, out.back())) {
            return -1;
        }
    }

    if (data != end) {
        return -1;
    }
    return 0;
}

bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (cur + 4 > end) {
        return false;
    }
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (cur + n > end) {
        return false;
    }

    out.assign(cur, cur + n);
    cur += n;
    return true;
}

// void make_response(const Response &resp, std::vector<uint8_t> &out) {
//     uint32_t resp_len = 4 + (uint32_t)resp.data.size();
//     buf_append(out, (const uint8_t*)&resp_len, 4);
//     buf_append(out, (const uint8_t*)&resp.status, 4);
//     buf_append(out, resp.data.data(), resp.data.size());
// }

Conn *handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *) &client_addr, &addrlen);

    if (connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }

    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u.:%u\n", 
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
        ntohs(client_addr.sin_port)
    );

    // set the new connection to fd non-blocking mode
    fd_set_nb(connfd);
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
} 

void handle_read(Conn *conn) {
    // read some data 
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }

    // handle IO error
    if (rv < 0) {
        msg_errno("read() error");
        conn->want_close = true;
        return;
    }

    if (rv == 0) {
        if (conn->incoming.size() == 0) {
            msg("client closed");
        } else {
            msg("unexpected EOF");
        }
        conn->want_close = true;
        return;
    }

    buf_append(conn->incoming, buf, (size_t)rv);

    // parse requests and generate resposnes
    while (try_one_request(conn)) {}

    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;

        // the socket is likely ready to write a in request-response protocol
        // try to write it without waiting for the next iteration
        return handle_write(conn);
    }
}

void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;
        return;
    }

    buf_consume(conn->outgoing, (size_t) rv);
    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    } // else want write
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    // this is needed for most server applications
    int val = 1; // related to TIME_WAIT in tcp
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // port
    addr.sin_addr.s_addr = htonl(0); // wildcard address 0.0.0.0
    int rv = bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }

    fd_set_nb(fd);

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    std::vector<Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;


    while(true) {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for (Conn *conn: fd2conn) {
            if (!conn) {
                continue;
            }

            struct pollfd pfd = {conn->fd, POLLERR, 0};

            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
        
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }

            poll_args.push_back(pfd);
        }

        // wait for readiness
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) {
            continue; // not an error2
        }

        if (rv < 0) {
            die("poll");
        }

        // handle the listening socket
        if (poll_args[0].revents) {
            if (Conn *conn = handle_accept(fd)) {
                if (fd2conn.size() <= (size_t)conn->fd) {
                    fd2conn.resize(conn->fd + 1);
                }
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
            }
        }

        for (size_t i = 1; i < poll_args.size(); i++) {
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) {
                continue;
            }

            Conn *conn = fd2conn[poll_args[i].fd];
            if (ready & POLLIN) {
                assert(conn->want_read);
                handle_read(conn);
            }
            if (ready & POLLOUT) {
                assert(conn->want_write);
                handle_write(conn);
            }

            if ((ready & POLLERR) || conn->want_close ) {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        } // for each connection sockets
    } // the event loop
    return 0;
}