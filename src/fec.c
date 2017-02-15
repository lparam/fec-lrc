#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <lrc.h>

#include "fec.h"
#include "array.h"
#include "util.h"

#define FEC_DATA_BYTES   2
#define FEC_HEADER_SIZE  6

#define RX(idx) \
    (struct fec_packet *)((uint8_t *)fec->rx->elts + (idx) * fec->rx->size)

typedef struct {
    int index;
    size_t n;
    uint8_t *buf[];
} fec_recovered_t;

typedef struct fec {
    array_t *rx; // ordered list
    uint32_t rx_limit;
    uint32_t data_shards;
    uint32_t code_shards;
    uint32_t nshards;
    uint32_t next; // next seqid
    uint32_t paws; // max seqid
    uint32_t max_seqid;
    uint32_t count;
    uint32_t maxsize;
    uint64_t last_check;
	void *user;
    lrc_t *codec;
    lrc_buf_t *encode_buf;
    lrc_buf_t *decode_buf;
    fec_recovered_t *recovered;
    output_t output;
} fec_t;

const int FEC_EXPIRE = 30000; // 30s
static int64_t (*get_clock)(void);

static void
clean_codec_buf(lrc_buf_t *buf) {
    memset(buf->buf, 0, buf->aligned_chunk_size * buf->n);
}

static int
fec_output(struct fec *fec, uint8_t *data, int size) {
    assert(fec->output);
    return fec->output(fec, data, size, fec->user);
}

static int64_t
fec_now() {
    assert(get_clock);
    return get_clock();
}

struct fec *
fec_create(int data_shards, int code_shards, int chunk_size, void *user) {
    struct fec *fec = malloc(sizeof(*fec));
    memset(fec, 0, sizeof(*fec));

    fec->user = user;
    fec->data_shards = data_shards;
    fec->code_shards = code_shards;
    fec->nshards = data_shards + code_shards;
	fec->paws = (UINT32_MAX / fec->nshards - 1) * fec->nshards;
    fec->rx_limit = 3 * (data_shards + code_shards);

    fec->rx = array_create(fec->rx_limit, sizeof(struct fec_packet) + chunk_size);
    array_clean(fec->rx);

    fec->recovered = malloc(sizeof(fec_recovered_t) + sizeof(uint8_t *) * code_shards);

    fec->codec = malloc(sizeof(lrc_t));
    fec->encode_buf = malloc(sizeof(lrc_buf_t));
    fec->decode_buf = malloc(sizeof(lrc_buf_t));
    memset(fec->codec , 0, sizeof(lrc_t));
    memset(fec->encode_buf, 0, sizeof(lrc_buf_t));
    memset(fec->decode_buf, 0, sizeof(lrc_buf_t));

    lrc_init_n(fec->codec, 1, (uint8_t[]) {data_shards}, code_shards);
    lrc_buf_init(fec->encode_buf, fec->codec, chunk_size);
    lrc_buf_init(fec->decode_buf, fec->codec, chunk_size);
    clean_codec_buf(fec->encode_buf);
    clean_codec_buf(fec->decode_buf);

    return fec;
}

void
fec_release(struct fec *fec) {
    array_destroy(fec->rx);
    lrc_destroy(fec->codec);
    lrc_buf_destroy(fec->encode_buf);
    lrc_buf_destroy(fec->decode_buf);
    free(fec->codec);
    free(fec->encode_buf);
    free(fec->decode_buf);
    free(fec->recovered);
    free(fec);
}

void
fec_setoutput(struct fec *fec, output_t output) {
	fec->output = output;
}

void
fec_setclock(int64_t (*clock)(void)) {
	get_clock = clock;
}

static void
fec_encode(struct fec *fec, int type, uint8_t *buf, size_t len) {
    write_size_32(buf, fec->next++);
    if (type == FEC_DATA) {
        write_size(buf + 4, FEC_DATA);
        write_size(buf + 6, len);

    } else {
        write_size(buf + 4, FEC_CODE);
        if (fec->next >= fec->paws) {
            fec->next = 0;
        }
    }
}

int
fec_decode(uint8_t *buf, uint32_t len, struct fec_packet *pkt) {
    pkt->seqid = read_size_32(buf);
    pkt->type = read_size(buf + 4);
    pkt->ts = fec_now();
    pkt->len = len - FEC_HEADER_SIZE;
    memcpy(pkt->data, buf + FEC_HEADER_SIZE, pkt->len);
    return 0;
}

static void
fec_remove_shards(struct fec *fec, int begin, int nshards) {
    memmove(RX(begin), RX(begin + nshards),
            fec->rx->size * (fec->rx->nelts - begin - nshards));
    fec->rx->nelts -= nshards;
}

static void
fec_remove_shard(struct fec *fec, int idx) {
    memmove(RX(idx), RX(idx + 1), fec->rx->size * (fec->rx->nelts - idx - 1));
    fec->rx->nelts--;
}

static void
fec_limit_shards(struct fec *fec) {
    memmove(RX(0), RX(1), fec->rx->size * (fec->rx->nelts - 1));
    fec->rx->nelts--;
}

static int
fec_insert_shard(struct fec *fec, struct fec_packet *pkt) {
    int idx = 0;
    int n = fec->rx->nelts - 1;

    for (int i = n; i >= 0; i--) {
        if (pkt->seqid > (RX(i))->seqid) {
            idx = i + 1;
            break;
        } else if (pkt->seqid == (RX(i))->seqid) {
            return FEC_DUPLICATE;
        }
    }

    if (idx != n + 1) {
        memmove(RX(idx + 1), RX(idx), fec->rx->size * (n - idx + 1));
    }
    (RX(idx))->seqid = pkt->seqid;
    (RX(idx))->type = pkt->type;
    (RX(idx))->ts = pkt->ts;
    (RX(idx))->len = pkt->len;
    memcpy((RX(idx))->data, pkt->data, pkt->len);
    fec->rx->nelts++;

    return idx;
}

static int
fec_reconstruct(struct fec *fec, int8_t *erased, int subscripts[]) {
    int rc;
    int8_t sources[fec->nshards];

    rc = lrc_get_source(fec->codec, erased, sources);
    if (rc) {
        return FEC_UNRECOVERABLE;
    }

    clean_codec_buf(fec->decode_buf);
    for (int i = 0; i < fec->nshards; i++) {
        if (sources[i] == 1) {
            int idx = subscripts[i];
            if (i < fec->data_shards) {
                if ((RX(idx))->type == FEC_CODE) {
                    return FEC_UNRECOVERABLE;
                }
            }
            memcpy(fec->decode_buf->data[i], (RX(idx))->data, (RX(idx))->len);
        }
    }

    rc = lrc_decode(fec->codec, fec->decode_buf, erased);
    assert(rc == 0);
    int n = 0;
    for (int i = 0; i < fec->data_shards; i++) {
        if (erased[i]) {
            fec->recovered->buf[n++] = (uint8_t *)fec->decode_buf->data[i];
        }
    }
    fec->recovered->n = n;
    fec->recovered->index = 0;

    return n;
}

int
fec_input(struct fec *fec, struct fec_packet *pkt) {
    uint64_t current = fec_now();
    if (current - fec->last_check >= FEC_EXPIRE) {
        for (int i = 0; i < fec->rx->nelts; i++) {
            if (current - (RX(i))->ts >= FEC_EXPIRE) {
                fec_remove_shard(fec, i);
            }
        }
        fec->last_check = current;
    }

    int insert_idx = fec_insert_shard(fec, pkt);
    if (insert_idx < 0) {
        return FEC_DUPLICATE;
    }

    uint32_t shard_begin = pkt->seqid - (pkt->seqid % fec->nshards);
    uint32_t shard_end = shard_begin + fec->nshards - 1;

    int search_begin = insert_idx - (pkt->seqid % fec->nshards);
    if (search_begin < 0) {
        search_begin = 0;
    }
    int search_end = search_begin + fec->nshards - 1;
    if (search_end >= fec->rx->nelts) {
        search_end = fec->rx->nelts - 1;
    }

	if ((search_end - search_begin + 1) >= fec->data_shards) {
        int begin = -1;
        int nshards = 0;
        int n_data_shards = 0;
        int subscripts[fec->nshards];
        int8_t erased[fec->nshards];

        memset(subscripts, 0, fec->nshards);
        memset(erased, 1, fec->nshards);

        for (int i = search_begin; i <= search_end; i++) {
            uint32_t seqid = (RX(i))->seqid;
            if (seqid > shard_end) {
                break;
            }
            if (seqid >= shard_begin) {
                int idx = seqid % fec->nshards;
                subscripts[idx] = i;
                erased[idx] = 0;
                if ((RX(i))->type == FEC_DATA) {
                    n_data_shards++;
                }
                if (++nshards == 1) {
                    begin = i;
                }
            }
        }
        if (n_data_shards == fec->data_shards) {
            fec_remove_shards(fec, begin, nshards);
            return 0;

        } else if (nshards >= fec->data_shards) {
            int n = fec_reconstruct(fec, erased, subscripts);
            fec_remove_shards(fec, begin, nshards);
            return n;
        }
    }

    if (fec->rx->nelts >= fec->rx_limit) {
        fec_limit_shards(fec);
    }

    return 0;
}

int
fec_send(struct fec *fec, const void *buf, size_t len) {
    uint8_t stage[FEC_OVERHEAD_BYTES];
    uint8_t data[FEC_OVERHEAD_BYTES + len];

    fec_encode(fec, FEC_DATA, stage, len);
    memcpy(data, stage, FEC_OVERHEAD_BYTES);
    memcpy(data + FEC_OVERHEAD_BYTES, buf, len);

    memcpy(fec->encode_buf->data[fec->count++], data + FEC_HEADER_SIZE,
           len + FEC_DATA_BYTES);

    fec_output(fec, data, FEC_OVERHEAD_BYTES + len);

    if (len + FEC_DATA_BYTES > fec->maxsize) {
        fec->maxsize = len + FEC_DATA_BYTES;
    }

    if (fec->count == fec->data_shards) {
        int rc = lrc_encode(fec->codec, fec->encode_buf);
        assert(rc == 0);
        uint8_t code[FEC_HEADER_SIZE + fec->maxsize];
        for (int i = 0; i < fec->code_shards; i++) {
            fec_encode(fec, FEC_CODE, stage, 0);
            memcpy(code, stage, FEC_HEADER_SIZE);
            memcpy(code + FEC_HEADER_SIZE, fec->encode_buf->code[i], fec->maxsize);
            fec_output(fec, code, FEC_HEADER_SIZE + fec->maxsize);
        }
        fec->count = 0;
        fec->maxsize = 0;
        clean_codec_buf(fec->encode_buf);
    }

    return 0;
}

int
fec_read_size(struct fec *fec) {
    if (fec->recovered->n <= 0) {
        return -1;
    }
    uint32_t sz = read_size(fec->recovered->buf[fec->recovered->index]);
    return sz;
}

int
fec_recv(struct fec *fec, uint8_t *buf, size_t len) {
    if (fec->recovered->n <= 0) {
        return -1;
    }
    uint8_t *r = fec->recovered->buf[fec->recovered->index++] + FEC_DATA_BYTES;
    memcpy(buf, r, len);
    fec->recovered->n--;
    return len;
}
