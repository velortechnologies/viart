#ifndef VIART_H
#define VIART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct viart_client viart_client;

typedef struct {
    const uint8_t *data;
    size_t len;
} viart_bytes;

typedef enum {
    VIART_CONNECTED = 1,
    VIART_DISCONNECTED = 2,
    VIART_MESSAGE = 3,
    VIART_ACK = 4,
    VIART_CONNECT_ERROR = 5,
} viart_event_kind;

typedef struct {
    viart_event_kind kind;
    int error;                 /* errno-style error for DISCONNECTED/CONNECT_ERROR */
    uint32_t id;              /* operation id for ACK */
    uint8_t result;           /* VIART result code for ACK (1 = OK) */
    uint8_t frame_kind;       /* 1=publish, 0x12=direct, 0x13=broadcast */
    bool realtime;
    viart_bytes sender;
    viart_bytes topic;
    viart_bytes payload;
} viart_event;

/* Events run on the reactor thread. Borrowed byte spans live only until return. */
typedef void (*viart_event_fn)(viart_client *, const viart_event *, void *user);

typedef struct {
    const char *endpoint;     /* Unix path, IPv4:port, ws://IPv4:port, wss://IPv4:port */
    const char *name;         /* unique VIART client name */
    viart_event_fn on_event;
    void *user;
    size_t max_queued_bytes;  /* 0 = 4 MiB */
    uint32_t max_frame_bytes; /* 0 = 8 MiB */
    uint32_t reconnect_ms;    /* 0 = 1000 ms */
    const char *bearer_token; /* optional WS/WSS Authorization bearer token */
    const char *tls_ca_file;  /* optional CA PEM for WSS; system trust otherwise */
} viart_client_options;

enum { VIART_QOS_NO = 0, VIART_QOS_PROCESSED = 1,
       VIART_QOS_REALTIME = 2, VIART_QOS_REALTIME_PROCESSED = 3 };

int viart_client_create(const viart_client_options *options, viart_client **out);
int viart_client_start(viart_client *client);
void viart_client_stop(viart_client *client);
void viart_client_destroy(viart_client *client);
bool viart_client_is_connected(const viart_client *client);

/* Returns 0 when queued locally; processed QoS produces a later ACK event. */
int viart_client_publish(viart_client *, const char *topic, const void *data,
                         size_t len, uint8_t qos, uint32_t *id);
int viart_client_send(viart_client *, const char *target, const void *data,
                      size_t len, uint8_t qos, uint32_t *id);
int viart_client_broadcast(viart_client *, const char *target, const void *data,
                           size_t len, uint8_t qos, uint32_t *id);
int viart_client_subscribe(viart_client *, const char *topic, uint8_t qos, uint32_t *id);
int viart_client_unsubscribe(viart_client *, const char *topic, uint8_t qos, uint32_t *id);
int viart_client_publish_for(viart_client *, const char *topic, const char *receiver,
                             const void *data, size_t len, uint8_t qos, uint32_t *id);
int viart_client_exclude(viart_client *, const char *topic, uint8_t qos, uint32_t *id);
int viart_client_unexclude(viart_client *, const char *topic, uint8_t qos, uint32_t *id);

#ifdef __cplusplus
}
#endif
#endif
