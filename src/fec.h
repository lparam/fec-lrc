#ifndef _FEC_H
#define _FEC_H

#include <stdint.h>
#include <stddef.h>

#define FEC_TYPE_DATA 0x1
#define FEC_TYPE_CODE 0x2

#define FEC_DUPLICATE       -1
#define FEC_UNRECOVERABLE   -2

#define FEC_OVERHEAD_BYTES   8

typedef enum {
    FEC_DATA = 0x1,
    FEC_CODE = 0x2
} fec_type_t;

typedef struct fec_packet {
    uint32_t seqid;
    uint16_t type;
    uint32_t ts;
    size_t len;
    uint8_t data[];
} fec_packet_t;

struct fec;

typedef int (*output_t)(struct fec *fec, uint8_t *buf, int len, void *user);

struct fec * fec_create(int data_shards, int code_shards, int chunk_size, void *user);
void fec_release(struct fec *fec);
void fec_setoutput(struct fec *fec, output_t output);
void fec_setclock(int64_t (*clock)(void));
int fec_decode(uint8_t *buf, uint32_t len, struct fec_packet *pkt);
int fec_input(struct fec *fec, struct fec_packet *pkt);
int fec_send(struct fec *fec, const void *buf, size_t len);
int fec_recv(struct fec *fec, uint8_t *buf, size_t len);
int fec_read_size(struct fec *fec);

#endif // for #ifndef _FEC_H
